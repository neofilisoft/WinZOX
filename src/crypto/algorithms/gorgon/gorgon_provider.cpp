#include "crypto/algorithms/gorgon/gorgon_provider.hpp"

#include "crypto/algorithms/gorgon/gorgon.hpp"
#include "crypto/key_derivation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace winzox::crypto {

namespace {

constexpr size_t kAeadNonceSize = 12;
constexpr size_t kAeadTagSize = 16;

int ToOpenSslSize(size_t size) {
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Data block is too large for OpenSSL");
    }
    return static_cast<int>(size);
}

// Independent labelled salt derivation so the two cascade layers use distinct
// keys even though they share the same archive-level salt + password material.
std::vector<unsigned char> LabelSalt(const std::vector<unsigned char>& salt, const char* label) {
    std::vector<unsigned char> derived;
    derived.reserve(salt.size() + std::strlen(label));
    derived.insert(derived.end(), salt.begin(), salt.end());
    derived.insert(derived.end(), label, label + std::strlen(label));
    return derived;
}

std::vector<uint8_t> AeadEncryptOnce(const EVP_CIPHER* cipher,
                                     const std::vector<uint8_t>& plain,
                                     const std::vector<unsigned char>& key,
                                     const std::vector<uint8_t>& aad) {
    if (cipher == nullptr) {
        throw std::runtime_error("AEAD cipher is unavailable in this OpenSSL build");
    }

    std::vector<uint8_t> nonce(kAeadNonceSize);
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("Failed to generate AEAD nonce");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("Failed to allocate OpenSSL cipher context");
    }

    std::vector<uint8_t> output;
    output.resize(kAeadNonceSize + plain.size() + kAeadTagSize);
    std::memcpy(output.data(), nonce.data(), kAeadNonceSize);

    int outLen = 0;
    int finalLen = 0;
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, ToOpenSslSize(kAeadNonceSize), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize AEAD encryption");
    }

    if (!aad.empty()) {
        if (EVP_EncryptUpdate(ctx, nullptr, &outLen, aad.data(), ToOpenSslSize(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to bind AEAD associated data");
        }
    }

    if (!plain.empty()) {
        if (EVP_EncryptUpdate(ctx,
                              output.data() + kAeadNonceSize,
                              &outLen,
                              plain.data(),
                              ToOpenSslSize(plain.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AEAD encryption update failed");
        }
    } else {
        outLen = 0;
    }

    if (EVP_EncryptFinal_ex(ctx, output.data() + kAeadNonceSize + outLen, &finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AEAD encryption finalization failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx,
                            EVP_CTRL_AEAD_GET_TAG,
                            ToOpenSslSize(kAeadTagSize),
                            output.data() + kAeadNonceSize + plain.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AEAD tag extraction failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    return output;
}

std::vector<uint8_t> AeadDecryptOnce(const EVP_CIPHER* cipher,
                                     const std::vector<uint8_t>& blob,
                                     const std::vector<unsigned char>& key,
                                     const std::vector<uint8_t>& aad) {
    if (cipher == nullptr) {
        throw std::runtime_error("AEAD cipher is unavailable in this OpenSSL build");
    }
    if (blob.size() < kAeadNonceSize + kAeadTagSize) {
        throw std::runtime_error("AEAD blob is truncated");
    }

    const size_t cipherSize = blob.size() - kAeadNonceSize - kAeadTagSize;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("Failed to allocate OpenSSL cipher context");
    }

    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, ToOpenSslSize(kAeadNonceSize), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), blob.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize AEAD decryption");
    }

    int outLen = 0;
    int finalLen = 0;
    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &outLen, aad.data(), ToOpenSslSize(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to bind AEAD associated data on decrypt");
        }
    }

    std::vector<uint8_t> plain(cipherSize);
    if (cipherSize > 0) {
        if (EVP_DecryptUpdate(ctx,
                              plain.data(),
                              &outLen,
                              blob.data() + kAeadNonceSize,
                              ToOpenSslSize(cipherSize)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AEAD decryption update failed");
        }
    } else {
        outLen = 0;
    }

    void* tagPtr = const_cast<uint8_t*>(blob.data() + kAeadNonceSize + cipherSize);
    if (EVP_CIPHER_CTX_ctrl(ctx,
                            EVP_CTRL_AEAD_SET_TAG,
                            ToOpenSslSize(kAeadTagSize),
                            tagPtr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AEAD tag set failed");
    }

    const int finalResult = EVP_DecryptFinal_ex(ctx, plain.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);
    if (finalResult != 1) {
        throw std::runtime_error("AEAD authentication failed (wrong password or modified data)");
    }
    plain.resize(static_cast<size_t>(outLen + finalLen));
    return plain;
}

} // namespace

// =====================================================================
// Legacy Gorgon (CBC cascade) — kept for backward compatibility
// =====================================================================

EncryptionAlgorithm GorgonProvider::Algorithm() const {
    return EncryptionAlgorithm::Gorgon;
}

const char* GorgonProvider::Name() const {
    return "gorgon-cbc";
}

EncryptionMetadata GorgonProvider::CreateMetadata() const {
    EncryptionMetadata metadata;
    metadata.salt.resize(16);
    metadata.ivPrimary.resize(16);
    metadata.ivSecondary.resize(16);
    metadata.iterations = kDefaultKdfIterations;

    if (RAND_bytes(metadata.salt.data(), static_cast<int>(metadata.salt.size())) != 1 ||
        RAND_bytes(metadata.ivPrimary.data(), static_cast<int>(metadata.ivPrimary.size())) != 1 ||
        RAND_bytes(metadata.ivSecondary.data(), static_cast<int>(metadata.ivSecondary.size())) != 1) {
        throw std::runtime_error("Failed to generate Gorgon encryption metadata");
    }

    return metadata;
}

std::vector<uint8_t> GorgonProvider::Encrypt(const std::vector<uint8_t>& plainText,
                                             const std::string& password,
                                             const EncryptionMetadata& metadata) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }
    return EncryptGorgon(plainText, password, metadata);
}

