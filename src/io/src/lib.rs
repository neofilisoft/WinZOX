//! Rust 1.95 reimplementation of the WinZOX I/O primitives.
//!
//! This crate ports four C++ entry points from `WinZOX/src/io/` to Rust:
//!
//! | C++ | Rust |
//! |---|---|
//! | `winzox::io::CollectInputFiles` | [`collect_input_files`] |
//! | `winzox::io::ReadFileBytes` | [`read_file_bytes`] |
//! | `winzox::io::WriteFileBytes` | [`write_file_bytes`] |
//! | `winzox::io::ReadAllVolumes` | [`read_all_volumes`] |
//! | `winzox::io::VolumeWriter` | [`VolumeWriter`] |
//!
//! The Rust version adds three classes of hardening on top of the C++ source:
//!
//! * **Per-file cap (`MAX_FILE_SIZE`, 64 GiB).** `read_file_bytes` rejects files
//!   larger than this, so a quadrillion-byte file declared by a hostile volume
//!   header cannot trigger a multi-terabyte allocation in the C++ caller.
//! * **Total accumulated cap (`MAX_TOTAL_VOLUME_SIZE`, 1 TiB).** `read_all_volumes`
//!   refuses to keep appending split volumes (`.z01`, `.z02`, …) past this
//!   total. This fixes the unbounded loop in `volume_reader.cpp`.
//! * **Symlink-component check on writes.** `write_file_bytes` refuses to
//!   create any directory whose name resolves to a symlink, so a hostile
//!   archive cannot use `..` plus a pre-existing symlink in a sibling directory
//!   to escape the destination root.
//!
//! ## C ABI
//!
//! The public C ABI lives in [`ffi`] and is what the C++ `src/io/*.cpp`
//! files call when WinZOX is built with `-DWINZOX_ENABLE_RUST_COMPONENTS=ON`.
//! See `include/winzox_io_rs.h` for the C header.

pub mod ffi;

use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

/// Maximum size of a single regular file `read_file_bytes` will load into a buffer.
///
/// Matches the per-entry cap in `src/extraction/extractor.cpp`.
pub const MAX_FILE_SIZE: u64 = 64 << 30;

/// Maximum total number of bytes `read_all_volumes` will accumulate across
/// the head archive plus every `.zNN` continuation. Matches
/// `kMaxArchiveOriginalSize` in `extractor.cpp` (1 TiB).
pub const MAX_TOTAL_VOLUME_SIZE: u64 = 1024 << 30;

/// Hard ceiling on the number of split volumes we will scan for. The split
/// extension only has two digits in `.zNN`, so there are at most 99 possible
/// continuations regardless. We keep an explicit cap so a corrupted on-disk
/// numbering scheme can't accidentally cause us to loop forever.
pub const MAX_VOLUME_COUNT: u32 = 99;

/// Errors produced by the Rust IO layer. Each variant has a dedicated
/// numeric value exposed through [`ffi::WinzoxIoStatus`] so the C++ side can
/// switch on the cause without parsing English strings.
#[derive(Debug, thiserror::Error)]
pub enum IoError {
    #[error("path does not exist: {0}")]
    NotFound(PathBuf),
    #[error("refusing to follow symbolic link: {0}")]
    SymlinkRefused(PathBuf),
    #[error("path is neither a file nor a directory: {0}")]
    UnsupportedKind(PathBuf),
    #[error("file exceeds the {} byte size cap: {1}", MAX_FILE_SIZE)]
    FileTooLarge(PathBuf, u64),
    #[error(
        "total volume size exceeds the {} byte cap (would read {1} bytes)",
        MAX_TOTAL_VOLUME_SIZE
    )]
    VolumeChainTooLarge(PathBuf, u64),
    #[error("path is not valid UTF-8")]
    InvalidUtf8,
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
}

/// Result alias used throughout the safe Rust API.
pub type Result<T> = std::result::Result<T, IoError>;

