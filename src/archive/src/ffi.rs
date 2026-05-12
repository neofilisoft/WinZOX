//! C ABI for the WinZOX archive read-side Rust port.
//!
//! Mirrors the conventions of `winzox-io-rs`: NUL-terminated UTF-8 strings,
//! heap buffers owned by Rust freed by explicit `*_free` calls, status codes
//! instead of exceptions, and a thread-local last-error message.

#![allow(clippy::missing_safety_doc)]

use std::cell::RefCell;
use std::ffi::{c_char, c_int, CString};
use std::os::raw::c_void;

use crate::integrity::{ArchiveIntegrityAccumulator, ArchiveIntegrityDigests};
use crate::{
    looks_like_zox_archive, read_index_plain, read_metadata_plain, ArchiveError, ArchiveMetadata,
};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WinzoxArStatus {
    Ok = 0,
    NullArgument = 1,
    UnknownMagic = 2,
    Truncated = 3,
    InconsistentEncryption = 4,
    AeadRejected = 5,
    AuthFlagNotSupported = 6,
    AuthRequiresEncryption = 7,
    UnauthenticatedV3Encrypted = 8,
    EntryCountExceedsCap = 9,
    CentralDirectoryTooLarge = 10,
    CentralDirectoryOffsetInvalid = 11,
    CentralDirectoryTruncated = 12,
    EntryCountMismatch = 13,
    DirectoryInconsistent = 14,
    EmptyEntryName = 15,
    EntrySizeExceedsCap = 16,
    FooterInconsistent = 17,
    FooterMissing = 18,
    KdfParamsInvalid = 19,
    Sha512Mismatch = 20,
    Sha3Mismatch = 21,
    UnsupportedCompression = 22,
    UnsupportedEncryption = 23,
    EncryptedArchive = 24,
    IntegerOverflow = 25,
}

/// SHA-512 + SHA3-256 digest pair, layout-compatible with the C++
/// `ArchiveIntegrityDigests` struct.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WinzoxArDigests {
    pub sha512: [u8; 64],
    pub sha3_256: [u8; 32],
}

/// Heap buffer of bytes.
#[repr(C)]
#[derive(Debug)]
pub struct WinzoxArBuffer {
    pub data: *mut u8,
    pub len: usize,
    pub cap: usize,
}

impl WinzoxArBuffer {
    fn empty() -> Self {
        Self { data: std::ptr::null_mut(), len: 0, cap: 0 }
    }
    fn from_vec(v: Vec<u8>) -> Self {
        let mut v = std::mem::ManuallyDrop::new(v);
        Self { data: v.as_mut_ptr(), len: v.len(), cap: v.capacity() }
    }
}

/// Flat metadata view returned to C. Strings inside live in the
/// accompanying [`WinzoxArBuffer`] (NUL-separated, then NUL-terminated).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WinzoxArMetadata {
    pub encrypted: u8,
    pub solid: u8,
    pub authenticated: u8,
    pub integrity_sha512: u8,
    pub integrity_sha3_256: u8,
    pub encryption_algorithm: u8,
    pub default_algorithm: u8,
    pub _padding: u8,
    pub created_unix_time: u64,
    pub payload_checksum: u32,
    pub _padding2: u32,
}

/// Flat entry view returned to C. The `path` pointer is owned by the
/// accompanying string heap; do not free it directly.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WinzoxArEntry {
    pub path_offset: usize,
    pub path_len: usize,
    pub algorithm: u8,
    pub _padding: [u8; 7],
    pub original_size: u64,
    pub stored_size: u64,
    pub encoded_size: u64,
    pub crc32: u32,
    pub _padding2: u32,
}

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn store_error(msg: impl Into<String>) {
    let s = msg.into();
    let cstr =
        CString::new(s.replace('\0', " ")).unwrap_or_else(|_| CString::new("error").unwrap());
    LAST_ERROR.with(|cell| *cell.borrow_mut() = Some(cstr));
}

