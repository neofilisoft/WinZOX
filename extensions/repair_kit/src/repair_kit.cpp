#include "winzox/extensions/repair_kit/repair_kit_api.h"

#include "archive/archive_integrity.hpp"
#include "io/volume_reader.hpp"
#include "utils/checksum.hpp"
#include "utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

#ifndef WINZOX_REPAIR_KIT_API_VERSION
#define WINZOX_REPAIR_KIT_API_VERSION "2.12.0"
#endif

namespace fs = std::filesystem;

constexpr char kMagicWzox[] = "WZOX";
constexpr char kMagicZox4[] = "ZOX4";
constexpr char kMagicZox5[] = "ZOX5";
constexpr char kMagicZox6[] = "ZOX6";
constexpr char kMagicZip[] = "PK\x03\x04";
constexpr char kFooterMagic[] = "ZCDR";
constexpr uint8_t kEncryptedFlag = 0x01;
constexpr uint8_t kSolidFlag = 0x02;
constexpr uint8_t kAuthenticatedFlag = 0x04;

struct CurrentHeader {
    std::array<char, 4> magic {};
    bool is_current_format = false;
    bool encrypted = false;
    bool solid = false;
    bool authenticated = false;
    bool integrity_sha512 = false;
    bool integrity_sha3_256 = false;
    uint8_t encryption_algorithm = 0;
    uint8_t default_algorithm = 0;
    uint64_t created_unix_time = 0;
    uint32_t payload_checksum = 0;
    std::string comment;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> iv_primary;
    std::vector<uint8_t> iv_secondary;
    uint32_t iterations = 0;
    uint64_t data_section_plain_size = 0;
    size_t data_offset = 0;
};

struct DirectoryFooterData {
    uint64_t central_directory_offset = 0;
    uint64_t central_directory_stored_size = 0;
    uint64_t central_directory_plain_size = 0;
    uint32_t central_directory_checksum = 0;
    uint32_t entry_count = 0;
    std::vector<uint8_t> sha512;
    std::vector<uint8_t> sha3_256;
    std::vector<uint8_t> authentication_tag;
};

struct DirectoryCandidate {
    size_t offset = 0;
    size_t end_offset = 0;
    uint32_t entry_count = 0;
    std::vector<uint8_t> bytes;
};

template <typename T>
T ReadValue(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + sizeof(T) > data.size()) {
        throw std::runtime_error("Archive is truncated");
    }

    T value {};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}

std::string ReadString(const std::vector<uint8_t>& data, size_t& offset, size_t length) {
    if (offset + length > data.size()) {
        throw std::runtime_error("Archive string field is truncated");
    }

    std::string value(reinterpret_cast<const char*>(data.data() + offset), length);
    offset += length;
    return value;
}

template <typename T>
void AppendValue(std::vector<uint8_t>& buffer, T value) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), raw, raw + sizeof(T));
}

void AppendBytes(std::vector<uint8_t>& buffer, const void* data, size_t size) {
    const uint8_t* raw = static_cast<const uint8_t*>(data);
    buffer.insert(buffer.end(), raw, raw + size);
}

void WriteError(const std::string& message, char* error_buffer, size_t error_buffer_size) {
    if (error_buffer == nullptr || error_buffer_size == 0) {
        return;
    }

    const size_t copy_size = std::min(message.size(), error_buffer_size - 1);
    std::memcpy(error_buffer, message.data(), copy_size);
    error_buffer[copy_size] = '\0';
}

void SetSuggestion(WinZOXRepairKitReport* report, const std::string& message) {
    const size_t max_size = WINZOX_REPAIR_KIT_MESSAGE_SIZE - 1;
    const size_t copy_size = std::min(message.size(), max_size);
    std::memcpy(report->suggested_action, message.data(), copy_size);
    report->suggested_action[copy_size] = '\0';
}

void ResetReport(WinZOXRepairKitReport* report) {
    std::memset(report, 0, sizeof(*report));
}

