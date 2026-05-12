//! Rust 1.95 reimplementation of the **read-side core** of `WinZOX/src/archive`.
//!
//! Scope (Option A from the upgrade plan):
//!
//! * Magic detection (`looks_like_zox_archive`)
//! * Archive integrity accumulator (SHA-512 + SHA3-256) — formerly OpenSSL
//! * Header parser (`parse_header`) — for `WZOX` / `ZOX6` / `ZOX5` / `ZOX4`
//! * Footer parser (`parse_footer`) and digest verification
//! * Plain (unencrypted) directory index decoder
//!
//! Writer-side functions and decompression remain in the C++ project — those
//! flows live in `archive.cpp` and are unaffected by this crate. The C ABI
//! exposed in [`ffi`] is what the C++ project calls when WinZOX is built with
//! `-DWINZOX_ENABLE_RUST_COMPONENTS=ON`.
//!
//! # Wire compatibility
//!
//! All struct layouts and field widths exactly match the on-disk format used
//! by the C++ implementation, so archives produced by either side remain
//! readable by both. Round-trip parity is asserted by the
//! `cpp_parity_roundtrip` test, which constructs an in-memory archive
//! mimicking the C++ writer's byte stream and parses it back.

#![deny(unsafe_op_in_unsafe_fn)]

pub mod ffi;
pub mod integrity;

use std::convert::TryFrom;

use thiserror::Error;

pub const MAGIC_WZOX: &[u8; 4] = b"WZOX";
pub const MAGIC_ZOX6: &[u8; 4] = b"ZOX6";
pub const MAGIC_ZOX5: &[u8; 4] = b"ZOX5";
pub const MAGIC_ZOX4: &[u8; 4] = b"ZOX4";
pub const MAGIC_FOOTER: &[u8; 4] = b"ZCDR";

pub const FLAG_ENCRYPTED: u8 = 0x01;
pub const FLAG_SOLID: u8 = 0x02;
pub const FLAG_AUTHENTICATED: u8 = 0x04;

pub const AUTHENTICATION_TAG_SIZE: usize = 32;
pub const SHA512_DIGEST_SIZE: usize = 64;
pub const SHA3_256_DIGEST_SIZE: usize = 32;

// Archive-bomb caps lifted verbatim from `archive.cpp`.
pub const MAX_ENTRY_ORIGINAL_SIZE: u64 = 64 << 30;
pub const MAX_ENTRY_STORED_SIZE: u64 = 64 << 30;
pub const MAX_ARCHIVE_ORIGINAL_SIZE: u64 = 1024 << 30;
pub const MAX_ENTRY_COUNT: u32 = 10_000_000;
pub const MAX_CENTRAL_DIRECTORY_SIZE: u64 = 256 << 20;
pub const MAX_ENCRYPTED_EXPANSION_MARGIN: u64 = 1 << 20;

/// Compression algorithm IDs. Bit-for-bit identical to the C++ enum in
/// `compression/compressor.hpp`.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompressionAlgorithm {
    Store = 0,
    Deflate = 1,
    Lzma = 2,
    Zstd = 3,
    Bzip2 = 4,
    Lz4 = 5,
}

impl CompressionAlgorithm {
    pub fn from_u8(v: u8) -> Option<Self> {
        match v {
            0 => Some(Self::Store),
            1 => Some(Self::Deflate),
            2 => Some(Self::Lzma),
            3 => Some(Self::Zstd),
            4 => Some(Self::Bzip2),
            5 => Some(Self::Lz4),
            _ => None,
        }
    }
}

/// Encryption algorithm IDs. Bit-for-bit identical to the C++ enum.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EncryptionAlgorithm {
    None = 0,
    Aes256 = 1,
    ChaCha20 = 2,
    Gorgon = 3,
}

impl EncryptionAlgorithm {
    pub fn from_u8(v: u8) -> Option<Self> {
        match v {
            0 => Some(Self::None),
            1 => Some(Self::Aes256),
            2 => Some(Self::ChaCha20),
            3 => Some(Self::Gorgon),
            _ => None,
        }
    }
    pub fn is_aead(self) -> bool {
        matches!(self, Self::ChaCha20 | Self::Gorgon)
    }
}

