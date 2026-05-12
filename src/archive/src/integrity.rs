//! SHA-512 + SHA3-256 integrity accumulator.
//!
//! Replaces the C++ `ArchiveIntegrityAccumulator` (which depended on
//! OpenSSL's EVP API). Uses the audited RustCrypto crates (`sha2`, `sha3`)
//! plus a constant-time comparison via `subtle`. Bit-for-bit identical
//! output to the OpenSSL implementation, asserted by KAT tests.

use sha2::{Digest as _, Sha512};
use sha3::Sha3_256;
use subtle::ConstantTimeEq;

use crate::{SHA3_256_DIGEST_SIZE, SHA512_DIGEST_SIZE};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArchiveIntegrityDigests {
    pub sha512: [u8; SHA512_DIGEST_SIZE],
    pub sha3_256: [u8; SHA3_256_DIGEST_SIZE],
}

impl Default for ArchiveIntegrityDigests {
    fn default() -> Self {
        Self {
            sha512: [0u8; SHA512_DIGEST_SIZE],
            sha3_256: [0u8; SHA3_256_DIGEST_SIZE],
        }
    }
}

/// Streaming accumulator. Call [`update`](Self::update) repeatedly, then
/// [`finalize`](Self::finalize) once.
#[derive(Debug)]
pub struct ArchiveIntegrityAccumulator {
    sha512: Sha512,
    sha3_256: Sha3_256,
    finalized: bool,
}

impl Default for ArchiveIntegrityAccumulator {
    fn default() -> Self {
        Self::new()
    }
}

impl ArchiveIntegrityAccumulator {
    pub fn new() -> Self {
        Self {
            sha512: Sha512::new(),
            sha3_256: Sha3_256::new(),
            finalized: false,
        }
    }

    pub fn update(&mut self, data: &[u8]) {
        if self.finalized {
            // Match the C++ semantics: updating a finalized accumulator is a
            // programming error. We panic here because the FFI layer catches
            // it and returns a status code.
            panic!("ArchiveIntegrityAccumulator: update after finalize");
        }
        if !data.is_empty() {
            self.sha512.update(data);
            self.sha3_256.update(data);
        }
    }

    pub fn finalize(mut self) -> ArchiveIntegrityDigests {
        if self.finalized {
            panic!("ArchiveIntegrityAccumulator: finalize called twice");
        }
        self.finalized = true;
        let mut sha512 = [0u8; SHA512_DIGEST_SIZE];
        let mut sha3_256 = [0u8; SHA3_256_DIGEST_SIZE];
        sha512.copy_from_slice(self.sha512.finalize().as_slice());
        sha3_256.copy_from_slice(self.sha3_256.finalize().as_slice());
        ArchiveIntegrityDigests { sha512, sha3_256 }
    }
}

/// Convenience wrapper around the streaming accumulator.
pub fn compute_digests(data: &[u8]) -> ArchiveIntegrityDigests {
    let mut acc = ArchiveIntegrityAccumulator::new();
    acc.update(data);
    acc.finalize()
}

/// Constant-time digest comparison.
pub fn digests_equal(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() {
        return false;
    }
    left.ct_eq(right).into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex_literal::hex;

    #[test]
    fn empty_input_matches_test_vectors() {
        // SHA-512("")
        const SHA512_EMPTY: [u8; 64] = hex!(
            "cf83e1357eefb8bdf1542850d66d8007"
            "d620e4050b5715dc83f4a921d36ce9ce"
            "47d0d13c5d85f2b0ff8318d2877eec2f"
            "63b931bd47417a81a538327af927da3e"
        );
        // SHA3-256("")
        const SHA3_EMPTY: [u8; 32] = hex!(
            "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"
        );
        let digests = compute_digests(&[]);
        assert_eq!(digests.sha512, SHA512_EMPTY);
        assert_eq!(digests.sha3_256, SHA3_EMPTY);
    }

    #[test]
    fn abc_matches_kat() {
        const SHA512_ABC: [u8; 64] = hex!(
            "ddaf35a193617abacc417349ae204131"
            "12e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd"
            "454d4423643ce80e2a9ac94fa54ca49f"
        );
        const SHA3_ABC: [u8; 32] = hex!(
            "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"
        );
        let digests = compute_digests(b"abc");
        assert_eq!(digests.sha512, SHA512_ABC);
        assert_eq!(digests.sha3_256, SHA3_ABC);
    }

    #[test]
    fn streaming_matches_one_shot() {
        let big: Vec<u8> = (0..4096u32).flat_map(|n| n.to_le_bytes()).collect();
        let oneshot = compute_digests(&big);

        let mut acc = ArchiveIntegrityAccumulator::new();
        for chunk in big.chunks(7) {
            acc.update(chunk);
        }
        let streamed = acc.finalize();
        assert_eq!(oneshot, streamed);
    }

    #[test]
    fn digests_equal_is_length_strict() {
        let a = [1u8; 32];
        let b = [1u8; 32];
        let c = [1u8; 16];
        assert!(digests_equal(&a, &b));
        assert!(!digests_equal(&a, &c));
    }
}