bool EndsWithSplitExtension(const std::string& extension) {
    if (extension.size() != 4 || extension[0] != '.' || std::tolower(static_cast<unsigned char>(extension[1])) != 'z') {
        return false;
    }
    return std::isdigit(static_cast<unsigned char>(extension[2])) != 0 &&
           std::isdigit(static_cast<unsigned char>(extension[3])) != 0;
}

bool ReadMagic(const fs::path& path, std::array<char, 4>& magic_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.read(magic_out.data(), static_cast<std::streamsize>(magic_out.size()));
    return input.gcount() == static_cast<std::streamsize>(magic_out.size());
}

const char* MatchMagic(const std::array<char, 4>& magic) {
    if (std::memcmp(magic.data(), kMagicWzox, 4) == 0) return kMagicWzox;
    if (std::memcmp(magic.data(), kMagicZox4, 4) == 0) return kMagicZox4;
    if (std::memcmp(magic.data(), kMagicZox5, 4) == 0) return kMagicZox5;
    if (std::memcmp(magic.data(), kMagicZox6, 4) == 0) return kMagicZox6;
    if (std::memcmp(magic.data(), kMagicZip, 4) == 0) return "ZIP";
    return "";
}

void SetDetectedMagic(WinZOXRepairKitReport* report, const char* magic) {
    std::memset(report->detected_magic, 0, WINZOX_REPAIR_KIT_MAGIC_SIZE);
    const size_t copy_size = std::min(std::strlen(magic), static_cast<size_t>(WINZOX_REPAIR_KIT_MAGIC_SIZE - 1));
    std::memcpy(report->detected_magic, magic, copy_size);
}

size_t FooterSize(const CurrentHeader& header) {
    return 4 + sizeof(uint64_t) * 3 + sizeof(uint32_t) * 2 +
           (header.integrity_sha512 ? winzox::archive::integrity::kSha512DigestSize : 0) +
           (header.integrity_sha3_256 ? winzox::archive::integrity::kSha3_256DigestSize : 0) +
           (header.authenticated ? 32 : 0);
}

