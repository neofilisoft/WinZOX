#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef WINZOX_UNZOX_BUILD_SHARED
#    define WINZOX_UNZOX_API __declspec(dllexport)
#  else
#    define WINZOX_UNZOX_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define WINZOX_UNZOX_API __attribute__((visibility("default")))
#else
#  define WINZOX_UNZOX_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UnZOXStatusCode {
    UNZOX_STATUS_OK = 0,
    UNZOX_STATUS_INVALID_ARGUMENT = 1,
    UNZOX_STATUS_INVALID_MAGIC = 2,
    UNZOX_STATUS_UNSUPPORTED_FORMAT = 3,
    UNZOX_STATUS_TRUNCATED = 4,
    UNZOX_STATUS_PASSWORD_REQUIRED = 5,
    UNZOX_STATUS_AUTH_FAILED = 6,
    UNZOX_STATUS_GORGON_DECRYPT = 7,
    UNZOX_STATUS_DECRYPT_FAILED = 8,
    UNZOX_STATUS_INTEGRITY_FAILED = 9,
    UNZOX_STATUS_ENTRY_OUT_OF_RANGE = 10,
    UNZOX_STATUS_CANCELED = 11,
    UNZOX_STATUS_IO = 12,
    UNZOX_STATUS_INTERNAL = 13
} UnZOXStatusCode;

typedef struct UnZOXArchiveMetadata {
    int encrypted;
    int solid;
    int authenticated;
    int integrity_sha512;
    int integrity_sha3_256;
    uint32_t encryption_algorithm;
    uint32_t default_algorithm;
    uint64_t created_unix_time;
    uint32_t payload_checksum;
} UnZOXArchiveMetadata;

typedef struct UnZOXEntryInfo {
    char* path;
    uint32_t algorithm;
    uint64_t original_size;
    uint64_t stored_size;
    uint64_t encoded_size;
    uint32_t crc32;
} UnZOXEntryInfo;

typedef struct UnZOXEntryList {
    UnZOXEntryInfo* entries;
    size_t count;
} UnZOXEntryList;

typedef struct UnZOXBuffer {
    uint8_t* data;
    size_t size;
} UnZOXBuffer;

typedef int (*UnZOXProgressCallback)(
    uint64_t completed_units,
    uint64_t total_units,
    const char* current_item,
    const char* status_text,
    void* user_data);

WINZOX_UNZOX_API const char* unzox_api_version(void);

WINZOX_UNZOX_API UnZOXStatusCode unzox_probe_archive_file(
    const char* archive_path,
    const char* password,
    UnZOXArchiveMetadata* out_metadata,
    UnZOXEntryList* out_entries,
    char* error_buffer,
    size_t error_buffer_size);

WINZOX_UNZOX_API UnZOXStatusCode unzox_extract_archive_file(
    const char* archive_path,
    const char* destination,
    const char* password,
    UnZOXProgressCallback progress_callback,
    void* user_data,
    char* error_buffer,
    size_t error_buffer_size);

WINZOX_UNZOX_API UnZOXStatusCode unzox_read_entry_file(
    const char* archive_path,
    size_t entry_index,
    const char* password,
    UnZOXBuffer* out_buffer,
    char* error_buffer,
    size_t error_buffer_size);

WINZOX_UNZOX_API void unzox_free_buffer(UnZOXBuffer* buffer);
WINZOX_UNZOX_API void unzox_free_entry_list(UnZOXEntryList* entry_list);

#ifdef __cplusplus
}
#endif
