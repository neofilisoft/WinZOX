#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zipbox::compression {

enum class CompressionAlgorithm : uint8_t {
    Store = 0,
    Zstd = 1,
    Zlib = 2,
};

CompressionAlgorithm ParseAlgorithmName(const std::string& value);
std::string AlgorithmName(CompressionAlgorithm algorithm);

std::vector<uint8_t> CompressBuffer(const std::vector<uint8_t>& data,
                                    CompressionAlgorithm algorithm,
                                    int zstdLevel,
                                    int zlibLevel);
std::vector<uint8_t> DecompressBuffer(const std::vector<uint8_t>& data,
                                      CompressionAlgorithm algorithm,
                                      uint64_t expectedSize);

} // namespace zipbox::compression