std::vector<uint8_t> GorgonProvider::Decrypt(const std::vector<uint8_t>& cipherText,
                                             const std::string& password,
                                             const EncryptionMetadata& metadata,
                                             uint64_t plainTextSize) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }
    return DecryptGorgon(cipherText, password, metadata, plainTextSize);
}

const IEncryptionProvider& GetGorgonProvider() {
    static const GorgonProvider provider;
    return provider;
}

// =====================================================================
// Gorgon v2 — AEAD cascade (default for v3 archives)
// =====================================================================

EncryptionAlgorithm GorgonAeadProvider::Algorithm() const {
    return EncryptionAlgorithm::GorgonAead;
}

const char* GorgonAeadProvider::Name() const {
    return "gorgon-aead";
}

EncryptionMetadata GorgonAeadProvider::CreateMetadata() const {
    EncryptionMetadata metadata;
    metadata.salt.resize(16);
    metadata.iterations = kDefaultKdfIterations;
    if (RAND_bytes(metadata.salt.data(), static_cast<int>(metadata.salt.size())) != 1) {
        throw std::runtime_error("Failed to generate Gorgon-AEAD salt");
    }
    return metadata;
}

std::vector<uint8_t> GorgonAeadProvider::Encrypt(const std::vector<uint8_t>& plainText,
                                                 const std::string& password,
                                                 const EncryptionMetadata& metadata) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }
    const std::vector<unsigned char> innerKey =
        DeriveKey(password, LabelSalt(metadata.salt, "gorgon-aead/aes"), metadata.iterations);
    const std::vector<unsigned char> outerKey =
        DeriveKey(password, LabelSalt(metadata.salt, "gorgon-aead/chacha"), metadata.iterations);

    const std::vector<uint8_t> innerAad = {'W', 'Z', 'O', 'X', 'G', 'R', 'G', '2', 'I'};
    const std::vector<uint8_t> outerAad = {'W', 'Z', 'O', 'X', 'G', 'R', 'G', '2', 'O'};

    const std::vector<uint8_t> innerBlob = AeadEncryptOnce(EVP_aes_256_gcm(), plainText, innerKey, innerAad);
    return AeadEncryptOnce(EVP_chacha20_poly1305(), innerBlob, outerKey, outerAad);
}

std::vector<uint8_t> GorgonAeadProvider::Decrypt(const std::vector<uint8_t>& cipherText,
                                                 const std::string& password,
                                                 const EncryptionMetadata& metadata,
                                                 uint64_t plainTextSize) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }
    const std::vector<unsigned char> innerKey =
        DeriveKey(password, LabelSalt(metadata.salt, "gorgon-aead/aes"), metadata.iterations);
    const std::vector<unsigned char> outerKey =
        DeriveKey(password, LabelSalt(metadata.salt, "gorgon-aead/chacha"), metadata.iterations);

    const std::vector<uint8_t> innerAad = {'W', 'Z', 'O', 'X', 'G', 'R', 'G', '2', 'I'};
    const std::vector<uint8_t> outerAad = {'W', 'Z', 'O', 'X', 'G', 'R', 'G', '2', 'O'};

    const std::vector<uint8_t> innerBlob = AeadDecryptOnce(EVP_chacha20_poly1305(), cipherText, outerKey, outerAad);
    const std::vector<uint8_t> plain = AeadDecryptOnce(EVP_aes_256_gcm(), innerBlob, innerKey, innerAad);
    if (plainTextSize != 0 && plain.size() != plainTextSize) {
        throw std::runtime_error("Gorgon-AEAD plaintext size mismatch");
    }
    return plain;
}

const IEncryptionProvider& GetGorgonAeadProvider() {
    static const GorgonAeadProvider provider;
    return provider;
}

} // namespace winzox::crypto
