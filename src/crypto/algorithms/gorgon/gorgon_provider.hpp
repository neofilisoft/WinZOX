#pragma once

#include "crypto/encryption_provider.hpp"

namespace winzox::crypto {

class GorgonProvider final : public IEncryptionProvider {
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

const IEncryptionProvider& GetGorgonProvider();

// Gorgon v2 — AEAD cascade. The plaintext is first encrypted with AES-256-GCM
// (using a key/nonce derived from the master scrypt output) and the inner
// AEAD blob is then re-encrypted with ChaCha20-Poly1305 using a second
// independent key/nonce. Either layer's tag failure aborts the whole
// decryption, so the cascade is at least as strong as the strongest layer.
//
// On-disk blob layout (single Encrypt() call output):
//   nonceOuter[12] || cipherOuter || tagOuter[16]
// Where cipherOuter contains, after one AEAD decrypt:
//   nonceInner[12] || cipherInner || tagInner[16]
class GorgonAeadProvider final : public IEncryptionProvider {
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

const IEncryptionProvider& GetGorgonAeadProvider();

} // namespace winzox::crypto