/// Format flavour detected from the four-byte magic.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArchiveFlavour {
    /// Current `WZOX` v3+ format (extended footer + KDF iteration field).
    Wzox,
    /// Legacy v6 `ZOX6` (extended footer, no iteration field).
    Zox6,
    /// Legacy v5 `ZOX5` (extended footer, no authentication flag).
    Zox5,
    /// Original `ZOX4` (no central directory; single-payload format).
    Zox4Legacy,
}

impl ArchiveFlavour {
    pub fn from_magic(magic: &[u8]) -> Option<Self> {
        if magic.len() < 4 {
            return None;
        }
        match &magic[..4] {
            m if m == MAGIC_WZOX => Some(Self::Wzox),
            m if m == MAGIC_ZOX6 => Some(Self::Zox6),
            m if m == MAGIC_ZOX5 => Some(Self::Zox5),
            m if m == MAGIC_ZOX4 => Some(Self::Zox4Legacy),
            _ => None,
        }
    }
    pub fn is_extended_footer(self) -> bool {
        matches!(self, Self::Wzox | Self::Zox6 | Self::Zox5)
    }
    pub fn has_iteration_field(self) -> bool {
        matches!(self, Self::Wzox)
    }
    pub fn integrity_hashes(self) -> bool {
        // C++ behaviour: only the current `WZOX` flavour carries SHA-512 + SHA3-256
        // digests in the footer.
        matches!(self, Self::Wzox)
    }
}

/// High-level archive metadata extracted from the header + footer.
///
/// Mirrors the C++ `ArchiveMetadata` struct.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ArchiveMetadata {
    pub encrypted: bool,
    pub solid: bool,
    pub authenticated: bool,
    pub integrity_sha512: bool,
    pub integrity_sha3_256: bool,
    pub encryption_algorithm: u8,
    pub default_algorithm: u8,
    pub created_unix_time: u64,
    pub payload_checksum: u32,
    pub comment: String,
}

/// Central-directory entry as exposed to consumers. Mirrors the C++
/// `ArchiveEntryInfo`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ArchiveEntryInfo {
    pub path: String,
    pub algorithm: u8,
    pub original_size: u64,
    pub stored_size: u64,
    pub encoded_size: u64,
    pub crc32: u32,
}

#[derive(Clone, Debug)]
pub struct ParsedHeader {
    pub flavour: ArchiveFlavour,
    pub metadata: ArchiveMetadata,
    pub data_section_plain_size: u64,
    pub data_offset: usize,
    pub iv_primary: Vec<u8>,
    pub iv_secondary: Vec<u8>,
    pub salt: Vec<u8>,
    pub iterations: u32,
}

#[derive(Clone, Debug, Default)]
pub struct ParsedFooter {
    pub central_directory_offset: u64,
    pub central_directory_stored_size: u64,
    pub central_directory_plain_size: u64,
    pub central_directory_checksum: u32,
    pub entry_count: u32,
    pub sha512_digest: Vec<u8>,
    pub sha3_256_digest: Vec<u8>,
    pub authentication_tag: Vec<u8>,
}

#[derive(Debug, Error)]
pub enum ArchiveError {
    #[error("invalid .zox archive header (unknown magic)")]
    UnknownMagic,
    #[error("archive is truncated")]
    Truncated,
    #[error("archive string field is truncated")]
    TruncatedString,
    #[error("archive data field is truncated")]
    TruncatedData,
    #[error("archive declares encryption but has no encryption mode")]
    InconsistentEncryption,
    #[error("legacy archive header cannot carry an AEAD encryption mode")]
    LegacyAeadRejected,
    #[error("pre-v3 archive header cannot carry an AEAD encryption mode")]
    PreV3AeadRejected,
    #[error("archive authentication flag is not supported by this archive format")]
    AuthFlagNotSupportedByFormat,
    #[error("authenticated archives must be encrypted")]
    AuthRequiresEncryption,
    #[error("encrypted WZOX archives must include an authentication tag")]
    UnauthenticatedV3Encrypted,
    #[error("archive declares too many entries")]
    EntryCountExceedsCap,
    #[error("archive central directory exceeds the size cap")]
    CentralDirectoryTooLarge,
    #[error("archive central directory offset is invalid")]
    CentralDirectoryOffsetInvalid,
    #[error("archive central directory offset is outside the file")]
    CentralDirectoryOffsetOutOfBounds,
    #[error("archive central directory is truncated")]
    CentralDirectoryTruncated,
    #[error("archive central directory entry count mismatch")]
    EntryCountMismatch,
    #[error("archive central directory metadata is inconsistent")]
    DirectoryInconsistent,
    #[error("archive contains an empty entry name")]
    EmptyEntryName,
    #[error("archive entry exceeds the per-entry size cap: {0}")]
    EntryOriginalSizeExceedsCap(String),
    #[error("archive entry stored size exceeds cap: {0}")]
    EntryStoredSizeExceedsCap(String),
    #[error("archive entry encoded size exceeds cap: {0}")]
    EntryEncodedSizeExceedsCap(String),
    #[error("archive footer metadata is inconsistent")]
    FooterInconsistent,
    #[error("archive central directory footer is missing")]
    FooterMissing,
    #[error("archive KDF parameters are invalid or below the minimum security threshold")]
    KdfParamsInvalid,
    #[error("archive SHA-512 integrity check failed")]
    Sha512Mismatch,
    #[error("archive SHA3-256 integrity check failed")]
    Sha3Mismatch,
    #[error("unsupported compression algorithm id: {0}")]
    UnsupportedCompression(u8),
    #[error("unsupported encryption algorithm id: {0}")]
    UnsupportedEncryption(u8),
    #[error("plain index parse refused for encrypted archive")]
    EncryptedArchive,
    #[error("integer field is out of range for this platform")]
    IntegerOverflow,
}

