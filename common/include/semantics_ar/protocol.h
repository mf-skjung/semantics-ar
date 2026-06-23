#ifndef SEMANTICS_AR_PROTOCOL_H
#define SEMANTICS_AR_PROTOCOL_H

#include <stdint.h>
#include "semantics_ar/keystore_format.h"

#define SEMANTICS_AR_PROTOCOL_VERSION 1u

#define SEMANTICS_AR_MSG_VERDICT_NOTIFY   1
#define SEMANTICS_AR_MSG_RECOVERY_REQUEST 2
#define SEMANTICS_AR_MSG_RECOVERY_DONE    3
#define SEMANTICS_AR_MSG_SET_MODE         4
#define SEMANTICS_AR_MSG_WHITELIST_ADD    5
#define SEMANTICS_AR_MSG_WHITELIST_REMOVE 6
#define SEMANTICS_AR_MSG_GET_STATUS       7

#define SEMANTICS_AR_MODE_AUDIT   0
#define SEMANTICS_AR_MODE_ENFORCE 1

#define SEMANTICS_AR_PROTO_PATH_MAX  260
#define SEMANTICS_AR_PROTO_SUBJECT_MAX 256
#define SEMANTICS_AR_CONTENT_HASH_SIZE 32

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
} semantics_ar_recovery_request_t;

typedef struct {
    semantics_ar_msg_header_t header;
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    int32_t  result;
    uint32_t files_recovered;
} semantics_ar_recovery_done_t;

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
} semantics_ar_get_status_t;

typedef struct {
    semantics_ar_msg_header_t header;
    int32_t  result;
    uint32_t protocol_version;
    uint32_t mode;
    uint64_t captured_key_count;
} semantics_ar_status_reply_t;

#pragma pack(pop)

#endif