bool TryParseCurrentHeader(const std::vector<uint8_t>& raw, CurrentHeader& header, std::string& error) {
    if (raw.size() < 4) {
        error = "Archive is too small";
        return false;
    }

    std::memcpy(header.magic.data(), raw.data(), 4);
    const bool is_current_format = std::memcmp(raw.data(), kMagicWzox, 4) == 0;
    const bool is_v6 = std::memcmp(raw.data(), kMagicZox6, 4) == 0;
    const bool is_v5 = std::memcmp(raw.data(), kMagicZox5, 4) == 0;
    if (!is_current_format && !is_v6 && !is_v5) {
        error = "Unsupported archive format for repair";
        return false;
    }

    size_t offset = 4;
    try {
        header.is_current_format = is_current_format;
        const uint8_t flags = ReadValue<uint8_t>(raw, offset);
        header.encrypted = (flags & kEncryptedFlag) != 0;
        header.solid = (flags & kSolidFlag) != 0;
        header.authenticated = (flags & kAuthenticatedFlag) != 0;
        header.integrity_sha512 = is_current_format;
        header.integrity_sha3_256 = is_current_format;
        header.encryption_algorithm = ReadValue<uint8_t>(raw, offset);
        header.default_algorithm = ReadValue<uint8_t>(raw, offset);
        header.created_unix_time = ReadValue<uint64_t>(raw, offset);
        header.payload_checksum = ReadValue<uint32_t>(raw, offset);

        const uint32_t comment_length = ReadValue<uint32_t>(raw, offset);
        header.comment = ReadString(raw, offset, comment_length);

        if (header.encrypted) {
            header.salt.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + 16));
            offset += 16;
            header.iv_primary.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + 16));
            offset += 16;
            if (header.encryption_algorithm == 2) {
                header.iv_secondary.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + 16));
                offset += 16;
            }
            if (is_current_format) {
                header.iterations = ReadValue<uint32_t>(raw, offset);
            }
        }

        if (header.solid) {
            header.data_section_plain_size = ReadValue<uint64_t>(raw, offset);
        }

        header.data_offset = offset;
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool TryParseDirectoryBytes(const std::vector<uint8_t>& directory_bytes,
                            uint64_t data_section_stored_size,
                            bool solid_archive,
                            uint32_t expected_checksum,
                            uint32_t& entry_count,
                            std::string& error) {
    if (directory_bytes.size() < sizeof(uint32_t)) {
        error = "Central directory is too small";
        return false;
    }

    if (winzox::utils::ComputeCrc32(directory_bytes) != expected_checksum) {
        error = "Central directory checksum mismatch";
        return false;
    }

    auto parse_variant = [&](bool with_encoded_size) -> bool {
        size_t cursor = 0;
        const uint32_t declared_count = ReadValue<uint32_t>(directory_bytes, cursor);
        if (declared_count == 0) {
            error = "Central directory contains no entries";
            return false;
        }
        if (declared_count > 1000000u) {
            error = "Central directory entry count is implausibly large";
            return false;
        }

        for (uint32_t index = 0; index < declared_count; ++index) {
            const uint16_t path_length = ReadValue<uint16_t>(directory_bytes, cursor);
            const std::string path = ReadString(directory_bytes, cursor, path_length);
            if (path.empty()) {
                error = "Central directory contains an empty path";
                return false;
            }

            static_cast<void>(ReadValue<uint8_t>(directory_bytes, cursor));
            static_cast<void>(ReadValue<uint64_t>(directory_bytes, cursor));
            const uint64_t stored_size = ReadValue<uint64_t>(directory_bytes, cursor);
            const uint64_t encoded_size = with_encoded_size ? ReadValue<uint64_t>(directory_bytes, cursor) : stored_size;
            static_cast<void>(ReadValue<uint32_t>(directory_bytes, cursor));
            const uint64_t data_offset = ReadValue<uint64_t>(directory_bytes, cursor);

            if (!solid_archive && data_offset + encoded_size > data_section_stored_size) {
                error = "Central directory references data outside the archive payload";
                return false;
            }
        }

        if (cursor != directory_bytes.size()) {
            error = "Central directory has trailing bytes";
            return false;
        }

        entry_count = declared_count;
        return true;
    };

    try {
        if (parse_variant(true)) {
            return true;
        }
    } catch (const std::exception&) {
    }

    try {
        return parse_variant(false);
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool TryReadExistingFooter(const std::vector<uint8_t>& raw,
                           const CurrentHeader& header,
                           DirectoryFooterData& footer,
                           std::string& error) {
    const size_t footer_size = FooterSize(header);
    if (raw.size() < footer_size) {
        error = "Archive is smaller than the expected footer";
        return false;
    }

    size_t offset = raw.size() - footer_size;
    if (std::memcmp(raw.data() + offset, kFooterMagic, 4) != 0) {
        error = "Footer magic is missing";
        return false;
    }
    offset += 4;

    try {
        footer.central_directory_offset = ReadValue<uint64_t>(raw, offset);
        footer.central_directory_stored_size = ReadValue<uint64_t>(raw, offset);
        footer.central_directory_plain_size = ReadValue<uint64_t>(raw, offset);
        footer.central_directory_checksum = ReadValue<uint32_t>(raw, offset);
        footer.entry_count = ReadValue<uint32_t>(raw, offset);
        if (header.integrity_sha512) {
            footer.sha512.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                 raw.begin() + static_cast<std::ptrdiff_t>(offset + winzox::archive::integrity::kSha512DigestSize));
            offset += winzox::archive::integrity::kSha512DigestSize;
        }
        if (header.integrity_sha3_256) {
            footer.sha3_256.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                   raw.begin() + static_cast<std::ptrdiff_t>(offset + winzox::archive::integrity::kSha3_256DigestSize));
            offset += winzox::archive::integrity::kSha3_256DigestSize;
        }
        if (header.authenticated) {
            footer.authentication_tag.assign(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                             raw.begin() + static_cast<std::ptrdiff_t>(offset + 32));
            offset += 32;
        }
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }

    if (offset != raw.size()) {
        error = "Footer metadata is inconsistent";
        return false;
    }

    return true;
}

