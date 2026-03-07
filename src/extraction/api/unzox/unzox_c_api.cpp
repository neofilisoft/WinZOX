#include "extraction/api/unzox/unzox_c_api.h"

#include "archive/archive.hpp"
#include "extraction/api/unzox/unzox_api.hpp"
#include "extraction/extractor.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

#ifndef WINZOX_UNZOX_API_VERSION
#define WINZOX_UNZOX_API_VERSION "2.10.0"
#endif

void SetError(char* errorBuffer, size_t errorBufferSize, const std::string& message) {
    if (errorBuffer == nullptr || errorBufferSize == 0) {
        return;
    }

    const size_t copyLength = (message.size() < (errorBufferSize - 1)) ? message.size() : (errorBufferSize - 1);
    if (copyLength > 0) {
        std::memcpy(errorBuffer, message.data(), copyLength);
    }
    errorBuffer[copyLength] = '\0';
}

UnZOXStatusCode ToStatusCode(winzox::extraction::api::unzox::ErrorCode code) {
    return static_cast<UnZOXStatusCode>(code);
}

void ExportMetadata(const winzox::archive::ArchiveMetadata& metadata, UnZOXArchiveMetadata* output) {
    output->encrypted = metadata.encrypted ? 1 : 0;
    output->solid = metadata.solid ? 1 : 0;
    output->authenticated = metadata.authenticated ? 1 : 0;
    output->integrity_sha512 = metadata.integritySha512 ? 1 : 0;
    output->integrity_sha3_256 = metadata.integritySha3_256 ? 1 : 0;
    output->encryption_algorithm = static_cast<uint32_t>(metadata.encryptionAlgorithm);
    output->default_algorithm = static_cast<uint32_t>(metadata.defaultAlgorithm);
    output->created_unix_time = metadata.createdUnixTime;
    output->payload_checksum = metadata.payloadChecksum;
}

char* DuplicateUtf8String(const std::string& value) {
    const size_t size = value.size() + 1;
    auto* allocation = static_cast<char*>(std::malloc(size));
    if (allocation == nullptr) {
        throw std::runtime_error("Failed to allocate string buffer");
    }
    std::memcpy(allocation, value.c_str(), size);
    return allocation;
}

void ExportEntryList(const std::vector<winzox::archive::ArchiveEntryInfo>& entries, UnZOXEntryList* output) {
    output->entries = nullptr;
    output->count = 0;
    if (entries.empty()) {
        return;
    }

    auto* list = static_cast<UnZOXEntryInfo*>(std::calloc(entries.size(), sizeof(UnZOXEntryInfo)));
    if (list == nullptr) {
        throw std::runtime_error("Failed to allocate entry list");
    }

    size_t index = 0;
    try {
        for (; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            list[index].path = DuplicateUtf8String(entry.path);
            list[index].algorithm = static_cast<uint32_t>(entry.algorithm);
            list[index].original_size = entry.originalSize;
            list[index].stored_size = entry.storedSize;
            list[index].encoded_size = entry.encodedSize;
            list[index].crc32 = entry.crc32;
        }
    } catch (...) {
        for (size_t freeIndex = 0; freeIndex < index; ++freeIndex) {
            std::free(list[freeIndex].path);
        }
        std::free(list);
        throw;
    }

    output->entries = list;
    output->count = entries.size();
}

void ExportBuffer(const std::vector<uint8_t>& input, UnZOXBuffer* output) {
    output->data = nullptr;
    output->size = 0;
    if (input.empty()) {
        return;
    }

    auto* allocation = static_cast<uint8_t*>(std::malloc(input.size()));
    if (allocation == nullptr) {
        throw std::runtime_error("Failed to allocate output buffer");
    }
    std::memcpy(allocation, input.data(), input.size());
    output->data = allocation;
    output->size = input.size();
}

std::string ToPassword(const char* password) {
    return password == nullptr ? std::string() : std::string(password);
}

} // namespace