pub type Result<T> = std::result::Result<T, ArchiveError>;

/// Quick boolean: does the byte slice start with a recognised WZOX/ZOXn magic?
pub fn looks_like_zox_archive(raw: &[u8]) -> bool {
    ArchiveFlavour::from_magic(raw).is_some()
}

struct Reader<'a> {
    raw: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(raw: &'a [u8]) -> Self {
        Self { raw, pos: 0 }
    }

    fn with_pos(raw: &'a [u8], pos: usize) -> Self {
        Self { raw, pos }
    }

    fn remaining(&self) -> usize {
        self.raw.len() - self.pos
    }

    fn read_u8(&mut self) -> Result<u8> {
        if self.remaining() < 1 {
            return Err(ArchiveError::Truncated);
        }
        let v = self.raw[self.pos];
        self.pos += 1;
        Ok(v)
    }

    fn read_u16(&mut self) -> Result<u16> {
        if self.remaining() < 2 {
            return Err(ArchiveError::Truncated);
        }
        let mut buf = [0u8; 2];
        buf.copy_from_slice(&self.raw[self.pos..self.pos + 2]);
        self.pos += 2;
        Ok(u16::from_le_bytes(buf))
    }

    fn read_u32(&mut self) -> Result<u32> {
        if self.remaining() < 4 {
            return Err(ArchiveError::Truncated);
        }
        let mut buf = [0u8; 4];
        buf.copy_from_slice(&self.raw[self.pos..self.pos + 4]);
        self.pos += 4;
        Ok(u32::from_le_bytes(buf))
    }

    fn read_u64(&mut self) -> Result<u64> {
        if self.remaining() < 8 {
            return Err(ArchiveError::Truncated);
        }
        let mut buf = [0u8; 8];
        buf.copy_from_slice(&self.raw[self.pos..self.pos + 8]);
        self.pos += 8;
        Ok(u64::from_le_bytes(buf))
    }

    fn read_bytes(&mut self, len: usize) -> Result<Vec<u8>> {
        if self.remaining() < len {
            return Err(ArchiveError::TruncatedData);
        }
        let out = self.raw[self.pos..self.pos + len].to_vec();
        self.pos += len;
        Ok(out)
    }

    fn read_string(&mut self, len: usize) -> Result<String> {
        if self.remaining() < len {
            return Err(ArchiveError::TruncatedString);
        }
        let bytes = &self.raw[self.pos..self.pos + len];
        self.pos += len;
        // The C++ side stores comment / path bytes verbatim. We treat them as
        // UTF-8 strings here for ergonomic ownership; if a producer ever
        // emits non-UTF-8 bytes the C ABI returns them as raw bytes via
        // `read_index_entries_raw`.
        String::from_utf8(bytes.to_vec()).map_err(|_| ArchiveError::TruncatedString)
    }
}

