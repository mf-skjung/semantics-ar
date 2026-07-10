#ifndef SARAPI_H
#define SARAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SARAPI_ABI_VERSION 2u

#if defined(SARAPI_STATIC)
#define SARAPI_API
#elif defined(SARAPI_EXPORTS)
#define SARAPI_API __declspec(dllexport)
#else
#define SARAPI_API __declspec(dllimport)
#endif

typedef enum {
    SARAPI_OK = 0,
    SARAPI_INVALID_ARG = 1,
    SARAPI_PIPE_UNAVAILABLE = 2,
    SARAPI_ACCESS_DENIED = 3,
    SARAPI_SERVER_UNTRUSTED = 4,
    SARAPI_VERSION_MISMATCH = 5,
    SARAPI_TRANSPORT_ERROR = 6
} sarapi_result_t;

#define SARAPI_DESCENT_NO_TPM      0x1u
#define SARAPI_DESCENT_NO_VBS_HVCI 0x2u
#define SARAPI_DESCENT_NO_PPL      0x4u

#define SARAPI_PRESERVE_UNKNOWN    0u
#define SARAPI_PRESERVE_HEALTHY    1u
#define SARAPI_PRESERVE_LOW        2u
#define SARAPI_PRESERVE_CRITICAL   3u

#define SARAPI_EXPIRY_NONE         0u
#define SARAPI_EXPIRY_GT_7D        1u
#define SARAPI_EXPIRY_LE_7D        2u
#define SARAPI_EXPIRY_LE_24H       3u

typedef struct {
    uint32_t protocol_version;
    uint32_t service_running;
    uint32_t driver_connected;
    uint32_t mode;
    uint64_t captured_key_count;
    uint32_t descents;
    uint32_t preserve_health;
    uint32_t oldest_expiry_bucket;
} sarapi_posture_t;

SARAPI_API uint32_t __cdecl sarapi_abi_version(void);

SARAPI_API sarapi_result_t __cdecl sarapi_posture_read(sarapi_posture_t *out);

#define SARAPI_PATH_MAX    260
#define SARAPI_SUBJECT_MAX 256
#define SARAPI_HASH_SIZE   32
#define SARAPI_KEY_ID_SIZE 32
#define SARAPI_PAGE        8

typedef struct {
    uint8_t  key_id[SARAPI_KEY_ID_SIZE];
    uint32_t algorithm;
    uint32_t mode;
    uint16_t provenance_path[SARAPI_PATH_MAX];
    uint64_t capture_time;
    uint64_t actor_start_key;
} sarapi_catalog_entry_t;

#define SARAPI_PRESERVE_PROBATION 0u
#define SARAPI_PRESERVE_PROTECTED 1u

typedef struct {
    uint16_t provenance_path[SARAPI_PATH_MAX];
    uint64_t offset;
    uint64_t length;
    uint64_t capture_time;
    uint64_t size;
    uint64_t actor_start_key;
    uint64_t app_identity_id;
    uint32_t state;
} sarapi_preserve_entry_t;

typedef struct {
    uint64_t app_identity_id;
    uint16_t image_path[SARAPI_PATH_MAX];
    uint16_t cert_subject[SARAPI_SUBJECT_MAX];
    uint8_t  content_hash[SARAPI_HASH_SIZE];
    uint32_t verdict;
} sarapi_app_identity_t;

typedef struct {
    uint16_t image_path[SARAPI_PATH_MAX];
    uint16_t cert_subject[SARAPI_SUBJECT_MAX];
    uint8_t  content_hash[SARAPI_HASH_SIZE];
} sarapi_identity_t;

#define SARAPI_WL_MATCH_MATCHING              0u
#define SARAPI_WL_MATCH_LAPSED_SAME_SIGNER    1u
#define SARAPI_WL_MATCH_LAPSED_CHANGED_SIGNER 2u

typedef struct {
    uint16_t image_path[SARAPI_PATH_MAX];
    uint16_t cert_subject[SARAPI_SUBJECT_MAX];
    uint8_t  content_hash[SARAPI_HASH_SIZE];
    uint64_t first_seen;
    uint32_t match_state;
} sarapi_whitelist_entry_t;

SARAPI_API sarapi_result_t __cdecl sarapi_catalog_page(uint32_t start,
                                                       sarapi_catalog_entry_t *entries,
                                                       uint32_t *out_total,
                                                       uint32_t *out_returned);

SARAPI_API sarapi_result_t __cdecl sarapi_preserve_page(uint32_t start,
                                                        sarapi_preserve_entry_t *entries,
                                                        uint32_t *out_total,
                                                        uint32_t *out_returned);

SARAPI_API sarapi_result_t __cdecl sarapi_app_identity_page(uint32_t start,
                                                            sarapi_app_identity_t *entries,
                                                            uint32_t *out_total,
                                                            uint32_t *out_returned);

SARAPI_API sarapi_result_t __cdecl sarapi_recover(const uint8_t *key_id,
                                                  const uint16_t *target_path,
                                                  int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_preserve_recover(const uint16_t *target_path,
                                                           uint64_t offset,
                                                           uint64_t length,
                                                           int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_set_mode(uint32_t mode, int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_set_budget(uint64_t retention_100ns,
                                                     uint64_t capacity_bytes,
                                                     int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_resolve_identity(const uint16_t *image_path,
                                                           sarapi_identity_t *out_identity,
                                                           uint32_t *out_verdict,
                                                           int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_whitelist_add(const uint16_t *image_path,
                                                        uint32_t *out_verdict,
                                                        int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_whitelist_remove(const uint16_t *image_path,
                                                           uint32_t *out_verdict,
                                                           int32_t *out_result);

SARAPI_API sarapi_result_t __cdecl sarapi_whitelist_page(uint32_t start,
                                                         sarapi_whitelist_entry_t *entries,
                                                         uint32_t *out_total,
                                                         uint32_t *out_returned);

#define SARAPI_EVENT_NONE              0u
#define SARAPI_EVENT_KEY_CAPTURED      1u
#define SARAPI_EVENT_BLOCK_FORWARD     2u
#define SARAPI_EVENT_BLOCK_PHANTOM     3u
#define SARAPI_EVENT_BLOCK_CAPACITY    4u
#define SARAPI_EVENT_MODE_CHANGED      5u
#define SARAPI_EVENT_WHITELIST_ADDED   6u
#define SARAPI_EVENT_WHITELIST_REMOVED 7u
#define SARAPI_EVENT_BLOCK_INJECTION   8u
#define SARAPI_EVENT_EXEMPT_REVOKED    10u

typedef struct {
    uint32_t valid;
    uint32_t gap;
    uint32_t event_class;
    uint64_t generation;
    uint64_t sequence;
    uint64_t timestamp;
    uint64_t actor_start_key;
} sarapi_event_t;

SARAPI_API sarapi_result_t __cdecl sarapi_events_open(void **out_handle);

SARAPI_API sarapi_result_t __cdecl sarapi_events_read(void *handle, sarapi_event_t *out);

SARAPI_API void __cdecl sarapi_events_close(void *handle);

#ifdef __cplusplus
}
#endif

#endif
