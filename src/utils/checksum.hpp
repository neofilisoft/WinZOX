#pragma once

#include <cstdint>
#include <vector>

namespace zipbox::utils {

uint32_t ComputeCrc32(const std::vector<uint8_t>& data);

} // namespace zipbox::utils