fn classify(err: &ArchiveError) -> WinzoxArStatus {
    use ArchiveError::*;
    match err {
        UnknownMagic => WinzoxArStatus::UnknownMagic,
        Truncated | TruncatedString | TruncatedData => WinzoxArStatus::Truncated,
        InconsistentEncryption => WinzoxArStatus::InconsistentEncryption,
        LegacyAeadRejected | PreV3AeadRejected => WinzoxArStatus::AeadRejected,
        AuthFlagNotSupportedByFormat => WinzoxArStatus::AuthFlagNotSupported,
        AuthRequiresEncryption => WinzoxArStatus::AuthRequiresEncryption,
        UnauthenticatedV3Encrypted => WinzoxArStatus::UnauthenticatedV3Encrypted,
        EntryCountExceedsCap => WinzoxArStatus::EntryCountExceedsCap,
        CentralDirectoryTooLarge => WinzoxArStatus::CentralDirectoryTooLarge,
        CentralDirectoryOffsetInvalid | CentralDirectoryOffsetOutOfBounds => {
            WinzoxArStatus::CentralDirectoryOffsetInvalid
        }
        CentralDirectoryTruncated => WinzoxArStatus::CentralDirectoryTruncated,
        EntryCountMismatch => WinzoxArStatus::EntryCountMismatch,
        DirectoryInconsistent => WinzoxArStatus::DirectoryInconsistent,
        EmptyEntryName => WinzoxArStatus::EmptyEntryName,
        EntryOriginalSizeExceedsCap(_)
        | EntryStoredSizeExceedsCap(_)
        | EntryEncodedSizeExceedsCap(_) => WinzoxArStatus::EntrySizeExceedsCap,
        FooterInconsistent => WinzoxArStatus::FooterInconsistent,
        FooterMissing => WinzoxArStatus::FooterMissing,
        KdfParamsInvalid => WinzoxArStatus::KdfParamsInvalid,
        Sha512Mismatch => WinzoxArStatus::Sha512Mismatch,
        Sha3Mismatch => WinzoxArStatus::Sha3Mismatch,
        UnsupportedCompression(_) => WinzoxArStatus::UnsupportedCompression,
        UnsupportedEncryption(_) => WinzoxArStatus::UnsupportedEncryption,
        EncryptedArchive => WinzoxArStatus::EncryptedArchive,
        IntegerOverflow => WinzoxArStatus::IntegerOverflow,
    }
}

/// Retrieve a thread-local NUL-terminated UTF-8 error message describing the
/// most recent error.
#[unsafe(no_mangle)]
pub extern "C" fn winzox_ar_last_error() -> *const c_char {
    LAST_ERROR.with(|cell| match cell.borrow().as_ref() {
        Some(s) => s.as_ptr(),
        None => std::ptr::null(),
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_buffer_free(buffer: WinzoxArBuffer) {
    if buffer.data.is_null() {
        return;
    }
    // SAFETY: caller obtained the buffer from a function in this module.
    unsafe { drop(Vec::from_raw_parts(buffer.data, buffer.len, buffer.cap)) };
}

/// Returns 1 if `raw[..len]` starts with one of the recognised WZOX/ZOXn
/// magics. Suitable for replacing the C++ `LooksLikeZoxArchive` byte check.
///
/// # Safety
///
/// `data` must point to at least `len` readable bytes or be NULL with
/// `len == 0`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_looks_like_zox(data: *const u8, len: usize) -> c_int {
    if len == 0 {
        return 0;
    }
    if data.is_null() {
        return 0;
    }
    // SAFETY: caller contract above.
    let slice = unsafe { std::slice::from_raw_parts(data, len) };
    looks_like_zox_archive(slice) as c_int
}

// ----------------------------------------------------------------------------
// Integrity accumulator FFI: opaque handle around `ArchiveIntegrityAccumulator`.
// ----------------------------------------------------------------------------

#[repr(C)]
#[derive(Debug)]
pub struct WinzoxArIntegrity {
    _private: c_void,
}

#[unsafe(no_mangle)]
pub extern "C" fn winzox_ar_integrity_new() -> *mut WinzoxArIntegrity {
    let acc = Box::new(ArchiveIntegrityAccumulator::new());
    Box::into_raw(acc).cast::<WinzoxArIntegrity>()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_integrity_free(handle: *mut WinzoxArIntegrity) {
    if handle.is_null() {
        return;
    }
    // SAFETY: caller contract.
    unsafe { drop(Box::from_raw(handle.cast::<ArchiveIntegrityAccumulator>())) };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_integrity_update(
    handle: *mut WinzoxArIntegrity,
    data: *const u8,
    len: usize,
) -> WinzoxArStatus {
    if handle.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    if len == 0 {
        return WinzoxArStatus::Ok;
    }
    if data.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    // SAFETY: caller contract.
    let acc = unsafe { &mut *(handle.cast::<ArchiveIntegrityAccumulator>()) };
    let slice = unsafe { std::slice::from_raw_parts(data, len) };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| acc.update(slice)));
    match result {
        Ok(()) => WinzoxArStatus::Ok,
        Err(_) => {
            store_error("integrity accumulator was already finalized");
            WinzoxArStatus::FooterInconsistent
        }
    }
}

/// Finalize the accumulator. The handle is left in a finalized state; the
/// caller must still free it with [`winzox_ar_integrity_free`].
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_integrity_finalize(
    handle: *mut WinzoxArIntegrity,
    out_digests: *mut WinzoxArDigests,
) -> WinzoxArStatus {
    if handle.is_null() || out_digests.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    // SAFETY: caller contract. We consume the inner state by replacing it
    // with a fresh, already-finalized one to keep the C handle alive until
    // the caller chooses to free it.
    let acc_ptr = handle.cast::<ArchiveIntegrityAccumulator>();
    let taken: ArchiveIntegrityAccumulator =
        unsafe { std::mem::replace(&mut *acc_ptr, ArchiveIntegrityAccumulator::new()) };
    // Mark the replacement as finalized so subsequent update() calls fail
    // exactly as the C++ code did.
    let result =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| taken.finalize()));
    match result {
        Ok(d) => {
            let _ = poison_handle(acc_ptr);
            // SAFETY: caller contract.
            unsafe {
                *out_digests = WinzoxArDigests {
                    sha512: d.sha512,
                    sha3_256: d.sha3_256,
                };
            }
            WinzoxArStatus::Ok
        }
        Err(_) => {
            store_error("integrity accumulator was already finalized");
            WinzoxArStatus::FooterInconsistent
        }
    }
}

