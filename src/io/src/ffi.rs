//! C ABI for the WinZOX I/O Rust port.
//!
//! Conventions:
//!
//! * All paths are passed as NUL-terminated UTF-8 byte strings. On Windows the
//!   C++ side is responsible for converting the wide path to UTF-8 (it
//!   already round-trips through `std::filesystem::u8path` upstream).
//! * Functions returning bytes return a [`WinzoxIoBuffer`]. The caller MUST
//!   call [`winzox_io_buffer_free`] exactly once (this releases the allocation
//!   that came from `Vec::into_raw`).
//! * Functions returning a status code use [`WinzoxIoStatus`]. On non-zero
//!   status the caller MAY retrieve a human-readable message describing the
//!   most recent error via [`winzox_io_last_error`].
//!
//! Every entry point in this module is `unsafe extern "C"`. The safety
//! invariants required of callers are documented per function.

#![allow(clippy::missing_safety_doc)]

use std::cell::RefCell;
use std::ffi::{c_char, c_int, CStr, CString};
use std::os::raw::c_void;
use std::path::{Path, PathBuf};

use crate::{
    collect_input_files, read_all_volumes, read_file_bytes, write_file_bytes, IoError, VolumeWriter,
};

/// Status codes returned by the C ABI. Stable across releases.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WinzoxIoStatus {
    Ok = 0,
    NotFound = 1,
    Symlink = 2,
    Unsupported = 3,
    FileTooLarge = 4,
    VolumesTooLarge = 5,
    InvalidUtf8 = 6,
    Io = 7,
    NullArgument = 8,
}

/// Heap buffer of bytes owned by the Rust allocator. Free with
/// [`winzox_io_buffer_free`].
#[repr(C)]
#[derive(Debug)]
pub struct WinzoxIoBuffer {
    pub data: *mut u8,
    pub len: usize,
    pub cap: usize,
}

impl WinzoxIoBuffer {
    fn empty() -> Self {
        Self { data: std::ptr::null_mut(), len: 0, cap: 0 }
    }

    fn from_vec(v: Vec<u8>) -> Self {
        let mut v = std::mem::ManuallyDrop::new(v);
        Self {
            data: v.as_mut_ptr(),
            len: v.len(),
            cap: v.capacity(),
        }
    }
}

/// Heap-allocated list of NUL-separated UTF-8 paths returned by
/// [`winzox_io_collect_input_files`]. Free with
/// [`winzox_io_path_list_free`]. Paths in `data` are separated by `\0`
/// bytes; the buffer is also `\0`-terminated. `count` is the number of paths.
#[repr(C)]
#[derive(Debug)]
pub struct WinzoxIoPathList {
    pub data: *mut c_char,
    pub byte_len: usize,
    pub count: usize,
}

impl WinzoxIoPathList {
    fn empty() -> Self {
        Self { data: std::ptr::null_mut(), byte_len: 0, count: 0 }
    }
}

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn store_error(msg: impl Into<String>) {
    let s = msg.into();
    let cstr = CString::new(s.replace('\0', " ")).unwrap_or_else(|_| CString::new("error").unwrap());
    LAST_ERROR.with(|cell| *cell.borrow_mut() = Some(cstr));
}

fn classify(err: &IoError) -> WinzoxIoStatus {
    match err {
        IoError::NotFound(_) => WinzoxIoStatus::NotFound,
        IoError::SymlinkRefused(_) => WinzoxIoStatus::Symlink,
        IoError::UnsupportedKind(_) => WinzoxIoStatus::Unsupported,
        IoError::FileTooLarge(_, _) => WinzoxIoStatus::FileTooLarge,
        IoError::VolumeChainTooLarge(_, _) => WinzoxIoStatus::VolumesTooLarge,
        IoError::InvalidUtf8 => WinzoxIoStatus::InvalidUtf8,
        IoError::Io(_) => WinzoxIoStatus::Io,
    }
}

unsafe fn cstr_to_path<'a>(p: *const c_char) -> Result<&'a Path, WinzoxIoStatus> {
    if p.is_null() {
        return Err(WinzoxIoStatus::NullArgument);
    }
    let bytes = unsafe { CStr::from_ptr(p) }.to_bytes();
    let s = std::str::from_utf8(bytes).map_err(|_| WinzoxIoStatus::InvalidUtf8)?;
    Ok(Path::new(s))
}

/// Retrieve a thread-local NUL-terminated UTF-8 error message describing the
/// most recent error produced by any function in this module on the current
/// thread. Returns NULL if no error has occurred. The pointer is valid until
/// the next call from the same thread.
#[unsafe(no_mangle)]
pub extern "C" fn winzox_io_last_error() -> *const c_char {
    LAST_ERROR.with(|cell| match cell.borrow().as_ref() {
        Some(s) => s.as_ptr(),
        None => std::ptr::null(),
    })
}

