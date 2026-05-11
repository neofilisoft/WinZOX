#include "utils/checksum.hpp"

#include <zlib.h>

namespace winzox::utils {

uint32_t ComputeCrc32(const std::vector<uint8_t>& data) {
    return static_cast<uint32_t>(crc32(0L, data.data(), static_cast<uInt>(data.size())));
}

} // namespace winzox::utils