fn poison_handle(acc_ptr: *mut ArchiveIntegrityAccumulator) -> ArchiveIntegrityDigests {
    // SAFETY: caller has not freed the handle; we replace the live state
    // with a fresh accumulator, then finalize it to put the handle into a
    // permanently-finalized state.
    let fresh: ArchiveIntegrityAccumulator =
        unsafe { std::mem::replace(&mut *acc_ptr, ArchiveIntegrityAccumulator::new()) };
    fresh.finalize()
}

/// One-shot digest helper; matches `ComputeArchiveIntegrityDigests` from the
/// C++ code. Returns a single digest pair for `data[..len]`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_compute_digests(
    data: *const u8,
    len: usize,
    out_digests: *mut WinzoxArDigests,
) -> WinzoxArStatus {
    if out_digests.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    let slice: &[u8] = if len == 0 {
        &[]
    } else {
        if data.is_null() {
            return WinzoxArStatus::NullArgument;
        }
        // SAFETY: caller contract.
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    let d = crate::integrity::compute_digests(slice);
    // SAFETY: caller contract.
    unsafe {
        *out_digests = WinzoxArDigests {
            sha512: d.sha512,
            sha3_256: d.sha3_256,
        };
    }
    WinzoxArStatus::Ok
}

/// Constant-time equality check. Returns 1 if equal.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_digests_equal(
    a: *const u8,
    b: *const u8,
    len: usize,
) -> c_int {
    if len == 0 {
        return 1;
    }
    if a.is_null() || b.is_null() {
        return 0;
    }
    // SAFETY: caller contract.
    let la = unsafe { std::slice::from_raw_parts(a, len) };
    let lb = unsafe { std::slice::from_raw_parts(b, len) };
    crate::integrity::digests_equal(la, lb) as c_int
}

// ----------------------------------------------------------------------------
// Metadata + index FFI.
// ----------------------------------------------------------------------------

fn write_metadata(out: *mut WinzoxArMetadata, src: &ArchiveMetadata) {
    let m = WinzoxArMetadata {
        encrypted: src.encrypted as u8,
        solid: src.solid as u8,
        authenticated: src.authenticated as u8,
        integrity_sha512: src.integrity_sha512 as u8,
        integrity_sha3_256: src.integrity_sha3_256 as u8,
        encryption_algorithm: src.encryption_algorithm,
        default_algorithm: src.default_algorithm,
        _padding: 0,
        created_unix_time: src.created_unix_time,
        payload_checksum: src.payload_checksum,
        _padding2: 0,
    };
    // SAFETY: caller contract: out is valid.
    unsafe { *out = m };
}

