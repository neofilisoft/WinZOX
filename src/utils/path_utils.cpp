#include "utils/path_utils.hpp"

#include <cctype>
#include <stdexcept>

namespace winzox::utils {

std::string ToLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool IsSplitZoxExtension(const std::string& extension) {
    if (extension.size() < 3 || extension[0] != '.' || extension[1] != 'z') {
        return false;
    }

    for (size_t index = 2; index < extension.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(extension[index]))) {
            return false;
        }
    }

    return true;
}

std::filesystem::path ResolveSafeOutputPath(const std::filesystem::path& destinationRoot,
                                            const std::string& entryPath) {
    std::filesystem::path relative(entryPath);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        throw std::runtime_error("Archive contains an absolute path: " + entryPath);
    }

    std::filesystem::path cleaned;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            throw std::runtime_error("Archive entry escapes the destination directory: " + entryPath);
        }
        cleaned /= component;
    }

    if (cleaned.empty()) {
        throw std::runtime_error("Archive contains an empty path");
    }

    return destinationRoot / cleaned;
}

} // namespace winzox::utils
