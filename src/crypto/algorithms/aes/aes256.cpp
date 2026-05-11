#include "crypto/algorithms/aes/aes256.hpp"

#include "crypto/algorithms/aes/aes_provider.hpp"
#include "crypto/encryption_provider.hpp"
#include "crypto/algorithms/gorgon/gorgon_provider.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace winzox::crypto {

namespace {

const IEncryptionProvider& ResolveProvider(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
    case EncryptionAlgorithm::Aes256:
        return GetAesProvider();
    case EncryptionAlgorithm::Gorgon:
        return GetGorgonProvider();
    case EncryptionAlgorithm::Aes256Gcm:
        return GetAesGcmProvider();
    case EncryptionAlgorithm::ChaCha20Poly1305:
        return GetChaCha20Poly1305Provider();
    case EncryptionAlgorithm::GorgonAead:
        return GetGorgonAeadProvider();
    case EncryptionAlgorithm::None:
        break;
    }

    throw std::runtime_error("Unsupported encryption algorithm");
}

} // namespace

bool IsAeadAlgorithm(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
    case EncryptionAlgorithm::Aes256Gcm:
    case EncryptionAlgorithm::ChaCha20Poly1305:
    case EncryptionAlgorithm::GorgonAead:
        return true;
    default:
        return false;
    }
}

bool IsLegacyCbcAlgorithm(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
    case EncryptionAlgorithm::Aes256:
    case EncryptionAlgorithm::Gorgon:
        return true;
    default:
        return false;
    }
}

EncryptionAlgorithm ParseEncryptionAlgorithmName(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower == "none") {
        return EncryptionAlgorithm::None;
    }
    if (lower == "aes" || lower == "aes256" || lower == "aes-256" ||
        lower == "aes-cbc" || lower == "aes256-cbc" || lower == "aes-256-cbc") {
        return EncryptionAlgorithm::Aes256;
    }
    if (lower == "gorgon" || lower == "gorgon-cbc" || lower == "gorgon1") {
        return EncryptionAlgorithm::Gorgon;
    }
    if (lower == "aes-gcm" || lower == "aes256-gcm" || lower == "aes-256-gcm") {
        return EncryptionAlgorithm::Aes256Gcm;
    }
    if (lower == "chacha20" || lower == "chacha20-poly1305" || lower == "chacha") {
        return EncryptionAlgorithm::ChaCha20Poly1305;
    }
    if (lower == "gorgon-aead" || lower == "gorgon2" || lower == "gorgon-v2") {
        return EncryptionAlgorithm::GorgonAead;
    }

    throw std::runtime_error("Unsupported encryption algorithm: " + value);
}

std::string EncryptionAlgorithmName(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
    case EncryptionAlgorithm::None:
        return "none";
    case EncryptionAlgorithm::Aes256:
        return "aes256-cbc";
    case EncryptionAlgorithm::Gorgon:
        return "gorgon-cbc";
    case EncryptionAlgorithm::Aes256Gcm:
        return "aes256-gcm";
    case EncryptionAlgorithm::ChaCha20Poly1305:
        return "chacha20-poly1305";
    case EncryptionAlgorithm::GorgonAead:
        return "gorgon-aead";
    }

    throw std::runtime_error("Unknown encryption algorithm id");
}

EncryptionMetadata CreateEncryptionMetadata(EncryptionAlgorithm algorithm) {
    if (algorithm == EncryptionAlgorithm::None) {
        return {};
    }

    return ResolveProvider(algorithm).CreateMetadata();
}

const IEncryptionProvider& GetEncryptionProvider(EncryptionAlgorithm algorithm) {
    if (algorithm == EncryptionAlgorithm::None) {
        throw std::runtime_error("No provider exists for the 'none' encryption mode");
    }

    return ResolveProvider(algorithm);
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

    return ResolveProvider(algorithm).Encrypt(plainText, password, metadata);
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

    return ResolveProvider(algorithm).Decrypt(cipherText, password, metadata, plainTextSize);
}

} // namespace winzox::crypto
