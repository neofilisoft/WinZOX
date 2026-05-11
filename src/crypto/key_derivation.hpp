#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winzox::crypto {

constexpr uint32_t kLegacyKdfIterations = 10000;
constexpr uint32_t kMinKdfIterations = 100000;
constexpr uint32_t kKdfSchemeMask = 0x80000000u;
constexpr uint32_t kScryptLogNShift = 16u;
constexpr uint32_t kScryptRShift = 8u;
constexpr uint32_t kScryptPShift = 0u;
constexpr uint32_t kScryptByteMask = 0xFFu;
constexpr uint8_t kDefaultScryptLogN = 15u;
constexpr uint8_t kDefaultScryptR = 8u;
constexpr uint8_t kDefaultScryptP = 1u;
constexpr uint32_t kDefaultKdfIterations =
    kKdfSchemeMask |
    (static_cast<uint32_t>(kDefaultScryptLogN) << kScryptLogNShift) |
    (static_cast<uint32_t>(kDefaultScryptR) << kScryptRShift) |
    (static_cast<uint32_t>(kDefaultScryptP) << kScryptPShift);

bool UsesMemoryHardKdf(uint32_t iterations);
bool IsSupportedKdfParameter(uint32_t iterations);
uint32_t EncodeScryptParameters(uint8_t logN, uint8_t r, uint8_t p);

std::vector<unsigned char> DeriveKey(const std::string& password,
                                     const std::vector<unsigned char>& salt,
                                     uint32_t iterations);
std::vector<unsigned char> DeriveAuthenticationKey(const std::string& password,
                                                   const std::vector<unsigned char>& salt,
                                                   uint32_t iterations);

} // namespace winzox::crypto