fn read_encryption_metadata(
    reader: &mut Reader<'_>,
    algorithm: EncryptionAlgorithm,
    include_iterations: bool,
) -> Result<(Vec<u8>, Vec<u8>, Vec<u8>, u32)> {
    let salt = reader.read_bytes(16)?;
    let (iv_primary, iv_secondary) = if algorithm.is_aead() {
        (Vec::new(), Vec::new())
    } else {
        let iv_primary = reader.read_bytes(16)?;
        let iv_secondary = if matches!(algorithm, EncryptionAlgorithm::Gorgon) {
            reader.read_bytes(16)?
        } else {
            Vec::new()
        };
        (iv_primary, iv_secondary)
    };
    let iterations = if include_iterations {
        reader.read_u32()?
    } else {
        // C++ keeps a `kLegacyKdfIterations` constant for backwards compat.
        // We return zero and let the C++ side substitute its constant.
        0
    };
    Ok((salt, iv_primary, iv_secondary, iterations))
}

/// Parse a `WZOX`/`ZOX6`/`ZOX5` header. Returns the per-archive metadata plus
/// the offset at which the data section begins.
pub fn parse_header(raw: &[u8]) -> Result<ParsedHeader> {
    let flavour = ArchiveFlavour::from_magic(raw).ok_or(ArchiveError::UnknownMagic)?;
    match flavour {
        ArchiveFlavour::Zox4Legacy => {
            // Legacy format is parsed by a different codepath in the C++
            // project (`ParseLegacyHeader`); we expose it via the dedicated
            // function below to keep this path focused on the v3+ layout.
            parse_legacy_header(raw)
        }
        _ => parse_current_header(raw, flavour),
    }
}

fn parse_current_header(raw: &[u8], flavour: ArchiveFlavour) -> Result<ParsedHeader> {
    let mut reader = Reader::with_pos(raw, 4);
    let mut metadata = ArchiveMetadata::default();
    let flags = reader.read_u8()?;
    metadata.encrypted = flags & FLAG_ENCRYPTED != 0;
    metadata.solid = flags & FLAG_SOLID != 0;
    metadata.authenticated = flags & FLAG_AUTHENTICATED != 0;
    metadata.integrity_sha512 = flavour.integrity_hashes();
    metadata.integrity_sha3_256 = flavour.integrity_hashes();

    if metadata.authenticated && matches!(flavour, ArchiveFlavour::Zox5) {
        return Err(ArchiveError::AuthFlagNotSupportedByFormat);
    }
    if metadata.authenticated && !metadata.encrypted {
        return Err(ArchiveError::AuthRequiresEncryption);
    }
    if matches!(flavour, ArchiveFlavour::Wzox) && metadata.encrypted && !metadata.authenticated {
        return Err(ArchiveError::UnauthenticatedV3Encrypted);
    }

    let encryption_byte = reader.read_u8()?;
    let encryption_algo = EncryptionAlgorithm::from_u8(encryption_byte)
        .ok_or(ArchiveError::UnsupportedEncryption(encryption_byte))?;
    if metadata.encrypted && matches!(encryption_algo, EncryptionAlgorithm::None) {
        return Err(ArchiveError::InconsistentEncryption);
    }
    if metadata.encrypted && encryption_algo.is_aead() && !matches!(flavour, ArchiveFlavour::Wzox) {
        return Err(ArchiveError::PreV3AeadRejected);
    }
    metadata.encryption_algorithm = if metadata.encrypted {
        encryption_byte
    } else {
        EncryptionAlgorithm::None as u8
    };

    let default_algo_byte = reader.read_u8()?;
    metadata.default_algorithm = default_algo_byte;

    metadata.created_unix_time = reader.read_u64()?;
    metadata.payload_checksum = reader.read_u32()?;

    let comment_length = reader.read_u32()? as usize;
    metadata.comment = reader.read_string(comment_length)?;

    let (salt, iv_primary, iv_secondary, iterations) = if metadata.encrypted {
        read_encryption_metadata(&mut reader, encryption_algo, flavour.has_iteration_field())?
    } else {
        (Vec::new(), Vec::new(), Vec::new(), 0)
    };

    let data_section_plain_size = if metadata.solid {
        reader.read_u64()?
    } else {
        0
    };

    Ok(ParsedHeader {
        flavour,
        metadata,
        data_section_plain_size,
        data_offset: reader.pos,
        iv_primary,
        iv_secondary,
        salt,
        iterations,
    })
}

