#include "extraction/extractor.hpp"

#include "archive/archive.hpp"
#include "compression/compressor.hpp"
#include "io/file_writer.hpp"
#include "utils/checksum.hpp"
#include "utils/path_utils.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace zipbox::extraction {

namespace fs = std::filesystem;

namespace {

void ReportProgress(const utils::ProgressCallback& progressCallback,
                    uint64_t completedUnits,
                    uint64_t totalUnits,
                    const std::string& currentItem,
                    const std::string& statusText) {
    if (!progressCallback) {
        return;
    }

    if (!progressCallback(utils::ProgressInfo { completedUnits, totalUnits, currentItem, statusText })) {
        throw std::runtime_error("Operation canceled");
    }
}

uint64_t CalculateTotalEntryUnits(const std::vector<zipbox::archive::ArchiveEntryInfo>& entries) {
    uint64_t totalUnits = 0;
    for (const auto& entry : entries) {
        const uint64_t increment = entry.originalSize > 0 ? entry.originalSize : 1;
        if (totalUnits > (std::numeric_limits<uint64_t>::max)() - increment) {
            throw std::runtime_error("Archive is too large");
        }
        totalUnits += increment;
    }
    return totalUnits;
}

std::vector<zipbox::archive::ArchiveEntryInfo> ListGenericArchiveEntries(const std::string& filename) {
    struct archive* reader = archive_read_new();
    if (reader == nullptr) {
        throw std::runtime_error("Failed to allocate libarchive reader");
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(reader, filename.c_str(), 10240) != ARCHIVE_OK) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_read_free(reader);
        throw std::runtime_error("Failed to open archive: " + message);
    }

    std::vector<zipbox::archive::ArchiveEntryInfo> entries;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        zipbox::archive::ArchiveEntryInfo info;
        info.path = archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "";
        const la_int64_t size = archive_entry_size(entry);
        info.originalSize = size > 0 ? static_cast<uint64_t>(size) : 0;
        info.storedSize = info.originalSize;
        info.encodedSize = info.storedSize;
        entries.push_back(std::move(info));
        archive_read_data_skip(reader);
    }

    archive_read_close(reader);
    archive_read_free(reader);
    return entries;
}

void ExtractGenericArchive(const std::string& filename,
                          const std::string& destination,
                          const utils::ProgressCallback& progressCallback) {
    const std::vector<zipbox::archive::ArchiveEntryInfo> entries = ListGenericArchiveEntries(filename);
    const uint64_t totalUnits = CalculateTotalEntryUnits(entries);
    uint64_t completedUnits = 0;

    struct archive* reader = archive_read_new();
    if (reader == nullptr) {
        throw std::runtime_error("Failed to allocate libarchive reader");
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    struct archive* writer = archive_write_disk_new();
    if (writer == nullptr) {
        archive_read_free(reader);
        throw std::runtime_error("Failed to allocate libarchive writer");
    }

    archive_write_disk_set_options(writer,
                                   ARCHIVE_EXTRACT_TIME |
                                   ARCHIVE_EXTRACT_PERM |
                                   ARCHIVE_EXTRACT_ACL |
                                   ARCHIVE_EXTRACT_FFLAGS);

    if (archive_read_open_filename(reader, filename.c_str(), 10240) != ARCHIVE_OK) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_write_free(writer);
        archive_read_free(reader);
        throw std::runtime_error("Failed to open archive: " + message);
    }

    fs::create_directories(fs::path(destination));
    ReportProgress(progressCallback, 0, totalUnits, "", "Preparing extraction");

    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const std::string currentFile = archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "";
        const fs::path outputPath = zipbox::utils::ResolveSafeOutputPath(fs::path(destination), currentFile);
        archive_entry_set_pathname(entry, outputPath.string().c_str());

        if (archive_write_header(writer, entry) < ARCHIVE_OK) {
            const std::string message = archive_error_string(writer) ? archive_error_string(writer) : "unknown libarchive error";
            archive_write_free(writer);
            archive_read_free(reader);
            throw std::runtime_error(message);
        }

        const void* buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        bool consumedAnyData = false;
        while ((result = archive_read_data_block(reader, &buffer, &size, &offset)) == ARCHIVE_OK) {
            if (archive_write_data_block(writer, buffer, size, offset) < ARCHIVE_OK) {
                const std::string message = archive_error_string(writer) ? archive_error_string(writer) : "unknown libarchive error";
                archive_write_free(writer);
                archive_read_free(reader);
                throw std::runtime_error(message);
            }

            consumedAnyData = true;
            completedUnits += static_cast<uint64_t>(size);
            ReportProgress(progressCallback, completedUnits, totalUnits, currentFile, "Extracting");
        }

        if (result != ARCHIVE_EOF) {
            const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
            archive_write_free(writer);
            archive_read_free(reader);
            throw std::runtime_error(message);
        }

        archive_write_finish_entry(writer);
        if (!consumedAnyData) {
            ++completedUnits;
            ReportProgress(progressCallback, completedUnits, totalUnits, currentFile, "Extracting");
        }
    }

    if (result != ARCHIVE_EOF) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_write_free(writer);
        archive_read_free(reader);
        throw std::runtime_error(message);
    }

    archive_write_close(writer);
    archive_write_free(writer);
    archive_read_close(reader);
    archive_read_free(reader);
    ReportProgress(progressCallback, 1, 1, fs::path(filename).filename().u8string(), "Extraction complete");
}