fn symlink_metadata_no_follow(path: &Path) -> Result<fs::Metadata> {
    fs::symlink_metadata(path).map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            IoError::NotFound(path.to_path_buf())
        } else {
            IoError::Io(e)
        }
    })
}

fn is_symlink(path: &Path) -> bool {
    fs::symlink_metadata(path).is_ok_and(|m| m.is_symlink())
}

/// Recursively collect every regular file under `input_path`.
///
/// Symbolic links are never followed: if `input_path` itself is a symlink we
/// refuse, and symlinks discovered during traversal are skipped (with a
/// warning on stderr to match the C++ behaviour). The returned list is sorted
/// in lexicographic order so archives produced by the Rust path are
/// byte-identical to those produced by the C++ path.
pub fn collect_input_files(input_path: &Path) -> Result<Vec<PathBuf>> {
    let meta = symlink_metadata_no_follow(input_path)?;

    if meta.is_symlink() {
        return Err(IoError::SymlinkRefused(input_path.to_path_buf()));
    }

    let mut files = Vec::new();
    if meta.is_file() {
        files.push(input_path.to_path_buf());
    } else if meta.is_dir() {
        walk_collect(input_path, &mut files)?;
    } else {
        return Err(IoError::UnsupportedKind(input_path.to_path_buf()));
    }

    files.sort();
    Ok(files)
}

fn walk_collect(dir: &Path, files: &mut Vec<PathBuf>) -> Result<()> {
    let mut stack = vec![dir.to_path_buf()];
    while let Some(current) = stack.pop() {
        let entries = match fs::read_dir(&current) {
            Ok(rd) => rd,
            Err(e) => {
                if e.kind() == std::io::ErrorKind::PermissionDenied {
                    eprintln!("[warn] skipping path due to permission denied: {}", current.display());
                    continue;
                }
                return Err(IoError::Io(e));
            }
        };
        for entry in entries {
            let entry = entry?;
            let path = entry.path();
            let kind = entry.file_type()?;
            if kind.is_symlink() {
                eprintln!("[warn] skipping symbolic link: {}", path.display());
                continue;
            }
            if kind.is_dir() {
                stack.push(path);
            } else if kind.is_file() {
                files.push(path);
            }
        }
    }
    Ok(())
}

/// Read the entire contents of `path` into a `Vec<u8>`.
///
/// * Refuses to read through a symlink.
/// * Refuses files larger than [`MAX_FILE_SIZE`].
pub fn read_file_bytes(path: &Path) -> Result<Vec<u8>> {
    if is_symlink(path) {
        return Err(IoError::SymlinkRefused(path.to_path_buf()));
    }
    let meta = symlink_metadata_no_follow(path)?;
    if !meta.is_file() {
        return Err(IoError::UnsupportedKind(path.to_path_buf()));
    }
    let size = meta.len();
    if size > MAX_FILE_SIZE {
        return Err(IoError::FileTooLarge(path.to_path_buf(), size));
    }

    let mut file = File::open(path)?;
    let mut buffer = Vec::with_capacity(size as usize);
    file.read_to_end(&mut buffer)?;
    Ok(buffer)
}

/// Write `data` to `path`, creating intermediate directories as needed.
///
/// Hardened over the C++ original:
///
/// * Every component of `path.parent()` is walked top-down, and we refuse to
///   create or traverse a component whose name resolves to a symlink. This
///   defends in depth against a hostile archive entry that names a symlink
///   sitting alongside the destination root.
/// * If `path` itself already exists as a symlink we refuse to write to it,
///   so existing on-disk symlinks can't be used as a redirect target.
pub fn write_file_bytes(path: &Path, data: &[u8]) -> Result<()> {
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            create_dir_no_symlinks(parent)?;
        }
    }

    if is_symlink(path) {
        return Err(IoError::SymlinkRefused(path.to_path_buf()));
    }

    let mut file = File::create(path)?;
    if !data.is_empty() {
        file.write_all(data)?;
    }
    Ok(())
}