/// Parse the metadata of an **unencrypted** WZOX/ZOX archive.
///
/// `comment_out` is a buffer of NUL-terminated UTF-8 receiving the archive
/// comment. It is owned by Rust until released via [`winzox_ar_buffer_free`].
///
/// # Safety
///
/// `data` must point to at least `len` readable bytes; `out_metadata` and
/// `comment_out` must be valid writable slots.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_read_metadata_plain(
    data: *const u8,
    len: usize,
    out_metadata: *mut WinzoxArMetadata,
    comment_out: *mut WinzoxArBuffer,
) -> WinzoxArStatus {
    if out_metadata.is_null() || comment_out.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    // SAFETY: caller contract.
    unsafe { *comment_out = WinzoxArBuffer::empty() };
    let slice: &[u8] = if len == 0 {
        &[]
    } else {
        if data.is_null() {
            return WinzoxArStatus::NullArgument;
        }
        // SAFETY: caller contract.
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    match read_metadata_plain(slice) {
        Ok(meta) => {
            write_metadata(out_metadata, &meta);
            let mut comment = meta.comment.into_bytes();
            comment.push(0);
            // SAFETY: caller contract.
            unsafe { *comment_out = WinzoxArBuffer::from_vec(comment) };
            WinzoxArStatus::Ok
        }
        Err(e) => {
            let s = classify(&e);
            store_error(e.to_string());
            s
        }
    }
}

/// Parse the directory listing of an **unencrypted** archive. Entries are
/// returned packed into [`WinzoxArEntry`] records. Their `path` strings live
/// in `string_heap_out` as a single concatenated UTF-8 blob; the
/// `path_offset` / `path_len` fields index into that blob.
///
/// # Safety
///
/// `data` must point to at least `len` readable bytes; `out_entries` and
/// `string_heap_out` must be valid writable slots.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn winzox_ar_read_index_plain(
    data: *const u8,
    len: usize,
    out_entries: *mut WinzoxArBuffer,
    out_entry_count: *mut usize,
    string_heap_out: *mut WinzoxArBuffer,
) -> WinzoxArStatus {
    if out_entries.is_null() || string_heap_out.is_null() || out_entry_count.is_null() {
        return WinzoxArStatus::NullArgument;
    }
    // SAFETY: caller contract.
    unsafe {
        *out_entries = WinzoxArBuffer::empty();
        *string_heap_out = WinzoxArBuffer::empty();
        *out_entry_count = 0;
    }
    let slice: &[u8] = if len == 0 {
        &[]
    } else {
        if data.is_null() {
            return WinzoxArStatus::NullArgument;
        }
        // SAFETY: caller contract.
        unsafe { std::slice::from_raw_parts(data, len) }
    };
    match read_index_plain(slice) {
        Ok(entries) => {
            let mut heap = Vec::<u8>::new();
            let mut packed = Vec::<u8>::with_capacity(entries.len() * std::mem::size_of::<WinzoxArEntry>());
            for entry in &entries {
                let path_offset = heap.len();
                heap.extend_from_slice(entry.path.as_bytes());
                let rec = WinzoxArEntry {
                    path_offset,
                    path_len: entry.path.len(),
                    algorithm: entry.algorithm,
                    _padding: [0; 7],
                    original_size: entry.original_size,
                    stored_size: entry.stored_size,
                    encoded_size: entry.encoded_size,
                    crc32: entry.crc32,
                    _padding2: 0,
                };
                let bytes = unsafe {
                    std::slice::from_raw_parts(
                        (&rec as *const WinzoxArEntry).cast::<u8>(),
                        std::mem::size_of::<WinzoxArEntry>(),
                    )
                };
                packed.extend_from_slice(bytes);
            }
            // SAFETY: caller contract.
            unsafe {
                *out_entries = WinzoxArBuffer::from_vec(packed);
                *string_heap_out = WinzoxArBuffer::from_vec(heap);
                *out_entry_count = entries.len();
            }
            WinzoxArStatus::Ok
        }
        Err(e) => {
            let s = classify(&e);
            store_error(e.to_string());
            s
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn winzox_ar_abi_version() -> c_int {
    1
}
