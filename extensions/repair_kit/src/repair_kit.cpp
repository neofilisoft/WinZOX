#include "winzox/extensions/repair_kit/repair_kit_api.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

#ifndef WINZOX_REPAIR_KIT_API_VERSION
#define WINZOX_REPAIR_KIT_API_VERSION "2.11.1"
#endif

namespace fs = std::filesystem;

constexpr char kMagicWzox[] = "WZOX";
constexpr char kMagicZox4[] = "ZOX4";
constexpr char kMagicZox5[] = "ZOX5";
constexpr char kMagicZox6[] = "ZOX6";
constexpr char kMagicZip[] = "PK\x03\x04";

void WriteError(const std::string& message, char* errorBuffer, size_t errorBufferSize) {
    if (errorBuffer == nullptr || errorBufferSize == 0) {
        return;
    }

    const size_t copySize = std::min(message.size(), errorBufferSize - 1);
    std::memcpy(errorBuffer, message.data(), copySize);
    errorBuffer[copySize] = '\0';
}

void SetSuggestion(WinZOXRepairKitReport* report, const std::string& message) {
    const size_t maxSize = WINZOX_REPAIR_KIT_MESSAGE_SIZE - 1;
    const size_t copySize = std::min(message.size(), maxSize);
    std::memcpy(report->suggested_action, message.data(), copySize);
    report->suggested_action[copySize] = '\0';
}

bool EndsWithSplitExtension(const std::string& extension) {
    if (extension.size() != 4 || extension[0] != '.' || std::tolower(static_cast<unsigned char>(extension[1])) != 'z') {
        return false;
    }
    return std::isdigit(static_cast<unsigned char>(extension[2])) != 0 &&
           std::isdigit(static_cast<unsigned char>(extension[3])) != 0;
}

bool ReadMagic(const fs::path& path, std::array<char, 4>& magicOut) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.read(magicOut.data(), static_cast<std::streamsize>(magicOut.size()));
    return input.gcount() == static_cast<std::streamsize>(magicOut.size());
}

const char* MatchMagic(const std::array<char, 4>& magic) {
    if (std::memcmp(magic.data(), kMagicWzox, 4) == 0) return kMagicWzox;
    if (std::memcmp(magic.data(), kMagicZox4, 4) == 0) return kMagicZox4;
    if (std::memcmp(magic.data(), kMagicZox5, 4) == 0) return kMagicZox5;
    if (std::memcmp(magic.data(), kMagicZox6, 4) == 0) return kMagicZox6;
    if (std::memcmp(magic.data(), kMagicZip, 4) == 0) return "ZIP";
    return "";
}

} // namespace

extern "C" {

const char* winzox_repair_kit_api_version(void) {
    return WINZOX_REPAIR_KIT_API_VERSION;
}

WinZOXRepairKitStatus winzox_repair_kit_analyze_file(const char* archive_path,
                                                     WinZOXRepairKitReport* out_report,
                                                     char* error_buffer,
                                                     size_t error_buffer_size) {
    if (archive_path == nullptr || out_report == nullptr) {
        WriteError("Invalid repair kit arguments", error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_INVALID_ARGUMENT;
    }

    std::memset(out_report, 0, sizeof(*out_report));

    try {
        const fs::path path = fs::u8path(archive_path);
        out_report->file_exists = fs::exists(path) ? 1 : 0;
        if (!out_report->file_exists) {
            WriteError("Archive file does not exist", error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
        }

        out_report->file_size = fs::is_regular_file(path) ? static_cast<uint64_t>(fs::file_size(path)) : 0;
        const std::string extension = path.extension().u8string();
        out_report->is_split_volume = EndsWithSplitExtension(extension) ? 1 : 0;
        out_report->is_probably_truncated = out_report->file_size < 32 ? 1 : 0;

        std::array<char, 4> magic {};
        if (!ReadMagic(path, magic)) {
            WriteError("Failed to read archive header", error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
        }

        const char* matchedMagic = MatchMagic(magic);
        std::memset(out_report->detected_magic, 0, WINZOX_REPAIR_KIT_MAGIC_SIZE);
        std::memcpy(out_report->detected_magic, matchedMagic, std::min<size_t>(std::strlen(matchedMagic), WINZOX_REPAIR_KIT_MAGIC_SIZE - 1));

        out_report->format_supported = matchedMagic[0] != '\0' ? 1 : 0;
        if (!out_report->format_supported) {
            SetSuggestion(out_report, "Unsupported header. Verify the source archive and extension.");
            return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
        }

        if (out_report->is_probably_truncated) {
            SetSuggestion(out_report, "Archive is very small. Re-copy the file or locate missing split volumes.");
        } else if (out_report->is_split_volume) {
            SetSuggestion(out_report, "Detected split volume. Start repair from the first .zox volume.");
        } else {
            SetSuggestion(out_report, "Header looks valid. Run archive test/list before attempting extraction.");
        }

        return WINZOX_REPAIR_KIT_STATUS_OK;
    } catch (const std::exception& error) {
        WriteError(error.what(), error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
    }
}

} // extern "C"