/// Parse the `ZOX4` legacy header. Used only for the metadata view; the
/// payload is decompressed by the C++ side.
fn parse_legacy_header(raw: &[u8]) -> Result<ParsedHeader> {
    let mut reader = Reader::with_pos(raw, 4);
    let mut metadata = ArchiveMetadata::default();
    let flags = reader.read_u8()?;
    metadata.encrypted = flags & FLAG_ENCRYPTED != 0;
    metadata.solid = flags & FLAG_SOLID != 0;
    metadata.integrity_sha512 = false;
    metadata.integrity_sha3_256 = false;

    let encryption_byte = reader.read_u8()?;
    let encryption_algo = EncryptionAlgorithm::from_u8(encryption_byte)
        .ok_or(ArchiveError::UnsupportedEncryption(encryption_byte))?;
    if metadata.encrypted && matches!(encryption_algo, EncryptionAlgorithm::None) {
        return Err(ArchiveError::InconsistentEncryption);
    }
    if metadata.encrypted && encryption_algo.is_aead() {
        return Err(ArchiveError::LegacyAeadRejected);
    }
    metadata.encryption_algorithm = if metadata.encrypted {
        encryption_byte
    } else {
        EncryptionAlgorithm::None as u8
    };

    metadata.default_algorithm = reader.read_u8()?;
    metadata.created_unix_time = reader.read_u64()?;
    let plain_payload_size = reader.read_u64()?;
    metadata.payload_checksum = reader.read_u32()?;

    let comment_length = reader.read_u32()? as usize;
    metadata.comment = reader.read_string(comment_length)?;

    let (salt, iv_primary, iv_secondary, _iterations) = if metadata.encrypted {
        read_encryption_metadata(&mut reader, encryption_algo, false)?
    } else {
        (Vec::new(), Vec::new(), Vec::new(), 0)
    };

    // Legacy header is followed by `payload_size` u64 then the payload bytes.
    Ok(ParsedHeader {
        flavour: ArchiveFlavour::Zox4Legacy,
        metadata,
        data_section_plain_size: plain_payload_size,
        data_offset: reader.pos,
        iv_primary,
        iv_secondary,
        salt,
        iterations: 0,
    })
}

/// Parse the trailing footer that follows the central directory.
pub fn parse_footer(raw: &[u8], header: &ParsedHeader) -> Result<ParsedFooter> {
    let integrity_bytes = if header.metadata.integrity_sha512 {
        SHA512_DIGEST_SIZE
    } else {
        0
    } + if header.metadata.integrity_sha3_256 {
        SHA3_256_DIGEST_SIZE
    } else {
        0
    };
    let auth_bytes = if header.metadata.authenticated {
        AUTHENTICATION_TAG_SIZE
    } else {
        0
    };
    let footer_size = 4 + 8 * 3 + 4 * 2 + integrity_bytes + auth_bytes;
    if raw.len() < footer_size {
        return Err(ArchiveError::Truncated);
    }

    let footer_start = raw.len() - footer_size;
    if &raw[footer_start..footer_start + 4] != MAGIC_FOOTER {
        return Err(ArchiveError::FooterMissing);
    }
    let mut reader = Reader::with_pos(raw, footer_start + 4);
    let mut footer = ParsedFooter::default();
    footer.central_directory_offset = reader.read_u64()?;
    footer.central_directory_stored_size = reader.read_u64()?;
    footer.central_directory_plain_size = reader.read_u64()?;
    footer.central_directory_checksum = reader.read_u32()?;
    footer.entry_count = reader.read_u32()?;
    if header.metadata.integrity_sha512 {
        footer.sha512_digest = reader.read_bytes(SHA512_DIGEST_SIZE)?;
    }
    if header.metadata.integrity_sha3_256 {
        footer.sha3_256_digest = reader.read_bytes(SHA3_256_DIGEST_SIZE)?;
    }
    if header.metadata.authenticated {
        footer.authentication_tag = reader.read_bytes(AUTHENTICATION_TAG_SIZE)?;
    }
    if reader.pos != raw.len() {
        return Err(ArchiveError::FooterInconsistent);
    }
    Ok(footer)
}

