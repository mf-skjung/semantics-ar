#ifndef SEMANTICS_AR_SERVICE_CONTROL_H
#define SEMANTICS_AR_SERVICE_CONTROL_H

#include <windows.h>
#include <stdint.h>

#include "commclient.h"
#include "identity.h"

#define SAR_CONTROL_PIPE_NAME L"\\\\.\\pipe\\SemanticsArControl"

#define SAR_CTL_OP_SET_MODE          1u
#define SAR_CTL_OP_WHITELIST_ADD     2u
#define SAR_CTL_OP_WHITELIST_REMOVE  3u
#define SAR_CTL_OP_RECOVER           4u

#pragma pack(push, 1)

typedef struct {
    uint32_t op;
    uint32_t mode;
    uint8_t  key_id[SEMANTICS_AR_KEY_ID_SIZE];
    uint16_t image_path[SEMANTICS_AR_PROTO_PATH_MAX];
} sar_control_command_t;

typedef struct {
    int32_t  result;
    uint32_t verdict;
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

HANDLE sar_control_listener_start(sar_comm_client_t *client);

void sar_control_listener_stop(HANDLE thread);

#endif
