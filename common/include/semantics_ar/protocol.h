#ifndef SEMANTICS_AR_PROTOCOL_H
#define SEMANTICS_AR_PROTOCOL_H

#include <stdint.h>
#include "semantics_ar/keystore_format.h"

#define SEMANTICS_AR_PROTOCOL_VERSION 1u

#define SAR_COMM_PORT_NAME L"\\SemanticsArPort"

#define SEMANTICS_AR_MSG_VERDICT_NOTIFY   1
#define SEMANTICS_AR_MSG_RECOVERY_EXEC    2
#define SEMANTICS_AR_MSG_RECOVERY_RESULT  3
#define SEMANTICS_AR_MSG_SET_MODE         4
#define SEMANTICS_AR_MSG_WHITELIST_ADD    5
#define SEMANTICS_AR_MSG_WHITELIST_REMOVE 6
#define SEMANTICS_AR_MSG_GET_STATUS       7
#define SEMANTICS_AR_MSG_CONNECT_CHALLENGE 8
#define SEMANTICS_AR_MSG_CONNECT_RESPONSE  9
#define SEMANTICS_AR_MSG_STATUS_REPLY     10
#define SEMANTICS_AR_MSG_CATALOG_QUERY    11
#define SEMANTICS_AR_MSG_CATALOG_REPLY    12
#define SEMANTICS_AR_MSG_PRESERVE_QUERY   13
#define SEMANTICS_AR_MSG_PRESERVE_REPLY   14
#define SEMANTICS_AR_MSG_PRESERVE_RECOVER 15
#define SEMANTICS_AR_MSG_PRESERVE_RESULT  16
#define SEMANTICS_AR_MSG_SET_BUDGET       17
#define SEMANTICS_AR_MSG_EVENTS_QUERY     18
#define SEMANTICS_AR_MSG_EVENTS_REPLY     19
#define SEMANTICS_AR_MSG_IDENTITY_VERDICT 20
#define SEMANTICS_AR_MSG_PROCESS_QUERY    21
#define SEMANTICS_AR_MSG_PROCESS_REPLY    22

#define SEMANTICS_AR_MODE_AUDIT   0
#define SEMANTICS_AR_MODE_ENFORCE 1

#define SAR_EVENT_CLASS_NONE               0u
#define SAR_EVENT_CLASS_KEY_CAPTURED       1u
#define SAR_EVENT_CLASS_BLOCK_FORWARD      2u
#define SAR_EVENT_CLASS_BLOCK_PHANTOM      3u
#define SAR_EVENT_CLASS_BLOCK_CAPACITY     4u
#define SAR_EVENT_CLASS_MODE_CHANGED       5u
#define SAR_EVENT_CLASS_WHITELIST_ADDED    6u
#define SAR_EVENT_CLASS_WHITELIST_REMOVED  7u
#define SAR_EVENT_CLASS_BLOCK_INJECTION    8u
#define SAR_EVENT_CLASS_EXEMPT_REVOKED     10u

#define SEMANTICS_AR_PROTO_PATH_MAX  260
#define SEMANTICS_AR_PROTO_SUBJECT_MAX 256
#define SEMANTICS_AR_CONTENT_HASH_SIZE 32
#define SEMANTICS_AR_HS_NONCE_SIZE 32
#define SEMANTICS_AR_HS_SIG_MAX 512

#pragma pack(push, 1)

typedef struct {
    uint32_t protocol_version;
    uint32_t message_type;
    uint32_t message_length;
} semantics_ar_msg_header_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint32_t algorithm;
    uint32_t mode;
    uint64_t mode_params;
    uint64_t provenance_offset;
    uint16_t provenance_path[SEMANTICS_AR_PROTO_PATH_MAX];
} semantics_ar_verdict_notify_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint16_t target_path[SEMANTICS_AR_PROTO_PATH_MAX];
} semantics_ar_recovery_exec_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  status;
    uint64_t bytes_recovered;
} semantics_ar_recovery_result_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint32_t mode;
} semantics_ar_set_mode_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint16_t image_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint16_t cert_subject[SEMANTICS_AR_PROTO_SUBJECT_MAX];
    uint8_t  content_hash[SEMANTICS_AR_CONTENT_HASH_SIZE];
} semantics_ar_whitelist_control_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint64_t pid;
    uint64_t start_key;
    uint16_t image_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint16_t cert_subject[SEMANTICS_AR_PROTO_SUBJECT_MAX];
    uint8_t  content_hash[SEMANTICS_AR_CONTENT_HASH_SIZE];
} semantics_ar_identity_verdict_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint64_t pid;
} semantics_ar_process_query_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint32_t valid;
    uint32_t id_state;
    uint64_t start_key;
} semantics_ar_process_reply_t;

typedef struct {
    semantics_ar_msg_header_t header;
} semantics_ar_get_status_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  result;
    uint32_t protocol_version;
    uint32_t mode;
    uint64_t captured_key_count;
    uint64_t preserve_capacity_bytes;
    uint64_t preserve_used_bytes;
    uint64_t preserve_retention_100ns;
    uint64_t preserve_oldest_protected_time;
    uint32_t preserve_protected_count;
    uint32_t preserve_probation_count;
    uint64_t capture_inflight;
} semantics_ar_status_reply_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint8_t  nonce[SEMANTICS_AR_HS_NONCE_SIZE];
} semantics_ar_connect_challenge_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint32_t sig_length;
    uint8_t  signature[SEMANTICS_AR_HS_SIG_MAX];
} semantics_ar_connect_response_t;

typedef struct {
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint32_t algorithm;
    uint32_t mode;
    uint64_t provenance_offset;
    uint16_t provenance_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t capture_time;
    uint64_t actor_start_key;
} semantics_ar_catalog_entry_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint32_t index;
} semantics_ar_catalog_query_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  result;
    uint32_t total;
    uint32_t index;
    uint32_t valid;
    semantics_ar_catalog_entry_t entry;
} semantics_ar_catalog_reply_t;

typedef struct {
    uint16_t provenance_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t provenance_offset;
    uint64_t provenance_length;
    uint64_t capture_time;
    uint64_t payload_length;
} semantics_ar_preserve_entry_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint32_t index;
} semantics_ar_preserve_query_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  result;
    uint32_t total;
    uint32_t index;
    uint32_t valid;
    semantics_ar_preserve_entry_t entry;
} semantics_ar_preserve_reply_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint16_t target_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t offset;
    uint64_t length;
} semantics_ar_preserve_recover_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint64_t retention_100ns;
    uint64_t capacity_bytes;
} semantics_ar_set_budget_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint64_t generation;
    uint64_t sequence;
} semantics_ar_events_query_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  result;
    uint32_t valid;
    uint32_t gap;
    uint32_t event_class;
    uint64_t generation;
    uint64_t sequence;
    uint64_t timestamp;
    uint64_t actor_start_key;
} semantics_ar_events_reply_t;

#pragma pack(pop)

#endif
