#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winzox::crypto {

enum class EncryptionAlgorithm : uint8_t {
    None = 0,
    Aes256 = 1,            // legacy: AES-256-CBC + PKCS#7 (read-only on v3+)
    Gorgon = 2,            // legacy: AES-256-CBC half | Serpent-256-CBC half (read-only on v3+)
    Aes256Gcm = 3,         // v3 AEAD: AES-256-GCM with random per-entry nonce
    ChaCha20Poly1305 = 4,  // v3 AEAD: ChaCha20-Poly1305 with random per-entry nonce
    GorgonAead = 5,        // v3 AEAD cascade: ChaCha20-Poly1305(AES-256-GCM(plain))
};

bool IsAeadAlgorithm(EncryptionAlgorithm algorithm);
bool IsLegacyCbcAlgorithm(EncryptionAlgorithm algorithm);

struct EncryptionMetadata {
    std::vector<unsigned char> salt;
    std::vector<unsigned char> ivPrimary;
    std::vector<unsigned char> ivSecondary;
    uint32_t iterations = 100000;
};

EncryptionAlgorithm ParseEncryptionAlgorithmName(const std::string& value);
std::string EncryptionAlgorithmName(EncryptionAlgorithm algorithm);

EncryptionMetadata CreateEncryptionMetadata(EncryptionAlgorithm algorithm);
std::vector<uint8_t> EncryptPayload(const std::vector<uint8_t>& plainText,
                                    const std::string& password,
                                    const EncryptionMetadata& metadata,
                                    EncryptionAlgorithm algorithm);
std::vector<uint8_t> DecryptPayload(const std::vector<uint8_t>& cipherText,
                                    const std::string& password,
                                    const EncryptionMetadata& metadata,
                                    EncryptionAlgorithm algorithm,
                                    uint64_t plainTextSize);

} // namespace winzox::crypto
