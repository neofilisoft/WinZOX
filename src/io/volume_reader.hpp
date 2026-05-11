#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace winzox::io {

std::vector<uint8_t> ReadAllVolumes(const std::filesystem::path& archivePath);

} // namespace winzox::io
