#include "utils/path_utils.hpp"

#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace winzox::utils {

namespace fs = std::filesystem;

namespace {

bool ContainsAlternateDataStream(const fs::path& path) {
#ifdef _WIN32
    bool firstComponent = true;
    for (const auto& component : path) {
        const std::wstring value = component.native();
        if (value.empty() || value == L"." || value == L"..") {
            firstComponent = false;
            continue;
        }
        if (firstComponent && component == path.root_name()) {
            firstComponent = false;
            continue;
        }
        if (value.find(L':') != std::wstring::npos) {
            return true;
        }
        firstComponent = false;
    }
#else
    (void)path;
#endif
    return false;
}

bool IsPathWithinRoot(const fs::path& canonicalRoot, const fs::path& canonicalCandidate) {
    auto rootIt = canonicalRoot.begin();
    auto candidateIt = canonicalCandidate.begin();
    for (; rootIt != canonicalRoot.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == canonicalCandidate.end() || *rootIt != *candidateIt) {
            return false;
        }
    }
    return true;
}

} // namespace

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

fs::path EnsurePathWithinRoot(const fs::path& destinationRoot,
                              const fs::path& candidatePath,
                              const std::string& sourcePath) {
    const fs::path canonicalRoot = fs::weakly_canonical(destinationRoot);
    const fs::path absoluteCandidate = candidatePath.is_absolute()
        ? candidatePath
        : destinationRoot / candidatePath;
    const fs::path canonicalCandidate = fs::weakly_canonical(absoluteCandidate);

    if (!IsPathWithinRoot(canonicalRoot, canonicalCandidate)) {
        const std::string label = sourcePath.empty() ? candidatePath.u8string() : sourcePath;
        throw std::runtime_error("Archive entry escapes the destination directory: " + label);
    }

    return absoluteCandidate;
}

fs::path ResolveSafeOutputPath(const fs::path& destinationRoot,
                               const std::string& entryPath) {
    fs::path relative(entryPath);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        throw std::runtime_error("Archive contains an absolute path: " + entryPath);
    }
    if (ContainsAlternateDataStream(relative)) {
        throw std::runtime_error("Archive contains an NTFS alternate data stream path: " + entryPath);
    }

    fs::path cleaned;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            throw std::runtime_error("Archive entry escapes the destination directory: " + entryPath);
        }
        if (ContainsAlternateDataStream(component)) {
            throw std::runtime_error("Archive contains an NTFS alternate data stream path: " + entryPath);
        }
        cleaned /= component;
    }

    if (cleaned.empty()) {
        throw std::runtime_error("Archive contains an empty path");
    }

    return EnsurePathWithinRoot(destinationRoot, destinationRoot / cleaned, entryPath);
}

} // namespace winzox::utils