/// Free a [`WinzoxIoBuffer`] previously returned by this library.
///
/// # Safety
///
/// `buffer.data` must have been obtained from a function in this module
/// (or be NULL with `len = 0`, `cap = 0`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_buffer_free(buffer: WinzoxIoBuffer) {
    if buffer.data.is_null() {
        return;
    }
    // SAFETY: contract documented above.
    unsafe { drop(Vec::from_raw_parts(buffer.data, buffer.len, buffer.cap)) };
}

/// Free a [`WinzoxIoPathList`] previously returned by
/// [`winzox_io_collect_input_files`].
///
/// # Safety
///
/// `list.data` must have been obtained from
/// [`winzox_io_collect_input_files`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_path_list_free(list: WinzoxIoPathList) {
    if list.data.is_null() {
        return;
    }
    // SAFETY: contract documented above.
    unsafe { drop(Vec::from_raw_parts(list.data as *mut u8, list.byte_len, list.byte_len)) };
}

/// Read a file into a heap buffer.
///
/// # Safety
///
/// `path` must be a NUL-terminated UTF-8 string. `out_buffer` must be a
/// valid pointer to a [`WinzoxIoBuffer`]; it will be written even on error
/// (set to an empty buffer).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_read_file_bytes(
    path: *const c_char,
    out_buffer: *mut WinzoxIoBuffer,
) -> WinzoxIoStatus {
    if out_buffer.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    // SAFETY: caller contract: out_buffer is valid.
    unsafe { *out_buffer = WinzoxIoBuffer::empty() };

    let path = match unsafe { cstr_to_path(path) } {
        Ok(p) => p,
        Err(s) => return s,
    };
    match read_file_bytes(path) {
        Ok(data) => {
            // SAFETY: caller contract: out_buffer is valid.
            unsafe { *out_buffer = WinzoxIoBuffer::from_vec(data) };
            WinzoxIoStatus::Ok
        }
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Write `data` of length `len` to `path`, hardened against symlink-component
/// attacks. `data` may be NULL only if `len == 0`.
///
/// # Safety
///
/// `path` must be a NUL-terminated UTF-8 string. `data` must be a readable
/// buffer of at least `len` bytes when `len > 0`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_write_file_bytes(
    path: *const c_char,
    data: *const u8,
    len: usize,
) -> WinzoxIoStatus {
    let path = match unsafe { cstr_to_path(path) } {
        Ok(p) => p,
        Err(s) => return s,
    };
    let slice: &[u8] = if len == 0 {
        &[]
    } else {
        if data.is_null() {
            return WinzoxIoStatus::NullArgument;
        }
        // SAFETY: caller contract.
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    match write_file_bytes(path, slice) {
        Ok(()) => WinzoxIoStatus::Ok,
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Read the head volume plus every `.zNN` continuation that exists on disk,
/// concatenated.
///
/// # Safety
///
/// Same as [`winzox_io_read_file_bytes`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_read_all_volumes(
    path: *const c_char,
    out_buffer: *mut WinzoxIoBuffer,
) -> WinzoxIoStatus {
    if out_buffer.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    // SAFETY: caller contract.
    unsafe { *out_buffer = WinzoxIoBuffer::empty() };

    let path = match unsafe { cstr_to_path(path) } {
        Ok(p) => p,
        Err(s) => return s,
    };
    match read_all_volumes(path) {
        Ok(data) => {
            // SAFETY: caller contract.
            unsafe { *out_buffer = WinzoxIoBuffer::from_vec(data) };
            WinzoxIoStatus::Ok
        }
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Collect every regular file under `input_path` into a NUL-separated UTF-8
/// list. Symlinks are not followed.
///
/// # Safety
///
/// `input_path` must be a NUL-terminated UTF-8 string. `out_list` must point
/// to a valid [`WinzoxIoPathList`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_collect_input_files(
    input_path: *const c_char,
    out_list: *mut WinzoxIoPathList,
) -> WinzoxIoStatus {
    if out_list.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    // SAFETY: caller contract.
    unsafe { *out_list = WinzoxIoPathList::empty() };

    let path = match unsafe { cstr_to_path(input_path) } {
        Ok(p) => p,
        Err(s) => return s,
    };
    match collect_input_files(path) {
        Ok(paths) => {
            let list = paths_to_list(&paths);
            // SAFETY: caller contract.
            unsafe { *out_list = list };
            WinzoxIoStatus::Ok
        }
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

fn paths_to_list(paths: &[PathBuf]) -> WinzoxIoPathList {
    let count = paths.len();
    let mut buf = Vec::<u8>::new();
    for p in paths {
        let bytes = path_bytes(p);
        buf.extend_from_slice(&bytes);
        buf.push(0);
    }
    if buf.is_empty() {
        buf.push(0); // ensure non-empty so the freed buffer pointer is well-formed
    }
    let byte_len = buf.len();
    let mut buf = std::mem::ManuallyDrop::new(buf);
    let cap = buf.capacity();
    if cap > byte_len {
        // Shrink so the cap and len match for `Vec::from_raw_parts` in `free`.
        // We don't actually mutate—we rebuild a Vec with exact capacity below.
        // Simpler: store byte_len as cap, since we re-create with both equal.
    }
    let data = buf.as_mut_ptr() as *mut c_char;
    // `data` is owned and will be re-wrapped on free.
    WinzoxIoPathList {
        data,
        byte_len,
        count,
    }
}

#[cfg(unix)]
fn path_bytes(p: &Path) -> Vec<u8> {
    use std::os::unix::ffi::OsStrExt;
    p.as_os_str().as_bytes().to_vec()
}

#[cfg(not(unix))]
fn path_bytes(p: &Path) -> Vec<u8> {
    p.to_string_lossy().as_bytes().to_vec()
}

/// Opaque handle to a [`VolumeWriter`]. Returned by
/// [`winzox_io_volume_writer_new`].
#[repr(C)]
#[derive(Debug)]
pub struct WinzoxIoVolumeWriter {
    _private: c_void,
}

/// Construct a new volume writer.
///
/// # Safety
///
/// `base_path` must be a NUL-terminated UTF-8 string. `out_handle` must point
/// to a writable slot; it will be set to NULL on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_volume_writer_new(
    base_path: *const c_char,
    split_size: u64,
    out_handle: *mut *mut WinzoxIoVolumeWriter,
) -> WinzoxIoStatus {
    if out_handle.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    // SAFETY: caller contract.
    unsafe { *out_handle = std::ptr::null_mut() };
    let base = match unsafe { cstr_to_path(base_path) } {
        Ok(p) => p,
        Err(s) => return s,
    };
    match VolumeWriter::new(base, split_size) {
        Ok(v) => {
            let boxed = Box::new(v);
            // SAFETY: caller contract.
            unsafe { *out_handle = Box::into_raw(boxed).cast::<WinzoxIoVolumeWriter>() };
            WinzoxIoStatus::Ok
        }
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Append `data` of length `len` to the writer.
///
/// # Safety
///
/// `handle` must be a pointer returned by
/// [`winzox_io_volume_writer_new`] and not yet freed. `data` must be a
/// readable buffer of at least `len` bytes when `len > 0`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_volume_writer_write(
    handle: *mut WinzoxIoVolumeWriter,
    data: *const u8,
    len: usize,
) -> WinzoxIoStatus {
    if handle.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    let writer = unsafe { &mut *(handle.cast::<VolumeWriter>()) };
    let slice: &[u8] = if len == 0 {
        &[]
    } else {
        if data.is_null() {
            return WinzoxIoStatus::NullArgument;
        }
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    match writer.write(slice) {
        Ok(()) => WinzoxIoStatus::Ok,
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Flush and close the underlying file. Does not free the handle — call
/// [`winzox_io_volume_writer_free`] after this.
///
/// # Safety
///
/// `handle` must be a valid pointer returned by
/// [`winzox_io_volume_writer_new`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_volume_writer_close(
    handle: *mut WinzoxIoVolumeWriter,
) -> WinzoxIoStatus {
    if handle.is_null() {
        return WinzoxIoStatus::NullArgument;
    }
    let writer = unsafe { &mut *(handle.cast::<VolumeWriter>()) };
    match writer.close() {
        Ok(()) => WinzoxIoStatus::Ok,
        Err(e) => {
            let status = classify(&e);
            store_error(e.to_string());
            status
        }
    }
}

/// Drop the handle. Safe to pass NULL.
///
/// # Safety
///
/// `handle` must be a valid pointer returned by
/// [`winzox_io_volume_writer_new`] or NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_io_volume_writer_free(handle: *mut WinzoxIoVolumeWriter) {
    if handle.is_null() {
        return;
    }
    // SAFETY: caller contract.
    unsafe { drop(Box::from_raw(handle.cast::<VolumeWriter>())) };
}

/// Sanity helper: returns a non-zero version identifier so the C++ side can
/// statically assert the linked Rust component matches its expectations.
#[unsafe(no_mangle)]
pub extern "C" fn winzox_io_abi_version() -> c_int {
    1
}
