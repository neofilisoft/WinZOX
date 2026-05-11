#include "crypto/algorithms/aes/aes_provider.hpp"

#include "crypto/key_derivation.hpp"

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

std::vector<uint8_t> TransformAes256Cbc(const std::vector<uint8_t>& input,
                                        const std::vector<unsigned char>& key,
                                        const std::vector<unsigned char>& iv,
                                        bool encrypt) {
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("Failed to create OpenSSL cipher context");
    }

    std::vector<uint8_t> output(input.size() + EVP_MAX_BLOCK_LENGTH);
    int produced = 0;
    int finalBytes = 0;

    const int initResult = encrypt
        ? EVP_EncryptInit_ex(context, EVP_aes_256_cbc(), nullptr, key.data(), iv.data())
        : EVP_DecryptInit_ex(context, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
    if (initResult != 1) {
        EVP_CIPHER_CTX_free(context);
        throw std::runtime_error("Failed to initialize cipher");
    }

    const int updateResult = encrypt
        ? EVP_EncryptUpdate(context, output.data(), &produced, input.data(), ToOpenSslSize(input.size()))
        : EVP_DecryptUpdate(context, output.data(), &produced, input.data(), ToOpenSslSize(input.size()));
    if (updateResult != 1) {
        EVP_CIPHER_CTX_free(context);
        throw std::runtime_error(encrypt ? "Encryption update failed" : "Decryption update failed");
    }

    const int finalResult = encrypt
        ? EVP_EncryptFinal_ex(context, output.data() + produced, &finalBytes)
        : EVP_DecryptFinal_ex(context, output.data() + produced, &finalBytes);
    if (finalResult != 1) {
        EVP_CIPHER_CTX_free(context);
        throw std::runtime_error(encrypt ? "Encryption finalization failed" : "Failed to decrypt archive payload");
    }

    EVP_CIPHER_CTX_free(context);
    output.resize(static_cast<size_t>(produced + finalBytes));
    return output;
}

// Generic AEAD encrypt/decrypt that frames the on-disk blob as
//   nonce[kAeadNonceSize] || ciphertext[N] || tag[kAeadTagSize]
// so callers don't have to manage nonces or tags separately. The nonce is
// freshly generated from RAND_bytes on every encryption.
std::vector<uint8_t> AeadEncrypt(const EVP_CIPHER* cipher,
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

std::vector<uint8_t> AeadDecrypt(const EVP_CIPHER* cipher,
                                 const std::vector<uint8_t>& blob,
                                 const std::vector<unsigned char>& key,
                                 const std::vector<uint8_t>& aad,
                                 uint64_t plainTextSize) {
    if (cipher == nullptr) {
        throw std::runtime_error("AEAD cipher is unavailable in this OpenSSL build");
    }
    if (blob.size() < kAeadNonceSize + kAeadTagSize) {
        throw std::runtime_error("AEAD blob is truncated");
    }

    const size_t cipherSize = blob.size() - kAeadNonceSize - kAeadTagSize;
    if (plainTextSize != 0 && cipherSize != plainTextSize) {
        throw std::runtime_error("AEAD blob size does not match expected plaintext size");
    }

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

    // Cast away const for OpenSSL's tag set call (it doesn't actually mutate).
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
        // AEAD authentication failure: wrong password, tampered data, or both.
        throw std::runtime_error("AEAD authentication failed (wrong password or modified data)");
    }
    plain.resize(static_cast<size_t>(outLen + finalLen));
    return plain;
}

EncryptionMetadata MakeAeadMetadataTemplate() {
    // For AEAD modes we only need salt + iterations in the header. The IV is
    // generated freshly per call by AeadEncrypt and is stored inline in each
    // ciphertext blob, so we never have to worry about IV reuse across entries.
    EncryptionMetadata metadata;
    metadata.salt.resize(16);
    metadata.iterations = kDefaultKdfIterations;
    if (RAND_bytes(metadata.salt.data(), static_cast<int>(metadata.salt.size())) != 1) {
        throw std::runtime_error("Failed to generate encryption salt");
    }
    return metadata;
}

} // namespace

// =====================================================================
// Legacy AES-256-CBC provider (kept for backward read compatibility)
// =====================================================================

EncryptionAlgorithm AesProvider::Algorithm() const {
    return EncryptionAlgorithm::Aes256;
}

const char* AesProvider::Name() const {
    return "aes256-cbc";
}