bool VerifyCurrentIntegrity(const std::vector<uint8_t>& raw,
                            const CurrentHeader& header,
                            const DirectoryFooterData& footer,
                            std::string& error) {
    if (!header.integrity_sha512 && !header.integrity_sha3_256) {
        return true;
    }

    const size_t digest_bytes =
        (header.integrity_sha512 ? footer.sha512.size() : 0) +
        (header.integrity_sha3_256 ? footer.sha3_256.size() : 0);
    const size_t authentication_bytes = header.authenticated ? footer.authentication_tag.size() : 0;
    if (raw.size() < digest_bytes + authentication_bytes) {
        error = "Archive integrity metadata is truncated";
        return false;
    }

    const size_t digested_size = raw.size() - digest_bytes - authentication_bytes;
    const auto computed = winzox::archive::integrity::ComputeArchiveIntegrityDigests(raw.data(), digested_size);
    if (header.integrity_sha512 &&
        (footer.sha512.size() != winzox::archive::integrity::kSha512DigestSize ||
         !winzox::archive::integrity::DigestsEqual(footer.sha512.data(), computed.sha512.data(), footer.sha512.size()))) {
        error = "SHA-512 integrity digest mismatch";
        return false;
    }
    if (header.integrity_sha3_256 &&
        (footer.sha3_256.size() != winzox::archive::integrity::kSha3_256DigestSize ||
         !winzox::archive::integrity::DigestsEqual(footer.sha3_256.data(), computed.sha3_256.data(), footer.sha3_256.size()))) {
        error = "SHA3-256 integrity digest mismatch";
        return false;
    }

    return true;
}

bool TryFindDirectoryCandidate(const std::vector<uint8_t>& raw,
                               const CurrentHeader& header,
                               size_t directory_end,
                               DirectoryCandidate& candidate,
                               std::string& error) {
    if (directory_end <= header.data_offset || directory_end > raw.size()) {
        error = "Directory scan range is invalid";
        return false;
    }

    for (size_t offset = header.data_offset; offset + sizeof(uint32_t) <= directory_end; ++offset) {
        const size_t directory_size = directory_end - offset;
        std::vector<uint8_t> directory_bytes(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                                             raw.begin() + static_cast<std::ptrdiff_t>(directory_end));

        uint32_t entry_count = 0;
        std::string parse_error;
        const uint64_t data_section_stored_size = static_cast<uint64_t>(offset - header.data_offset);
        if (!TryParseDirectoryBytes(directory_bytes,
                                    data_section_stored_size,
                                    header.solid,
                                    header.payload_checksum,
                                    entry_count,
                                    parse_error)) {
            continue;
        }

        candidate.offset = offset;
        candidate.end_offset = directory_end;
        candidate.entry_count = entry_count;
        candidate.bytes = std::move(directory_bytes);
        return true;
    }

    error = "Unable to locate a recoverable central directory";
    return false;
}

bool TryRecoverDirectory(const std::vector<uint8_t>& raw,
                         const CurrentHeader& header,
                         DirectoryCandidate& candidate,
                         bool& ignored_existing_footer,
                         std::string& error) {
    ignored_existing_footer = false;

    const size_t footer_size = FooterSize(header);
    if (raw.size() > footer_size) {
        DirectoryCandidate with_footer_removed;
        std::string scan_error;
        if (TryFindDirectoryCandidate(raw, header, raw.size() - footer_size, with_footer_removed, scan_error)) {
            candidate = std::move(with_footer_removed);
            ignored_existing_footer = true;
            return true;
        }
    }

    return TryFindDirectoryCandidate(raw, header, raw.size(), candidate, error);
}