fn create_dir_no_symlinks(path: &Path) -> Result<()> {
    let mut prefix = PathBuf::new();
    let mut first = true;
    for component in path.components() {
        if first {
            // Preserve absolute roots / drive letters exactly as `Path` exposes them.
            prefix.push(component);
            first = false;
        } else {
            prefix.push(component);
        }
        if !prefix.as_os_str().is_empty() && prefix.exists() {
            let meta = symlink_metadata_no_follow(&prefix)?;
            if meta.is_symlink() {
                return Err(IoError::SymlinkRefused(prefix));
            }
            if !meta.is_dir() {
                return Err(IoError::UnsupportedKind(prefix));
            }
        } else if !prefix.as_os_str().is_empty() {
            fs::create_dir(&prefix)?;
        }
    }
    Ok(())
}

/// Compute the on-disk path of the head volume given any archive path.
///
/// `.zox` → returned unchanged. `.zNN` continuations → the matching `.zox`
/// when it exists on disk, otherwise the supplied path is returned as-is.
pub fn resolve_first_volume(archive_path: &Path) -> PathBuf {
    let ext = archive_path
        .extension()
        .and_then(|s| s.to_str())
        .map(str::to_ascii_lowercase)
        .unwrap_or_default();

    if ext == "zox" {
        return archive_path.to_path_buf();
    }

    if is_split_zox_extension(&ext) {
        let mut first = archive_path.to_path_buf();
        first.set_extension("zox");
        if first.exists() {
            return first;
        }
    }
    archive_path.to_path_buf()
}

fn is_split_zox_extension(ext: &str) -> bool {
    let bytes = ext.as_bytes();
    bytes.len() >= 2
        && bytes[0] == b'z'
        && bytes[1..].iter().all(|b| b.is_ascii_digit())
}

/// Read the head volume plus every `.zNN` continuation that exists on disk.
///
/// Hardened over the C++ original: aborts as soon as the running total
/// exceeds [`MAX_TOTAL_VOLUME_SIZE`] or [`MAX_VOLUME_COUNT`] continuations
/// have been concatenated.
pub fn read_all_volumes(archive_path: &Path) -> Result<Vec<u8>> {
    let first = resolve_first_volume(archive_path);
    let mut data = read_file_bytes(&first)?;

    let first_ext = first
        .extension()
        .and_then(|s| s.to_str())
        .map(str::to_ascii_lowercase)
        .unwrap_or_default();
    if first_ext != "zox" {
        return Ok(data);
    }

    let mut total: u64 = data.len() as u64;
    for index in 1..=MAX_VOLUME_COUNT {
        let next = first.with_extension(format!("z{index:02}"));
        if !next.exists() {
            break;
        }
        let chunk = read_file_bytes(&next)?;
        let new_total = total.saturating_add(chunk.len() as u64);
        if new_total > MAX_TOTAL_VOLUME_SIZE {
            return Err(IoError::VolumeChainTooLarge(next, new_total));
        }
        total = new_total;
        data.extend_from_slice(&chunk);
    }

    Ok(data)
}

/// Writer for split volumes. When `split_size` is zero, behaves like a single
/// regular file (`<base>.zox`); otherwise writes `<base>.zox` first, then
/// rolls over to `<base>.zNN` as the running byte count crosses `split_size`.
#[derive(Debug)]
pub struct VolumeWriter {
    base_path: PathBuf,
    split_size: u64,
    current_size: u64,
    volume_index: u32,
    output: Option<File>,
}

impl VolumeWriter {
    pub fn new(base_path: impl AsRef<Path>, split_size: u64) -> Result<Self> {
        let mut writer = Self {
            base_path: base_path.as_ref().to_path_buf(),
            split_size,
            current_size: 0,
            volume_index: 0,
            output: None,
        };
        writer.open_next_volume()?;
        Ok(writer)
    }

    fn current_name(&self) -> PathBuf {
        let mut name = self.base_path.as_os_str().to_owned();
        if self.volume_index == 0 {
            name.push(".zox");
        } else {
            name.push(format!(".z{:02}", self.volume_index));
        }
        PathBuf::from(name)
    }