extern "C" {

const char* unzox_api_version(void) {
    return WINZOX_UNZOX_API_VERSION;
}

UnZOXStatusCode unzox_probe_archive_file(const char* archive_path,
                                         const char* password,
                                         UnZOXArchiveMetadata* out_metadata,
                                         UnZOXEntryList* out_entries,
                                         char* error_buffer,
                                         size_t error_buffer_size) {
    if (archive_path == nullptr || out_metadata == nullptr || out_entries == nullptr) {
        SetError(error_buffer, error_buffer_size, "Archive path and output pointers are required");
        return UNZOX_STATUS_INVALID_ARGUMENT;
    }
    out_entries->entries = nullptr;
    out_entries->count = 0;

    try {
        const std::string passwordValue = ToPassword(password);
        const winzox::archive::ArchiveMetadata metadata = winzox::extraction::GetArchiveMetadata(archive_path, passwordValue);
        const std::vector<winzox::archive::ArchiveEntryInfo> entries =
            winzox::extraction::ListArchiveEntries(archive_path, passwordValue);
        ExportMetadata(metadata, out_metadata);
        ExportEntryList(entries, out_entries);
        SetError(error_buffer, error_buffer_size, "");
        return UNZOX_STATUS_OK;
    } catch (const std::exception& error) {
        SetError(error_buffer, error_buffer_size, error.what());
        return ToStatusCode(winzox::extraction::api::unzox::ClassifyError(error.what()));
    }
}

UnZOXStatusCode unzox_extract_archive_file(const char* archive_path,
                                           const char* destination,
                                           const char* password,
                                           UnZOXProgressCallback progress_callback,
                                           void* user_data,
                                           char* error_buffer,
                                           size_t error_buffer_size) {
    if (archive_path == nullptr || destination == nullptr) {
        SetError(error_buffer, error_buffer_size, "Archive path and destination are required");
        return UNZOX_STATUS_INVALID_ARGUMENT;
    }

    try {
        const auto callback = progress_callback == nullptr
            ? winzox::utils::ProgressCallback()
            : winzox::utils::ProgressCallback([&](const winzox::utils::ProgressInfo& info) {
                return progress_callback(
                    info.completedUnits,
                    info.totalUnits,
                    info.currentItem.c_str(),
                    info.statusText.c_str(),
                    user_data) != 0;
            });
        winzox::extraction::ExtractArchive(archive_path, destination, ToPassword(password), callback);
        SetError(error_buffer, error_buffer_size, "");
        return UNZOX_STATUS_OK;
    } catch (const std::exception& error) {
        SetError(error_buffer, error_buffer_size, error.what());
        return ToStatusCode(winzox::extraction::api::unzox::ClassifyError(error.what()));
    }
}

UnZOXStatusCode unzox_read_entry_file(const char* archive_path,
                                      size_t entry_index,
                                      const char* password,
                                      UnZOXBuffer* out_buffer,
                                      char* error_buffer,
                                      size_t error_buffer_size) {
    if (archive_path == nullptr || out_buffer == nullptr) {
        SetError(error_buffer, error_buffer_size, "Archive path and output buffer are required");
        return UNZOX_STATUS_INVALID_ARGUMENT;
    }
    out_buffer->data = nullptr;
    out_buffer->size = 0;

    try {
        const std::vector<uint8_t> data =
            winzox::extraction::ReadArchiveEntry(archive_path, entry_index, ToPassword(password));
        ExportBuffer(data, out_buffer);
        SetError(error_buffer, error_buffer_size, "");
        return UNZOX_STATUS_OK;
    } catch (const std::exception& error) {
        SetError(error_buffer, error_buffer_size, error.what());
        return ToStatusCode(winzox::extraction::api::unzox::ClassifyError(error.what()));
    }
}

void unzox_free_buffer(UnZOXBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    if (buffer->data != nullptr) {
        std::free(buffer->data);
    }
    buffer->data = nullptr;
    buffer->size = 0;
}

void unzox_free_entry_list(UnZOXEntryList* entry_list) {
    if (entry_list == nullptr) {
        return;
    }
    if (entry_list->entries != nullptr) {
        for (size_t index = 0; index < entry_list->count; ++index) {
            std::free(entry_list->entries[index].path);
            entry_list->entries[index].path = nullptr;
        }
        std::free(entry_list->entries);
    }
    entry_list->entries = nullptr;
    entry_list->count = 0;
}

} // extern "C"
