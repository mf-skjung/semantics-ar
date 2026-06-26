#include "recovery.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "sar_recover.h"
#include "sar_recover_file.h"

int32_t sar_recovery_run(sar_comm_client_t *client,
                         const uint8_t *key_id,
                         const wchar_t *target_path,
                         uint64_t *out_bytes)
{
    semantics_ar_recovery_exec_t   req;
    semantics_ar_recovery_result_t res;
    sar_comm_status_t              cs;
    char target_utf8[SEMANTICS_AR_PROTO_PATH_MAX * 4];
    char temp_utf8[SEMANTICS_AR_PROTO_PATH_MAX * 4 + 16];
    uint32_t i;

    if (out_bytes)
        *out_bytes = 0;
    if (!client || !key_id || !target_path)
        return SAR_RECOVER_INVALID;

    memset(&req, 0, sizeof(req));
    req.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    req.header.message_type = SEMANTICS_AR_MSG_RECOVERY_EXEC;
    req.header.message_length = (uint32_t)sizeof(req);
    memcpy(req.key_id, key_id, SEMANTICS_AR_KEY_ID_SIZE);
    for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX && target_path[i] != 0; i++)
        req.target_path[i] = (uint16_t)target_path[i];
    req.target_path[i] = 0;

    memset(&res, 0, sizeof(res));
    cs = sar_comm_send_recv(client, &req, (uint32_t)sizeof(req),
                            &res, (uint32_t)sizeof(res),
                            SEMANTICS_AR_MSG_RECOVERY_RESULT);
    if (cs != SAR_COMM_OK)
        return SAR_RECOVER_INVALID;
    if (res.status != SAR_RECOVER_OK)
        return res.status;

    if (WideCharToMultiByte(CP_UTF8, 0, target_path, -1, target_utf8,
                            (int)sizeof(target_utf8), NULL, NULL) <= 0)
        return SAR_RECOVER_INVALID;
    if (_snprintf_s(temp_utf8, sizeof(temp_utf8), _TRUNCATE, "%s.sarrectmp", target_utf8) < 0)
        return SAR_RECOVER_INVALID;

    if (sar_atomic_replace_file(target_utf8, temp_utf8) != 0)
        return SAR_RECOVER_INVALID;

    if (out_bytes)
        *out_bytes = res.bytes_recovered;
    return SAR_RECOVER_OK;
}

int32_t sar_preserve_recovery_run(sar_comm_client_t *client,
                                  const wchar_t *target_path,
                                  uint64_t offset,
                                  uint64_t length,
                                  uint64_t *out_bytes)
{
    semantics_ar_preserve_recover_t req;
    semantics_ar_recovery_result_t  res;
    sar_comm_status_t               cs;
    char target_utf8[SEMANTICS_AR_PROTO_PATH_MAX * 4];
    char temp_utf8[SEMANTICS_AR_PROTO_PATH_MAX * 4 + 16];
    uint32_t i;

    if (out_bytes)
        *out_bytes = 0;
    if (!client || !target_path)
        return SAR_RECOVER_INVALID;

    memset(&req, 0, sizeof(req));
    req.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    req.header.message_type = SEMANTICS_AR_MSG_PRESERVE_RECOVER;
    req.header.message_length = (uint32_t)sizeof(req);
    for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX && target_path[i] != 0; i++)
        req.target_path[i] = (uint16_t)target_path[i];
    req.target_path[i] = 0;
    req.offset = offset;
    req.length = length;

    memset(&res, 0, sizeof(res));
    cs = sar_comm_send_recv(client, &req, (uint32_t)sizeof(req),
                            &res, (uint32_t)sizeof(res),
                            SEMANTICS_AR_MSG_PRESERVE_RESULT);
    if (cs != SAR_COMM_OK)
        return SAR_RECOVER_INVALID;
    if (res.status != SAR_RECOVER_OK)
        return res.status;

    if (WideCharToMultiByte(CP_UTF8, 0, target_path, -1, target_utf8,
                            (int)sizeof(target_utf8), NULL, NULL) <= 0)
        return SAR_RECOVER_INVALID;
    if (_snprintf_s(temp_utf8, sizeof(temp_utf8), _TRUNCATE, "%s.sarrectmp", target_utf8) < 0)
        return SAR_RECOVER_INVALID;

    if (sar_atomic_replace_file(target_utf8, temp_utf8) != 0)
        return SAR_RECOVER_INVALID;

    if (out_bytes)
        *out_bytes = res.bytes_recovered;
    return SAR_RECOVER_OK;
}