void TestGenericArchive(const std::string& filename) {
    struct archive* reader = archive_read_new();
    if (reader == nullptr) {
        throw std::runtime_error("Failed to allocate libarchive reader");
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(reader, filename.c_str(), 10240) != ARCHIVE_OK) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_read_free(reader);
        throw std::runtime_error("Failed to open archive: " + message);
    }

    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const void* buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while ((result = archive_read_data_block(reader, &buffer, &size, &offset)) == ARCHIVE_OK) {
        }

        if (result != ARCHIVE_EOF) {
            const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
            archive_read_free(reader);
            throw std::runtime_error(message);
        }
    }

    if (result != ARCHIVE_EOF) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_read_free(reader);
        throw std::runtime_error(message);
    }

    archive_read_close(reader);
    archive_read_free(reader);
}

std::vector<uint8_t> ReadGenericArchiveEntry(const std::string& filename, size_t entryIndex) {
    struct archive* reader = archive_read_new();
    if (reader == nullptr) {
        throw std::runtime_error("Failed to allocate libarchive reader");
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(reader, filename.c_str(), 10240) != ARCHIVE_OK) {
        const std::string message = archive_error_string(reader) ? archive_error_string(reader) : "unknown libarchive error";
        archive_read_free(reader);
        throw std::runtime_error("Failed to open archive: " + message);
    }

    archive_entry* entry = nullptr;
    size_t currentIndex = 0;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        if (currentIndex == entryIndex) {
            const la_int64_t declaredSize = archive_entry_size(entry);
            std::vector<uint8_t> data;
            if (declaredSize > 0) {
                data.reserve(static_cast<size_t>(declaredSize));
            }

            const void* buffer = nullptr;
            size_t size = 0;
            la_int64_t offset = 0;
            while ((result = archive_read_data_block(reader, &buffer, &size, &offset)) == ARCHIVE_OK) {
                const auto* bytes = static_cast<const uint8_t*>(buffer);
                data.insert(data.end(), bytes, bytes + size);
            }

            const std::string errorMessage =
                (result != ARCHIVE_EOF && archive_error_string(reader) != nullptr)
                ? archive_error_string(reader)
                : std::string();
            archive_read_close(reader);
            archive_read_free(reader);

            if (result != ARCHIVE_EOF) {
                throw std::runtime_error(errorMessage.empty() ? "unknown libarchive error" : errorMessage);
            }

            return data;
        }

        ++currentIndex;
        archive_read_data_skip(reader);
    }

    archive_read_close(reader);
    archive_read_free(reader);

    if (result != ARCHIVE_EOF) {
        throw std::runtime_error("Failed while reading archive entries");
    }

    throw std::runtime_error("Selected archive entry is out of range");
}

} // namespace