/// Verify the SHA-512 and SHA3-256 digests recorded in the footer.
///
/// Returns Ok if either no integrity hashes are claimed or both claimed
/// hashes match the recomputed values.
pub fn verify_integrity(raw: &[u8], header: &ParsedHeader, footer: &ParsedFooter) -> Result<()> {
    if !header.metadata.integrity_sha512 && !header.metadata.integrity_sha3_256 {
        return Ok(());
    }
    let digest_bytes = if header.metadata.integrity_sha512 {
        footer.sha512_digest.len()
    } else {
        0
    } + if header.metadata.integrity_sha3_256 {
        footer.sha3_256_digest.len()
    } else {
        0
    };
    let auth_bytes = if header.metadata.authenticated {
        footer.authentication_tag.len()
    } else {
        0
    };
    if raw.len() < digest_bytes + auth_bytes {
        return Err(ArchiveError::Truncated);
    }
    let digested_size = raw.len() - digest_bytes - auth_bytes;
    let computed = integrity::compute_digests(&raw[..digested_size]);

    if header.metadata.integrity_sha512
        && (footer.sha512_digest.len() != SHA512_DIGEST_SIZE
            || !integrity::digests_equal(&footer.sha512_digest, &computed.sha512))
    {
        return Err(ArchiveError::Sha512Mismatch);
    }
    if header.metadata.integrity_sha3_256
        && (footer.sha3_256_digest.len() != SHA3_256_DIGEST_SIZE
            || !integrity::digests_equal(&footer.sha3_256_digest, &computed.sha3_256))
    {
        return Err(ArchiveError::Sha3Mismatch);
    }
    Ok(())
}

/// Decode an in-memory central directory blob into entries. The blob must
/// have already been decrypted by the caller (or is plain to begin with).
///
/// The C++ reader tries two layout variants — with and without the
/// `encoded_size` field — to remain compatible with archives written by
/// pre-v3 builds. This function mirrors that behaviour.
pub fn parse_directory_entries(
    plain_directory: &[u8],
    expected_count: u32,
) -> Result<Vec<ArchiveEntryInfo>> {
    match try_parse_directory(plain_directory, expected_count, true) {
        Ok(entries) => Ok(entries),
        Err(_) => try_parse_directory(plain_directory, expected_count, false),
    }
}

fn try_parse_directory(
    plain: &[u8],
    expected_count: u32,
    with_encoded_size: bool,
) -> Result<Vec<ArchiveEntryInfo>> {
    let mut reader = Reader::new(plain);
    let declared = reader.read_u32()?;
    if declared != expected_count {
        return Err(ArchiveError::EntryCountMismatch);
    }
    let mut entries = Vec::with_capacity(declared as usize);
    for _ in 0..declared {
        let path_length = reader.read_u16()? as usize;
        let path = reader.read_string(path_length)?;
        if path.is_empty() {
            return Err(ArchiveError::EmptyEntryName);
        }
        let algorithm = reader.read_u8()?;
        let original_size = reader.read_u64()?;
        let stored_size = reader.read_u64()?;
        let encoded_size = if with_encoded_size {
            reader.read_u64()?
        } else {
            stored_size
        };
        let crc32 = reader.read_u32()?;
        // data offset is read but discarded — the caller will index the data
        // section separately when decompression is needed.
        let _data_offset = reader.read_u64()?;

        if original_size > MAX_ENTRY_ORIGINAL_SIZE {
            return Err(ArchiveError::EntryOriginalSizeExceedsCap(path));
        }
        if stored_size > MAX_ENTRY_STORED_SIZE {
            return Err(ArchiveError::EntryStoredSizeExceedsCap(path));
        }
        if encoded_size > MAX_ENTRY_STORED_SIZE + MAX_ENCRYPTED_EXPANSION_MARGIN {
            return Err(ArchiveError::EntryEncodedSizeExceedsCap(path));
        }
        entries.push(ArchiveEntryInfo {
            path,
            algorithm,
            original_size,
            stored_size,
            encoded_size,
            crc32,
        });
    }
    if reader.pos != plain.len() {
        return Err(ArchiveError::DirectoryInconsistent);
    }
    Ok(entries)
}

/// Top-level helper for read-side callers: parse metadata of an
/// **unencrypted** archive. Encrypted archives need the C++ crypto stack and
/// are rejected by this entry point.
pub fn read_metadata_plain(raw: &[u8]) -> Result<ArchiveMetadata> {
    let header = parse_header(raw)?;
    if header.metadata.encrypted {
        return Err(ArchiveError::EncryptedArchive);
    }
    match header.flavour {
        ArchiveFlavour::Wzox | ArchiveFlavour::Zox6 | ArchiveFlavour::Zox5 => {
            let footer = parse_footer(raw, &header)?;
            verify_integrity(raw, &header, &footer)?;
            let mut metadata = header.metadata.clone();
            if metadata.payload_checksum != footer.central_directory_checksum {
                return Err(ArchiveError::FooterInconsistent);
            }
            metadata.payload_checksum = footer.central_directory_checksum;
            Ok(metadata)
        }
        ArchiveFlavour::Zox4Legacy => Ok(header.metadata),
    }
}