std::vector<uint8_t> BuildFooter(const CurrentHeader& header,
                                 const DirectoryCandidate& candidate,
                                 const std::vector<uint8_t>& repaired_prefix) {
    std::vector<uint8_t> footer;
    AppendBytes(footer, kFooterMagic, 4);

    const uint64_t offset = static_cast<uint64_t>(candidate.offset);
    const uint64_t stored_size = static_cast<uint64_t>(candidate.bytes.size());
    const uint64_t plain_size = stored_size;
    const uint32_t checksum = header.payload_checksum;
    const uint32_t entry_count = candidate.entry_count;

    AppendValue<uint64_t>(footer, offset);
    AppendValue<uint64_t>(footer, stored_size);
    AppendValue<uint64_t>(footer, plain_size);
    AppendValue<uint32_t>(footer, checksum);
    AppendValue<uint32_t>(footer, entry_count);

    if (header.integrity_sha512 || header.integrity_sha3_256) {
        std::vector<uint8_t> digested_data = repaired_prefix;
        digested_data.insert(digested_data.end(), footer.begin(), footer.end());
        const auto digests = winzox::archive::integrity::ComputeArchiveIntegrityDigests(digested_data.data(), digested_data.size());
        if (header.integrity_sha512) {
            AppendBytes(footer, digests.sha512.data(), digests.sha512.size());
        }
        if (header.integrity_sha3_256) {
            AppendBytes(footer, digests.sha3_256.data(), digests.sha3_256.size());
        }
    }

    return footer;
}

bool WriteFileBytes(const fs::path& path, const std::vector<uint8_t>& data, std::string& error) {
    std::error_code ec;
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            error = "Failed to create output directory: " + parent.string();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Failed to open output file: " + path.string();
        return false;
    }

    if (!data.empty()) {
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!output) {
            error = "Failed to write repaired archive: " + path.string();
            return false;
        }
    }

    return true;
}

