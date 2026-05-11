#pragma once

#include "crypto/encryption_provider.hpp"

namespace winzox::crypto {

class AesProvider final : public IEncryptionProvider {
public:
    EncryptionAlgorithm Algorithm() const override;
    const char* Name() const override;
    EncryptionMetadata CreateMetadata() const override;
    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plainText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata) const override;
    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& cipherText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata,
                                 uint64_t plainTextSize) const override;
};

const IEncryptionProvider& GetAesProvider();

// AES-256-GCM AEAD provider. The on-disk blob layout for every Encrypt() call is
//   nonce[12] || ciphertext[N] || tag[16]
// The nonce is freshly generated from RAND_bytes() on every encryption so each
// archive entry gets its own per-entry random IV (Patch 7).
class AesGcmProvider final : public IEncryptionProvider {
public:
    EncryptionAlgorithm Algorithm() const override;
    const char* Name() const override;
    EncryptionMetadata CreateMetadata() const override;
    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plainText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata) const override;
    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& cipherText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata,
                                 uint64_t plainTextSize) const override;
};

const IEncryptionProvider& GetAesGcmProvider();

// ChaCha20-Poly1305 AEAD provider. Same on-disk layout as AesGcmProvider.
class ChaCha20Poly1305Provider final : public IEncryptionProvider {
public:
    EncryptionAlgorithm Algorithm() const override;
    const char* Name() const override;
    EncryptionMetadata CreateMetadata() const override;
    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plainText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata) const override;
    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& cipherText,
                                 const std::string& password,
                                 const EncryptionMetadata& metadata,
                                 uint64_t plainTextSize) const override;
};

const IEncryptionProvider& GetChaCha20Poly1305Provider();

} // namespace winzox::crypto
