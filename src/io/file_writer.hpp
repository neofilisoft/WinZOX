#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace winzox::io {

void WriteFileBytes(const std::filesystem::path& path, const std::vector<uint8_t>& data);

} // namespace winzox::io
