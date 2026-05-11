#include "io/file_reader.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace winzox::io {

namespace {

bool IsRegularFileNoFollow(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
    if (ec) {
        return false;
    }
    return std::filesystem::is_regular_file(status);
}

bool IsSymlink(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
    if (ec) {
        return false;
    }
    return std::filesystem::is_symlink(status);
}

} // namespace

std::vector<std::filesystem::path> CollectInputFiles(const std::filesystem::path& inputPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(fs::symlink_status(inputPath))) {
        throw std::runtime_error("Input path does not exist: " + inputPath.string());
    }

    if (IsSymlink(inputPath)) {
        throw std::runtime_error(
            "Input path is a symbolic link; refusing to follow for safety: " + inputPath.string());
    }

    std::vector<fs::path> files;
    if (IsRegularFileNoFollow(inputPath)) {
        files.push_back(inputPath);
    } else if (fs::is_directory(fs::symlink_status(inputPath))) {
        const auto options = fs::directory_options::skip_permission_denied;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(inputPath, options, ec);
             it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                std::cerr << "[warn] skipping path due to error: " << ec.message() << "\n";
                ec.clear();
                continue;
            }
            const fs::path& candidate = it->path();
            const fs::file_status status = it->symlink_status(ec);
            if (ec) {
                ec.clear();
                continue;
            }
            if (fs::is_symlink(status)) {
                std::cerr << "[warn] skipping symbolic link: " << candidate.string() << "\n";
                it.disable_recursion_pending();
                continue;
            }
            if (fs::is_regular_file(status)) {
                files.push_back(candidate);
            }
        }
    } else {
        throw std::runtime_error("Input path is neither a file nor a directory");
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    if (IsSymlink(path)) {
        throw std::runtime_error("Refusing to read through symbolic link: " + path.string());
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    const std::streamsize rawSize = input.tellg();
    if (rawSize < 0) {
        throw std::runtime_error("Failed to determine file size: " + path.string());
    }

    const size_t size = static_cast<size_t>(rawSize);
    std::vector<uint8_t> data(size);
    input.seekg(0, std::ios::beg);
    if (size > 0) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!input) {
            throw std::runtime_error("Failed to read file: " + path.string());
        }
    }

    return data;
}

} // namespace winzox::io