/// Top-level helper: parse the directory listing of an **unencrypted**
/// archive. Encrypted archives need the C++ crypto stack and are rejected.
pub fn read_index_plain(raw: &[u8]) -> Result<Vec<ArchiveEntryInfo>> {
    let header = parse_header(raw)?;
    if header.metadata.encrypted {
        return Err(ArchiveError::EncryptedArchive);
    }
    if matches!(header.flavour, ArchiveFlavour::Zox4Legacy) {
        return read_legacy_index(raw, &header);
    }
    let footer = parse_footer(raw, &header)?;
    verify_integrity(raw, &header, &footer)?;
    if footer.central_directory_offset < header.data_offset as u64 {
        return Err(ArchiveError::CentralDirectoryOffsetInvalid);
    }
    if footer.central_directory_offset > raw.len() as u64 {
        return Err(ArchiveError::CentralDirectoryOffsetOutOfBounds);
    }
    if footer.central_directory_stored_size
        > raw.len() as u64 - footer.central_directory_offset
    {
        return Err(ArchiveError::CentralDirectoryTruncated);
    }
    if footer.central_directory_stored_size > MAX_CENTRAL_DIRECTORY_SIZE
        || footer.central_directory_plain_size > MAX_CENTRAL_DIRECTORY_SIZE
    {
        return Err(ArchiveError::CentralDirectoryTooLarge);
    }
    if footer.entry_count > MAX_ENTRY_COUNT {
        return Err(ArchiveError::EntryCountExceedsCap);
    }
    let cd_offset = usize::try_from(footer.central_directory_offset)
        .map_err(|_| ArchiveError::IntegerOverflow)?;
    let cd_size = usize::try_from(footer.central_directory_stored_size)
        .map_err(|_| ArchiveError::IntegerOverflow)?;
    let directory = &raw[cd_offset..cd_offset + cd_size];

    if directory.len() as u64 != footer.central_directory_plain_size {
        return Err(ArchiveError::CentralDirectoryTruncated);
    }
    let computed_checksum = crc32fast::hash(directory);
    if computed_checksum != footer.central_directory_checksum
        || computed_checksum != header.metadata.payload_checksum
    {
        return Err(ArchiveError::FooterInconsistent);
    }
    parse_directory_entries(directory, footer.entry_count)
}

