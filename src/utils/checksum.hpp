#pragma once

#include <cstdint>
#include <vector>

namespace winzox::utils {

uint32_t ComputeCrc32(const std::vector<uint8_t>& data);

} // namespace winzox::utils