    fn open_next_volume(&mut self) -> Result<()> {
        if let Some(mut f) = self.output.take() {
            f.flush()?;
        }
        let name = self.current_name();
        let file = File::create(&name)?;
        self.output = Some(file);
        self.current_size = 0;
        self.volume_index += 1;
        Ok(())
    }

    pub fn write(&mut self, mut data: &[u8]) -> Result<()> {
        while !data.is_empty() {
            if self.split_size > 0 && self.current_size + (data.len() as u64) > self.split_size {
                let space_left =
                    usize::try_from(self.split_size - self.current_size).unwrap_or(usize::MAX);
                if space_left > 0 {
                    let head = &data[..space_left];
                    self.write_chunk(head)?;
                    data = &data[space_left..];
                }
                self.open_next_volume()?;
            } else {
                self.write_chunk(data)?;
                return Ok(());
            }
        }
        Ok(())
    }

    fn write_chunk(&mut self, chunk: &[u8]) -> Result<()> {
        let f = self.output.as_mut().ok_or_else(|| {
            IoError::Io(std::io::Error::other("VolumeWriter has no open output file"))
        })?;
        f.write_all(chunk)?;
        self.current_size += chunk.len() as u64;
        Ok(())
    }

    pub fn close(&mut self) -> Result<()> {
        if let Some(mut f) = self.output.take() {
            f.flush()?;
        }
        Ok(())
    }
}

impl Drop for VolumeWriter {
    fn drop(&mut self) {
        let _ = self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn read_write_roundtrip() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("hello.bin");
        let payload = b"hello world".to_vec();
        write_file_bytes(&path, &payload).unwrap();
        let read = read_file_bytes(&path).unwrap();
        assert_eq!(read, payload);
    }

    #[test]
    fn read_rejects_missing() {
        let err = read_file_bytes(Path::new("/definitely/not/here/zxyzzy")).unwrap_err();
        assert!(matches!(err, IoError::NotFound(_)));
    }

    #[test]
    fn collect_files_lists_lexicographically() {
        let dir = TempDir::new().unwrap();
        let a = dir.path().join("a.txt");
        let b = dir.path().join("b.txt");
        std::fs::write(&b, "B").unwrap();
        std::fs::write(&a, "A").unwrap();
        let files = collect_input_files(dir.path()).unwrap();
        assert_eq!(files, vec![a, b]);
    }

    #[test]
    fn write_refuses_symlink_parent() {
        #[cfg(unix)]
        {
            let dir = TempDir::new().unwrap();
            let real = dir.path().join("real");
            std::fs::create_dir(&real).unwrap();
            let link = dir.path().join("link");
            std::os::unix::fs::symlink(&real, &link).unwrap();
            let target = link.join("foo.txt");
            let err = write_file_bytes(&target, b"x").unwrap_err();
            assert!(matches!(err, IoError::SymlinkRefused(_)));
        }
    }

    #[test]
    fn volume_writer_splits() {
        let dir = TempDir::new().unwrap();
        let base = dir.path().join("vw");
        {
            let mut w = VolumeWriter::new(&base, 4).unwrap();
            w.write(b"0123").unwrap();
            w.write(b"4567").unwrap();
            w.write(b"89").unwrap();
            w.close().unwrap();
        }
        let head = std::fs::read(base.with_extension("zox")).unwrap();
        let v01 = std::fs::read(format!("{}.z01", base.display())).unwrap();
        let v02 = std::fs::read(format!("{}.z02", base.display())).unwrap();
        assert_eq!(&head, b"0123");
        assert_eq!(&v01, b"4567");
        assert_eq!(&v02, b"89");
    }

    #[test]
    fn read_all_volumes_concatenates() {
        let dir = TempDir::new().unwrap();
        let base = dir.path().join("vw");
        {
            let mut w = VolumeWriter::new(&base, 5).unwrap();
            w.write(b"abcdefghij").unwrap();
            w.close().unwrap();
        }
        let all = read_all_volumes(&base.with_extension("zox")).unwrap();
        assert_eq!(&all, b"abcdefghij");
    }
}