EncryptionMetadata AesProvider::CreateMetadata() const {
    EncryptionMetadata metadata;
    metadata.salt.resize(16);
    metadata.ivPrimary.resize(16);
    metadata.iterations = kDefaultKdfIterations;

    if (RAND_bytes(metadata.salt.data(), static_cast<int>(metadata.salt.size())) != 1 ||
        RAND_bytes(metadata.ivPrimary.data(), static_cast<int>(metadata.ivPrimary.size())) != 1) {
        throw std::runtime_error("Failed to generate encryption metadata");
    }

    return metadata;
}

std::vector<uint8_t> AesProvider::Encrypt(const std::vector<uint8_t>& plainText,
                                          const std::string& password,
                                          const EncryptionMetadata& metadata) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    return TransformAes256Cbc(plainText, key, metadata.ivPrimary, true);
}

std::vector<uint8_t> AesProvider::Decrypt(const std::vector<uint8_t>& cipherText,
                                          const std::string& password,
                                          const EncryptionMetadata& metadata,
                                          uint64_t) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    return TransformAes256Cbc(cipherText, key, metadata.ivPrimary, false);
}

const IEncryptionProvider& GetAesProvider() {
    static const AesProvider provider;
    return provider;
}

// =====================================================================
// AES-256-GCM AEAD provider (v3 default candidate)
// =====================================================================

EncryptionAlgorithm AesGcmProvider::Algorithm() const {
    return EncryptionAlgorithm::Aes256Gcm;
}

const char* AesGcmProvider::Name() const {
    return "aes256-gcm";
}

EncryptionMetadata AesGcmProvider::CreateMetadata() const {
    return MakeAeadMetadataTemplate();
}

std::vector<uint8_t> AesGcmProvider::Encrypt(const std::vector<uint8_t>& plainText,
                                             const std::string& password,
                                             const EncryptionMetadata& metadata) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    const std::vector<uint8_t> aad = {'W', 'Z', 'O', 'X', 'A', 'E', 'A', 'D', 0x01};
    return AeadEncrypt(EVP_aes_256_gcm(), plainText, key, aad);
}

std::vector<uint8_t> AesGcmProvider::Decrypt(const std::vector<uint8_t>& cipherText,
                                             const std::string& password,
                                             const EncryptionMetadata& metadata,
                                             uint64_t plainTextSize) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    const std::vector<uint8_t> aad = {'W', 'Z', 'O', 'X', 'A', 'E', 'A', 'D', 0x01};
    return AeadDecrypt(EVP_aes_256_gcm(), cipherText, key, aad, plainTextSize);
}

const IEncryptionProvider& GetAesGcmProvider() {
    static const AesGcmProvider provider;
    return provider;
}

// =====================================================================
// ChaCha20-Poly1305 AEAD provider
// =====================================================================

EncryptionAlgorithm ChaCha20Poly1305Provider::Algorithm() const {
    return EncryptionAlgorithm::ChaCha20Poly1305;
}

const char* ChaCha20Poly1305Provider::Name() const {
    return "chacha20-poly1305";
}

EncryptionMetadata ChaCha20Poly1305Provider::CreateMetadata() const {
    return MakeAeadMetadataTemplate();
}

std::vector<uint8_t> ChaCha20Poly1305Provider::Encrypt(const std::vector<uint8_t>& plainText,
                                                       const std::string& password,
                                                       const EncryptionMetadata& metadata) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    const std::vector<uint8_t> aad = {'W', 'Z', 'O', 'X', 'A', 'E', 'A', 'D', 0x02};
    return AeadEncrypt(EVP_chacha20_poly1305(), plainText, key, aad);
}

std::vector<uint8_t> ChaCha20Poly1305Provider::Decrypt(const std::vector<uint8_t>& cipherText,
                                                       const std::string& password,
                                                       const EncryptionMetadata& metadata,
                                                       uint64_t plainTextSize) const {
    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }
    const std::vector<unsigned char> key = DeriveKey(password, metadata.salt, metadata.iterations);
    const std::vector<uint8_t> aad = {'W', 'Z', 'O', 'X', 'A', 'E', 'A', 'D', 0x02};
    return AeadDecrypt(EVP_chacha20_poly1305(), cipherText, key, aad, plainTextSize);
}

const IEncryptionProvider& GetChaCha20Poly1305Provider() {
    static const ChaCha20Poly1305Provider provider;
    return provider;
}

} // namespace winzox::crypto
