#include "compression/compressor.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <zlib.h>
#include <zstd.h>

namespace zipbox::compression {

namespace {

uLong ToZlibSize(size_t size) {
    if (size > static_cast<size_t>(std::numeric_limits<uLong>::max())) {
        throw std::runtime_error("Data block is too large for zlib");
    }
    return static_cast<uLong>(size);
}

size_t ToSizeT(uint64_t value) {
    if (value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error("Data block is too large for this platform");
    }
    return static_cast<size_t>(value);
}

} // namespace

CompressionAlgorithm ParseAlgorithmName(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lower == "store" || lower == "none") {
        return CompressionAlgorithm::Store;
    }
    if (lower == "zstd") {
        return CompressionAlgorithm::Zstd;
    }
    if (lower == "zlib") {
        return CompressionAlgorithm::Zlib;
    }

    throw std::runtime_error("Unsupported compression algorithm: " + value);
}

std::string AlgorithmName(CompressionAlgorithm algorithm) {
    switch (algorithm) {
    case CompressionAlgorithm::Store:
        return "store";
    case CompressionAlgorithm::Zstd:
        return "zstd";
    case CompressionAlgorithm::Zlib:
        return "zlib";
    }

    throw std::runtime_error("Unknown compression algorithm id");
}

std::vector<uint8_t> CompressBuffer(const std::vector<uint8_t>& data,
                                    CompressionAlgorithm algorithm,
                                    int zstdLevel,
                                    int zlibLevel) {
    switch (algorithm) {
    case CompressionAlgorithm::Store:
        return data;

    case CompressionAlgorithm::Zstd: {
        if (data.empty()) {
            return {};
        }

        const size_t bound = ZSTD_compressBound(data.size());
        std::vector<uint8_t> compressed(bound);
        const size_t written = ZSTD_compress(compressed.data(), compressed.size(), data.data(), data.size(), zstdLevel);
        if (ZSTD_isError(written) != 0) {
            throw std::runtime_error(std::string("Zstd compression failed: ") + ZSTD_getErrorName(written));
        }

        compressed.resize(written);
        return compressed;
    }

    case CompressionAlgorithm::Zlib: {
        if (zlibLevel < 0 || zlibLevel > 9) {
            throw std::runtime_error("Zlib level must be between 0 and 9");
        }

        if (data.empty()) {
            return {};
        }

        const uLong sourceLen = ToZlibSize(data.size());
        uLongf destLen = compressBound(sourceLen);
        std::vector<uint8_t> compressed(destLen);

        if (compress2(compressed.data(), &destLen, data.data(), sourceLen, zlibLevel) != Z_OK) {
            throw std::runtime_error("Zlib compression failed");
        }

        compressed.resize(destLen);
        return compressed;
    }
    }

    throw std::runtime_error("Unsupported compression algorithm");
}

std::vector<uint8_t> DecompressBuffer(const std::vector<uint8_t>& data,
                                      CompressionAlgorithm algorithm,
                                      uint64_t expectedSize) {
    if (expectedSize == 0) {
        if (!data.empty() && algorithm != CompressionAlgorithm::Store) {
            throw std::runtime_error("Archive stores unexpected payload for an empty file");
        }
        return {};
    }

    switch (algorithm) {
    case CompressionAlgorithm::Store:
        if (data.size() != ToSizeT(expectedSize)) {
            throw std::runtime_error("Stored entry size does not match expected size");
        }
        return data;

    case CompressionAlgorithm::Zstd: {
        std::vector<uint8_t> plain(ToSizeT(expectedSize));
        const size_t written = ZSTD_decompress(plain.data(), plain.size(), data.data(), data.size());
        if (ZSTD_isError(written) != 0 || written != plain.size()) {
            throw std::runtime_error("Failed to decompress a Zstd entry");
        }
        return plain;
    }

    case CompressionAlgorithm::Zlib: {
        std::vector<uint8_t> plain(ToSizeT(expectedSize));
        uLongf outputSize = static_cast<uLongf>(plain.size());
        const int result = uncompress(plain.data(), &outputSize, data.data(), ToZlibSize(data.size()));
        if (result != Z_OK || outputSize != plain.size()) {
            throw std::runtime_error("Failed to decompress a zlib entry");
        }
        return plain;
    }
    }

    throw std::runtime_error("Unsupported compression algorithm");
}

} // namespace zipbox::compression
