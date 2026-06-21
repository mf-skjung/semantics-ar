#ifndef SEMANTICS_AR_PROTOCOL_H
#define SEMANTICS_AR_PROTOCOL_H

#include <stdint.h>

#define SEMANTICS_AR_MSG_CHAIN_CANDIDATE     1
#define SEMANTICS_AR_MSG_CONFIRMED           2
#define SEMANTICS_AR_MSG_RESTORE_COMPLETE    3
#define SEMANTICS_AR_MSG_SHADOW_SPACE_FREED  4
#define SEMANTICS_AR_MSG_SHADOW_USAGE_SET    5
#define SEMANTICS_AR_MSG_ASSOCIATE_CHILD     6
#define SEMANTICS_AR_MSG_SET_CONFIG          7
#define SEMANTICS_AR_MSG_ADD_TRUSTED_PID     8
#define SEMANTICS_AR_MSG_REMOVE_TRUSTED_PID  9
#define SEMANTICS_AR_MSG_GET_STATUS          10

#define SEMANTICS_AR_ACTION_ALLOW  0
#define SEMANTICS_AR_ACTION_BLOCK  1

#define SEMANTICS_AR_MAX_SECTION_PIDS   4
#define SEMANTICS_AR_MAX_TRUSTED_PIDS   64
#define SEMANTICS_AR_ORACLE_SAMPLE_SIZE 256
#define SEMANTICS_AR_PROTO_PATH_MAX     260

#pragma pack(push, 1)

typedef struct {
    uint32_t message_type;
} semantics_ar_msg_header_t;

typedef struct {
    uint32_t message_type;
    uint32_t process_id;
    uint32_t thread_id;
    uint16_t file_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t file_offset;
    uint32_t write_length;
    uint32_t sample_size;
    uint8_t  plaintext_sample[SEMANTICS_AR_ORACLE_SAMPLE_SIZE];
    uint8_t  ciphertext_sample[SEMANTICS_AR_ORACLE_SAMPLE_SIZE];
} semantics_ar_chain_notification_t;

typedef struct {
    uint32_t action;
} semantics_ar_chain_response_t;

typedef struct {
    uint32_t timeout_ms;
    uint32_t max_pending_writes;
} semantics_ar_config_t;

typedef struct {
    uint32_t pid;
    int64_t  creation_time;
} semantics_ar_trusted_pid_info_t;

typedef struct {
    uint32_t target_pid;
} semantics_ar_confirm_payload_t;

typedef struct {
    uint64_t bytes_freed;
    uint16_t volume_nt_path[SEMANTICS_AR_PROTO_PATH_MAX];
} semantics_ar_space_freed_payload_t;

typedef struct {
    int64_t  actual_bytes_used;
    uint16_t volume_nt_path[SEMANTICS_AR_PROTO_PATH_MAX];
} semantics_ar_shadow_usage_set_payload_t;

typedef struct {
    uint32_t child_pid;
    uint32_t originator_pid;
} semantics_ar_associate_child_payload_t;

typedef struct {
    int32_t  result_code;
    uint32_t protection_active;
    uint32_t confirmed_active;
    uint32_t trusted_pid_count;
} semantics_ar_status_reply_t;

#pragma pack(pop)

#endif