fn read_legacy_index(raw: &[u8], header: &ParsedHeader) -> Result<Vec<ArchiveEntryInfo>> {
    let mut reader = Reader::with_pos(raw, header.data_offset);
    // Legacy `payload_size` u64 prefix.
    let payload_size = reader.read_u64()? as usize;
    if reader.remaining() < payload_size {
        return Err(ArchiveError::Truncated);
    }
    let payload_start = reader.pos;
    let payload_end = payload_start + payload_size;
    if payload_end != raw.len() {
        return Err(ArchiveError::FooterInconsistent);
    }
    let payload = &raw[payload_start..payload_end];

    let computed = crc32fast::hash(payload);
    if computed != header.metadata.payload_checksum {
        return Err(ArchiveError::FooterInconsistent);
    }

    let mut p = Reader::new(payload);
    let file_count = p.read_u32()?;
    let mut entries = Vec::with_capacity(file_count as usize);
    for _ in 0..file_count {
        let path_len = p.read_u16()? as usize;
        let path = p.read_string(path_len)?;
        if path.is_empty() {
            return Err(ArchiveError::EmptyEntryName);
        }
        let algorithm = p.read_u8()?;
        let original_size = p.read_u64()?;
        let stored_size = p.read_u64()?;
        let crc32 = p.read_u32()?;
        // Skip stored bytes; we only return the metadata.
        let _data = p.read_bytes(usize::try_from(stored_size).map_err(|_| ArchiveError::IntegerOverflow)?)?;
        if original_size > MAX_ENTRY_ORIGINAL_SIZE {
            return Err(ArchiveError::EntryOriginalSizeExceedsCap(path));
        }
        if stored_size > MAX_ENTRY_STORED_SIZE {
            return Err(ArchiveError::EntryStoredSizeExceedsCap(path));
        }
        entries.push(ArchiveEntryInfo {
            path,
            algorithm,
            original_size,
            stored_size,
            encoded_size: stored_size,
            crc32,
        });
    }
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn looks_like_zox_recognises_each_magic() {
        assert!(looks_like_zox_archive(b"WZOX\0"));
        assert!(looks_like_zox_archive(b"ZOX6\0"));
        assert!(looks_like_zox_archive(b"ZOX5\0"));
        assert!(looks_like_zox_archive(b"ZOX4\0"));
        assert!(!looks_like_zox_archive(b"PK\x03\x04"));
        assert!(!looks_like_zox_archive(b"\0\0\0"));
    }

    /// Construct a *minimal* well-formed plain WZOX archive (no entries, no
    /// data section) and round-trip it through the parser.
    fn build_minimal_wzox() -> Vec<u8> {
        let mut buf = Vec::new();
        buf.extend_from_slice(MAGIC_WZOX);
        buf.push(0); // flags = 0 (not encrypted, not solid, not authenticated)
        buf.push(EncryptionAlgorithm::None as u8);
        buf.push(CompressionAlgorithm::Zstd as u8);
        buf.extend_from_slice(&0u64.to_le_bytes()); // created_unix_time
        // payload_checksum (placeholder; we'll overwrite after we know the CD)
        let payload_checksum_pos = buf.len();
        buf.extend_from_slice(&0u32.to_le_bytes());
        // comment
        buf.extend_from_slice(&0u32.to_le_bytes());

        let data_offset = buf.len();
        // No data section since there are no entries.

        // Build central directory: just `declared_count = 0`.
        let central_directory: Vec<u8> = 0u32.to_le_bytes().to_vec();
        let cd_offset = buf.len();
        let cd_size = central_directory.len() as u64;
        buf.extend_from_slice(&central_directory);

        // Compute checksum (CRC32 of the plain central directory).
        let checksum = crc32fast::hash(&central_directory);
        buf[payload_checksum_pos..payload_checksum_pos + 4]
            .copy_from_slice(&checksum.to_le_bytes());

        // Now footer: ZCDR + 3*u64 + 2*u32 + sha512 (64) + sha3 (32). No auth.
        // We need to compute the integrity digests on the prefix-up-to-here.
        let mut footer = Vec::new();
        footer.extend_from_slice(MAGIC_FOOTER);
        footer.extend_from_slice(&(cd_offset as u64).to_le_bytes());
        footer.extend_from_slice(&cd_size.to_le_bytes());
        footer.extend_from_slice(&cd_size.to_le_bytes()); // plain == stored when unencrypted
        footer.extend_from_slice(&checksum.to_le_bytes());
        footer.extend_from_slice(&0u32.to_le_bytes()); // entry_count

        // Digests are computed over (prefix-bytes + footer-without-digests).
        let mut prefix = buf.clone();
        prefix.extend_from_slice(&footer);
        let digests = integrity::compute_digests(&prefix);
        footer.extend_from_slice(&digests.sha512);
        footer.extend_from_slice(&digests.sha3_256);

        buf.extend_from_slice(&footer);
        let _ = data_offset; // silence unused warning
        buf
    }

    #[test]
    fn cpp_parity_roundtrip() {
        let raw = build_minimal_wzox();
        assert!(looks_like_zox_archive(&raw));
        let metadata = read_metadata_plain(&raw).unwrap();
        assert!(!metadata.encrypted);
        assert_eq!(metadata.default_algorithm, CompressionAlgorithm::Zstd as u8);
        assert!(metadata.integrity_sha512);
        let entries = read_index_plain(&raw).unwrap();
        assert!(entries.is_empty());
    }

    #[test]
    fn truncated_archive_is_rejected() {
        let raw = build_minimal_wzox();
        let short = &raw[..raw.len() - 1];
        assert!(read_metadata_plain(short).is_err());
    }

    #[test]
    fn unknown_magic_is_rejected() {
        assert!(matches!(
            read_metadata_plain(b"PK\x03\x04not a zip"),
            Err(ArchiveError::UnknownMagic)
        ));
    }

    #[test]
    fn auth_flag_without_encryption_is_rejected() {
        let mut raw = build_minimal_wzox();
        raw[4] = FLAG_AUTHENTICATED;
        assert!(matches!(
            read_metadata_plain(&raw),
            Err(ArchiveError::AuthRequiresEncryption)
        ));
    }
}