void ExtractArchive(const std::string& filename,
                    const std::string& destination,
                    const std::string& password,
                    const utils::ProgressCallback& progressCallback) {
    if (zipbox::archive::LooksLikeZoxArchive(filename)) {
        const zipbox::archive::ArchiveContents contents = zipbox::archive::ReadArchive(filename, password);
        const fs::path destinationRoot(destination);
        fs::create_directories(destinationRoot);
        std::vector<zipbox::archive::ArchiveEntryInfo> entryInfos;
        entryInfos.reserve(contents.entries.size());
        for (const auto& entry : contents.entries) {
            entryInfos.push_back(entry.info);
        }
        const uint64_t totalUnits = CalculateTotalEntryUnits(entryInfos);
        uint64_t completedUnits = 0;

        ReportProgress(progressCallback, 0, totalUnits, "", "Preparing extraction");

        for (const auto& entry : contents.entries) {
            const std::vector<uint8_t> plain = zipbox::compression::DecompressBuffer(
                entry.storedData,
                entry.info.algorithm,
                entry.info.originalSize);
            if (zipbox::utils::ComputeCrc32(plain) != entry.info.crc32) {
                throw std::runtime_error("CRC32 mismatch for entry: " + entry.info.path);
            }

            const fs::path outputPath = zipbox::utils::ResolveSafeOutputPath(destinationRoot, entry.info.path);
            zipbox::io::WriteFileBytes(outputPath, plain);
            completedUnits += entry.info.originalSize > 0 ? entry.info.originalSize : 1;
            ReportProgress(progressCallback, completedUnits, totalUnits, entry.info.path, "Extracting");
        }
        ReportProgress(progressCallback, 1, 1, fs::path(filename).filename().u8string(), "Extraction complete");
        return;
    }

    if (!password.empty()) {
        throw std::runtime_error("Passwords are only supported for .zox archives in this version");
    }

    ExtractGenericArchive(filename, destination, progressCallback);
}

std::vector<zipbox::archive::ArchiveEntryInfo> ListArchiveEntries(const std::string& filename,
                                                                  const std::string& password) {
    if (zipbox::archive::LooksLikeZoxArchive(filename)) {
        return zipbox::archive::ReadArchiveIndex(filename, password);
    }

    if (!password.empty()) {
        throw std::runtime_error("Passwords are only supported for .zox archives in this version");
    }

    return ListGenericArchiveEntries(filename);
}

zipbox::archive::ArchiveMetadata GetArchiveMetadata(const std::string& filename, const std::string& password) {
    if (zipbox::archive::LooksLikeZoxArchive(filename)) {
        (void)password;
        return zipbox::archive::ReadArchiveMetadata(filename);
    }

    if (!password.empty()) {
        throw std::runtime_error("Passwords are only supported for .zox archives in this version");
    }

    return {};
}

std::vector<uint8_t> ReadArchiveEntry(const std::string& filename,
                                      size_t entryIndex,
                                      const std::string& password) {
    if (zipbox::archive::LooksLikeZoxArchive(filename)) {
        const zipbox::archive::ArchiveContents contents = zipbox::archive::ReadArchive(filename, password);
        if (entryIndex >= contents.entries.size()) {
            throw std::runtime_error("Selected archive entry is out of range");
        }

        const auto& entry = contents.entries[entryIndex];
        const std::vector<uint8_t> plain = zipbox::compression::DecompressBuffer(
            entry.storedData,
            entry.info.algorithm,
            entry.info.originalSize);
        if (zipbox::utils::ComputeCrc32(plain) != entry.info.crc32) {
            throw std::runtime_error("CRC32 mismatch for entry: " + entry.info.path);
        }
        return plain;
    }

    if (!password.empty()) {
        throw std::runtime_error("Passwords are only supported for .zox archives in this version");
    }

    return ReadGenericArchiveEntry(filename, entryIndex);
}

void TestArchive(const std::string& filename, const std::string& password) {
    if (zipbox::archive::LooksLikeZoxArchive(filename)) {
        const zipbox::archive::ArchiveContents contents = zipbox::archive::ReadArchive(filename, password);
        for (const auto& entry : contents.entries) {
            const std::vector<uint8_t> plain = zipbox::compression::DecompressBuffer(
                entry.storedData,
                entry.info.algorithm,
                entry.info.originalSize);
            if (zipbox::utils::ComputeCrc32(plain) != entry.info.crc32) {
                throw std::runtime_error("CRC32 mismatch for entry: " + entry.info.path);
            }
        }
        return;
    }

    if (!password.empty()) {
        throw std::runtime_error("Passwords are only supported for .zox archives in this version");
    }

    TestGenericArchive(filename);
}

} // namespace zipbox::extraction