WinZOXRepairKitStatus AnalyzeArchive(const fs::path& path,
                                     const std::vector<uint8_t>& raw,
                                     WinZOXRepairKitReport* report,
                                     char* error_buffer,
                                     size_t error_buffer_size) {
    report->file_exists = fs::exists(path) ? 1 : 0;
    if (!report->file_exists) {
        WriteError("Archive file does not exist", error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
    }

    report->file_size = fs::is_regular_file(path) ? static_cast<uint64_t>(fs::file_size(path)) : 0;
    const std::string extension = winzox::utils::PathToUtf8(path.extension());
    report->is_split_volume = EndsWithSplitExtension(extension) ? 1 : 0;
    report->is_probably_truncated = raw.size() < 32 ? 1 : 0;

    std::array<char, 4> magic {};
    if (!ReadMagic(path, magic)) {
        WriteError("Failed to read archive header", error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
    }

    const char* matched_magic = MatchMagic(magic);
    SetDetectedMagic(report, matched_magic);
    report->format_supported = matched_magic[0] != '\0' ? 1 : 0;
    if (!report->format_supported) {
        SetSuggestion(report, "Unsupported header. Verify the source archive and extension.");
        return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
    }

    if (std::strcmp(matched_magic, "ZIP") == 0) {
        SetSuggestion(report, "ZIP archives are not handled by this repair engine.");
        return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
    }

    if (std::strcmp(matched_magic, kMagicZox4) == 0) {
        SetSuggestion(report, "Legacy ZOX4 archives are not repairable with this module yet.");
        return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
    }

    CurrentHeader header;
    std::string header_error;
    if (!TryParseCurrentHeader(raw, header, header_error)) {
        WriteError(header_error, error_buffer, error_buffer_size);
        SetSuggestion(report, "Header is damaged beyond footer-only repair.");
        return WINZOX_REPAIR_KIT_STATUS_NOT_REPAIRABLE;
    }

    if (header.encrypted || header.authenticated) {
        SetSuggestion(report, "Encrypted/authenticated archives need full cryptographic rebuild support and are not repairable yet.");
        return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
    }

    report->can_attempt_repair = 1;

    DirectoryCandidate candidate;
    bool ignored_existing_footer = false;
    std::string candidate_error;
    if (TryRecoverDirectory(raw, header, candidate, ignored_existing_footer, candidate_error)) {
        report->recovered_entry_count = candidate.entry_count;
        report->rebuilt_footer = 1;
        if (ignored_existing_footer) {
            SetSuggestion(report, "Recoverable central directory found. Rebuild the footer and write a repaired archive.");
        } else {
            SetSuggestion(report, "Central directory found at file tail. Rebuild footer/digests into a repaired archive.");
        }
        return WINZOX_REPAIR_KIT_STATUS_OK;
    }

    SetSuggestion(report, "No recoverable central directory was found. This archive cannot be repaired automatically.");
    WriteError(candidate_error, error_buffer, error_buffer_size);
    return WINZOX_REPAIR_KIT_STATUS_NOT_REPAIRABLE;
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

    ResetReport(out_report);

    try {
        const fs::path path = fs::u8path(archive_path);
        if (!fs::exists(path)) {
            out_report->file_exists = 0;
            WriteError("Archive file does not exist", error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
        }

        const std::vector<uint8_t> raw = winzox::io::ReadAllVolumes(path);
        return AnalyzeArchive(path, raw, out_report, error_buffer, error_buffer_size);
    } catch (const std::exception& error) {
        WriteError(error.what(), error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
    }
}

WinZOXRepairKitStatus winzox_repair_kit_repair_file(const char* archive_path,
                                                    const char* output_path,
                                                    const char* password,
                                                    WinZOXRepairKitReport* out_report,
                                                    char* error_buffer,
                                                    size_t error_buffer_size) {
    if (archive_path == nullptr || output_path == nullptr || out_report == nullptr) {
        WriteError("Invalid repair kit arguments", error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_INVALID_ARGUMENT;
    }

    ResetReport(out_report);

    try {
        const fs::path input = fs::u8path(archive_path);
        const fs::path output = fs::u8path(output_path);
        const std::string password_value = password != nullptr ? password : "";
        if (!password_value.empty()) {
            WriteError("Encrypted archive repair is not supported yet", error_buffer, error_buffer_size);
            SetSuggestion(out_report, "Retry without a password only for unencrypted archives, or extend the repair engine for encrypted rebuilds.");
            return WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED;
        }

        const std::vector<uint8_t> raw = winzox::io::ReadAllVolumes(input);
        WinZOXRepairKitStatus analyze_status = AnalyzeArchive(input, raw, out_report, error_buffer, error_buffer_size);
        if (analyze_status != WINZOX_REPAIR_KIT_STATUS_OK) {
            return analyze_status;
        }

        CurrentHeader header;
        std::string header_error;
        if (!TryParseCurrentHeader(raw, header, header_error)) {
            WriteError(header_error, error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_NOT_REPAIRABLE;
        }

        DirectoryCandidate candidate;
        bool ignored_existing_footer = false;
        std::string candidate_error;
        if (!TryRecoverDirectory(raw, header, candidate, ignored_existing_footer, candidate_error)) {
            WriteError(candidate_error, error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_NOT_REPAIRABLE;
        }

        std::vector<uint8_t> repaired_prefix(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(candidate.end_offset));
        std::vector<uint8_t> repaired_bytes = repaired_prefix;
        const std::vector<uint8_t> footer = BuildFooter(header, candidate, repaired_prefix);
        repaired_bytes.insert(repaired_bytes.end(), footer.begin(), footer.end());

        if (header.is_current_format) {
            DirectoryFooterData footer_data;
            std::string footer_error;
            if (!TryReadExistingFooter(repaired_bytes, header, footer_data, footer_error) ||
                !VerifyCurrentIntegrity(repaired_bytes, header, footer_data, footer_error)) {
                WriteError("Rebuilt archive failed integrity verification", error_buffer, error_buffer_size);
                return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
            }
        }

        std::string write_error;
        if (!WriteFileBytes(output, repaired_bytes, write_error)) {
            WriteError(write_error, error_buffer, error_buffer_size);
            return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
        }

        out_report->repaired = 1;
        out_report->rebuilt_footer = 1;
        out_report->recovered_entry_count = candidate.entry_count;
        out_report->recovered_output_size = static_cast<uint64_t>(repaired_bytes.size());
        SetSuggestion(out_report, ignored_existing_footer
            ? "Repaired archive written with a rebuilt footer and preserved payload data."
            : "Repaired archive written by restoring the missing footer metadata.");
        return WINZOX_REPAIR_KIT_STATUS_REPAIRED;
    } catch (const std::exception& error) {
        WriteError(error.what(), error_buffer, error_buffer_size);
        return WINZOX_REPAIR_KIT_STATUS_IO_ERROR;
    }
}

} // extern "C"
