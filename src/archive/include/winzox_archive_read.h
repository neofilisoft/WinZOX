/*
 * winzox_archive_read.h - C ABI for the WinZOX 3.1.0 archive read-side core
 * Rust port.
 *
 * Hand-written to match `winzox-archive-read/src/ffi.rs`. Bump the ABI
 * constant whenever any struct layout or function signature changes.
 */
#ifndef WINZOX_ARCHIVE_READ_H
#define WINZOX_ARCHIVE_READ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WinzoxArStatus {
    WINZOX_AR_OK = 0,
    WINZOX_AR_NULL_ARGUMENT = 1,
    WINZOX_AR_UNKNOWN_MAGIC = 2,
    WINZOX_AR_TRUNCATED = 3,
    WINZOX_AR_INCONSISTENT_ENCRYPTION = 4,
    WINZOX_AR_AEAD_REJECTED = 5,
    WINZOX_AR_AUTH_FLAG_NOT_SUPPORTED = 6,
    WINZOX_AR_AUTH_REQUIRES_ENCRYPTION = 7,
    WINZOX_AR_UNAUTHENTICATED_V3_ENCRYPTED = 8,
    WINZOX_AR_ENTRY_COUNT_EXCEEDS_CAP = 9,
    WINZOX_AR_CENTRAL_DIRECTORY_TOO_LARGE = 10,
    WINZOX_AR_CENTRAL_DIRECTORY_OFFSET_INVALID = 11,
    WINZOX_AR_CENTRAL_DIRECTORY_TRUNCATED = 12,
    WINZOX_AR_ENTRY_COUNT_MISMATCH = 13,
    WINZOX_AR_DIRECTORY_INCONSISTENT = 14,
    WINZOX_AR_EMPTY_ENTRY_NAME = 15,
    WINZOX_AR_ENTRY_SIZE_EXCEEDS_CAP = 16,
    WINZOX_AR_FOOTER_INCONSISTENT = 17,
    WINZOX_AR_FOOTER_MISSING = 18,
    WINZOX_AR_KDF_PARAMS_INVALID = 19,
    WINZOX_AR_SHA512_MISMATCH = 20,
    WINZOX_AR_SHA3_MISMATCH = 21,
    WINZOX_AR_UNSUPPORTED_COMPRESSION = 22,
    WINZOX_AR_UNSUPPORTED_ENCRYPTION = 23,
    WINZOX_AR_ENCRYPTED_ARCHIVE = 24,
    WINZOX_AR_INTEGER_OVERFLOW = 25,
} WinzoxArStatus;

typedef struct WinzoxArDigests {
    uint8_t  sha512[64];
    uint8_t  sha3_256[32];
} WinzoxArDigests;

typedef struct WinzoxArBuffer {
    uint8_t* data;
    size_t   len;
    size_t   cap;
} WinzoxArBuffer;

typedef struct WinzoxArMetadata {
    uint8_t  encrypted;
    uint8_t  solid;
    uint8_t  authenticated;
    uint8_t  integrity_sha512;
    uint8_t  integrity_sha3_256;
    uint8_t  encryption_algorithm;
    uint8_t  default_algorithm;
    uint8_t  _padding;
    uint64_t created_unix_time;
    uint32_t payload_checksum;
    uint32_t _padding2;
} WinzoxArMetadata;

typedef struct WinzoxArEntry {
    size_t   path_offset;
    size_t   path_len;
    uint8_t  algorithm;
    uint8_t  _padding[7];
    uint64_t original_size;
    uint64_t stored_size;
    uint64_t encoded_size;
    uint32_t crc32;
    uint32_t _padding2;
} WinzoxArEntry;

typedef struct WinzoxArIntegrity WinzoxArIntegrity;

const char*    winzox_ar_last_error(void);
void           winzox_ar_buffer_free(WinzoxArBuffer buffer);

int            winzox_ar_looks_like_zox(const uint8_t* data, size_t len);

WinzoxArIntegrity* winzox_ar_integrity_new(void);
void               winzox_ar_integrity_free(WinzoxArIntegrity* handle);
WinzoxArStatus     winzox_ar_integrity_update(WinzoxArIntegrity* handle,
                                              const uint8_t* data,
                                              size_t len);
WinzoxArStatus     winzox_ar_integrity_finalize(WinzoxArIntegrity* handle,
                                                WinzoxArDigests* out_digests);
WinzoxArStatus     winzox_ar_compute_digests(const uint8_t* data,
                                             size_t len,
                                             WinzoxArDigests* out_digests);
int                winzox_ar_digests_equal(const uint8_t* a,
                                           const uint8_t* b,
                                           size_t len);

WinzoxArStatus     winzox_ar_read_metadata_plain(const uint8_t* data,
                                                 size_t len,
                                                 WinzoxArMetadata* out_metadata,
                                                 WinzoxArBuffer* comment_out);
WinzoxArStatus     winzox_ar_read_index_plain(const uint8_t* data,
                                              size_t len,
                                              WinzoxArBuffer* out_entries,
                                              size_t* out_entry_count,
                                              WinzoxArBuffer* string_heap_out);

int                winzox_ar_abi_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WINZOX_ARCHIVE_READ_H */
