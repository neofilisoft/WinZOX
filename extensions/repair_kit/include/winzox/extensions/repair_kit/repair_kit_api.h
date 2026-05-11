#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef WINZOX_REPAIR_KIT_BUILD_SHARED
#    define WINZOX_REPAIR_KIT_API __declspec(dllexport)
#  else
#    define WINZOX_REPAIR_KIT_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define WINZOX_REPAIR_KIT_API __attribute__((visibility("default")))
#else
#  define WINZOX_REPAIR_KIT_API
#endif

#define WINZOX_REPAIR_KIT_MAGIC_SIZE 8u
#define WINZOX_REPAIR_KIT_MESSAGE_SIZE 256u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WinZOXRepairKitStatus {
    WINZOX_REPAIR_KIT_STATUS_OK = 0,
    WINZOX_REPAIR_KIT_STATUS_INVALID_ARGUMENT = 1,
    WINZOX_REPAIR_KIT_STATUS_IO_ERROR = 2,
    WINZOX_REPAIR_KIT_STATUS_UNSUPPORTED = 3,
    WINZOX_REPAIR_KIT_STATUS_REPAIRED = 4,
    WINZOX_REPAIR_KIT_STATUS_NOT_REPAIRABLE = 5
} WinZOXRepairKitStatus;

typedef struct WinZOXRepairKitReport {
    int file_exists;
    int format_supported;
    int is_split_volume;
    int is_probably_truncated;
    int can_attempt_repair;
    int repaired;
    int rebuilt_footer;
    uint32_t recovered_entry_count;
    uint64_t file_size;
    uint64_t recovered_output_size;
    char detected_magic[WINZOX_REPAIR_KIT_MAGIC_SIZE];
    char suggested_action[WINZOX_REPAIR_KIT_MESSAGE_SIZE];
} WinZOXRepairKitReport;

WINZOX_REPAIR_KIT_API const char* winzox_repair_kit_api_version(void);

WINZOX_REPAIR_KIT_API WinZOXRepairKitStatus winzox_repair_kit_analyze_file(
    const char* archive_path,
    WinZOXRepairKitReport* out_report,
    char* error_buffer,
    size_t error_buffer_size);

WINZOX_REPAIR_KIT_API WinZOXRepairKitStatus winzox_repair_kit_repair_file(
    const char* archive_path,
    const char* output_path,
    const char* password,
    WinZOXRepairKitReport* out_report,
    char* error_buffer,
    size_t error_buffer_size);

#ifdef __cplusplus
}
#endif
