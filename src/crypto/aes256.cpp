#include "crypto/aes256.hpp"

#include "crypto/gorgon.hpp"
#include "crypto/key_derivation.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace zipbox::crypto {

namespace {

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

} // namespace

EncryptionAlgorithm ParseEncryptionAlgorithmName(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower == "none") {
        return EncryptionAlgorithm::None;
    }
    if (lower == "aes" || lower == "aes256" || lower == "aes-256") {
        return EncryptionAlgorithm::Aes256;
    }
    if (lower == "gorgon") {
        return EncryptionAlgorithm::Gorgon;
    }

    throw std::runtime_error("Unsupported encryption algorithm: " + value);
}

std::string EncryptionAlgorithmName(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
    case EncryptionAlgorithm::None:
        return "none";
    case EncryptionAlgorithm::Aes256:
        return "aes256";
    case EncryptionAlgorithm::Gorgon:
        return "gorgon";
    }

    throw std::runtime_error("Unknown encryption algorithm id");
}

EncryptionMetadata CreateEncryptionMetadata(EncryptionAlgorithm algorithm) {
    EncryptionMetadata metadata;
    metadata.salt.resize(16);
    metadata.ivPrimary.resize(16);

    if (RAND_bytes(metadata.salt.data(), static_cast<int>(metadata.salt.size())) != 1 ||
        RAND_bytes(metadata.ivPrimary.data(), static_cast<int>(metadata.ivPrimary.size())) != 1) {
        throw std::runtime_error("Failed to generate encryption metadata");
    }

    if (algorithm == EncryptionAlgorithm::Gorgon) {
        metadata.ivSecondary.resize(16);
        if (RAND_bytes(metadata.ivSecondary.data(), static_cast<int>(metadata.ivSecondary.size())) != 1) {
            throw std::runtime_error("Failed to generate Gorgon IV");
        }
    }

    return metadata;
}

std::vector<uint8_t> EncryptPayload(const std::vector<uint8_t>& plainText,
                                    const std::string& password,
                                    const EncryptionMetadata& metadata,
                                    EncryptionAlgorithm algorithm) {
    if (algorithm == EncryptionAlgorithm::None) {
        return plainText;
    }

    if (password.empty()) {
        throw std::runtime_error("Password is required to encrypt this .zox archive");
    }

    if (algorithm == EncryptionAlgorithm::Aes256) {
        const std::vector<unsigned char> key = DeriveKey(password, metadata.salt);
        return TransformAes256Cbc(plainText, key, metadata.ivPrimary, true);
    }

    if (algorithm == EncryptionAlgorithm::Gorgon) {
        return EncryptGorgon(plainText, password, metadata);
    }

    throw std::runtime_error("Unsupported encryption algorithm");
}

std::vector<uint8_t> DecryptPayload(const std::vector<uint8_t>& cipherText,
                                    const std::string& password,
                                    const EncryptionMetadata& metadata,
                                    EncryptionAlgorithm algorithm,
                                    uint64_t plainTextSize) {
    if (algorithm == EncryptionAlgorithm::None) {
        return cipherText;
    }

    if (password.empty()) {
        throw std::runtime_error("Password is required to open this .zox archive");
    }

    if (algorithm == EncryptionAlgorithm::Aes256) {
        const std::vector<unsigned char> key = DeriveKey(password, metadata.salt);
        return TransformAes256Cbc(cipherText, key, metadata.ivPrimary, false);
    }

    if (algorithm == EncryptionAlgorithm::Gorgon) {
        return DecryptGorgon(cipherText, password, metadata, plainTextSize);
    }

    throw std::runtime_error("Unsupported encryption algorithm");
}

} // namespace zipbox::crypto
