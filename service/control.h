#ifndef SEMANTICS_AR_SERVICE_CONTROL_H
#define SEMANTICS_AR_SERVICE_CONTROL_H

#include <windows.h>
#include <stdint.h>

#include "commclient.h"
#include "identity.h"
#include "semantics_ar/posture.h"

#define SAR_CONTROL_PIPE_NAME L"\\\\.\\pipe\\SemanticsArControl"

#define SAR_CTL_OP_SET_MODE          1u
#define SAR_CTL_OP_WHITELIST_ADD     2u
#define SAR_CTL_OP_WHITELIST_REMOVE  3u
#define SAR_CTL_OP_RECOVER           4u
#define SAR_CTL_OP_LIST              5u
#define SAR_CTL_OP_PRESERVE_LIST     6u
#define SAR_CTL_OP_PRESERVE_RECOVER  7u
#define SAR_CTL_OP_SET_BUDGET        8u
#define SAR_CTL_OP_RESOLVE_IDENTITY  9u
#define SAR_CTL_OP_VERDICT           10u
#define SAR_CTL_OP_PROCESS_QUERY     11u
#define SAR_CTL_OP_STATUS            12u

#define SAR_CTL_LIST_PAGE            8u

#pragma pack(push, 1)

typedef struct {
    uint32_t op;
    uint32_t mode;
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint16_t image_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t arg0;
    uint64_t arg1;
} sar_control_command_t;

typedef struct {
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint32_t algorithm;
    uint32_t mode;
    uint16_t provenance_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t capture_time;
    uint64_t actor_start_key;
} sar_catalog_entry_t;

typedef struct {
    uint16_t provenance_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t offset;
    uint64_t length;
    uint64_t capture_time;
    uint64_t size;
} sar_preserve_list_entry_t;

typedef struct {
    int32_t            result;
    uint32_t           verdict;
    uint32_t           total;
    uint32_t           returned;
    sar_catalog_entry_t entries[SAR_CTL_LIST_PAGE];
    sar_preserve_list_entry_t preserve_entries[SAR_CTL_LIST_PAGE];
    sar_identity_t     resolved;
    uint32_t           id_state;
    uint64_t           proc_start_key;
    uint64_t           capture_inflight;
    uint64_t           preserve_used_bytes;
} sar_control_reply_t;

#pragma pack(pop)

sar_comm_status_t sar_control_send_mode(sar_comm_client_t *client,
                                        uint32_t mode);

sar_comm_status_t sar_control_send_whitelist(sar_comm_client_t *client,
                                             uint32_t op,
                                             const sar_identity_t *identity);

int sar_control_apply(sar_comm_client_t *client,
                      const sar_control_command_t *cmd,
                      sar_control_reply_t *reply);

int sar_control_listener_start(sar_comm_client_t *client);

void sar_control_listener_stop(void);

#endif
