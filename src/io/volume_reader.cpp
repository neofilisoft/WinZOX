#include "io/volume_reader.hpp"

#include "io/file_reader.hpp"
#include "utils/path_utils.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace winzox::io {

namespace fs = std::filesystem;

namespace {

fs::path ResolveFirstVolume(const fs::path& archivePath) {
    const std::string extension = winzox::utils::ToLower(archivePath.extension().string());
    if (extension == ".zox") {
        return archivePath;
    }

    if (winzox::utils::IsSplitZoxExtension(extension)) {
        fs::path firstVolume = archivePath;
        firstVolume.replace_extension(".zox");
        if (fs::exists(firstVolume)) {
            return firstVolume;
        }
    }

    return archivePath;
}

} // namespace

std::vector<uint8_t> ReadAllVolumes(const fs::path& archivePath) {
    const fs::path firstVolume = ResolveFirstVolume(archivePath);
    std::vector<uint8_t> data = ReadFileBytes(firstVolume);

    if (winzox::utils::ToLower(firstVolume.extension().string()) != ".zox") {
        return data;
    }

    for (int index = 1;; ++index) {
        std::ostringstream extension;
        extension << ".z" << std::setw(2) << std::setfill('0') << index;

        fs::path nextVolume = firstVolume;
        nextVolume.replace_extension(extension.str());
        if (!fs::exists(nextVolume)) {
            break;
        }

        const std::vector<uint8_t> chunk = ReadFileBytes(nextVolume);
        data.insert(data.end(), chunk.begin(), chunk.end());
    }

    return data;
}

} // namespace winzox::io
