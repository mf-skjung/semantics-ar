#include <windows.h>
#include <stddef.h>
#include <string.h>

#include "sarapi.h"
#include "server_identity.h"
#include "control.h"

_Static_assert(SARAPI_KEY_ID_SIZE == SEMANTICS_AR_KEY_ID_SIZE, "key id size");
_Static_assert(SARAPI_PATH_MAX == SEMANTICS_AR_PROTO_PATH_MAX, "path max");
_Static_assert(SARAPI_SUBJECT_MAX == SEMANTICS_AR_PROTO_SUBJECT_MAX, "subject max");
_Static_assert(SARAPI_HASH_SIZE == SEMANTICS_AR_CONTENT_HASH_SIZE, "hash size");
_Static_assert(SARAPI_PAGE == SAR_CTL_LIST_PAGE, "page size");
_Static_assert(sizeof(sarapi_preserve_entry_t) == 576, "preserve entry ABI");
_Static_assert(sizeof(sarapi_app_identity_t) == 1080, "app identity ABI");

#define SARAPI_CTL_BUSY_WAIT_MS 2000u
#define SARAPI_CTL_ATTEMPTS     3u

static sarapi_result_t open_control(HANDLE *out_pipe)
{
    DWORD attempt;

    for (attempt = 0; attempt < SARAPI_CTL_ATTEMPTS; attempt++) {
        HANDLE pipe = CreateFileW(SAR_CONTROL_PIPE_NAME,
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) {
            *out_pipe = pipe;
            return SARAPI_OK;
        }

        switch (GetLastError()) {
        case ERROR_PIPE_BUSY:
            if (!WaitNamedPipeW(SAR_CONTROL_PIPE_NAME, SARAPI_CTL_BUSY_WAIT_MS))
                return SARAPI_PIPE_UNAVAILABLE;
            break;
        case ERROR_ACCESS_DENIED:
            return SARAPI_ACCESS_DENIED;
        default:
            return SARAPI_PIPE_UNAVAILABLE;
        }
    }
    return SARAPI_PIPE_UNAVAILABLE;
}

static sarapi_result_t write_all(HANDLE h, const void *buf, DWORD n)
{
    const BYTE *p = (const BYTE *)buf;
    DWORD       done = 0;

    while (done < n) {
        DWORD put = 0;

        if (!WriteFile(h, p + done, n - done, &put, NULL) || put == 0)
            return SARAPI_TRANSPORT_ERROR;
        done += put;
    }
    return SARAPI_OK;
}

static sarapi_result_t read_all(HANDLE h, void *buf, DWORD n)
{
    BYTE *p = (BYTE *)buf;
    DWORD done = 0;

    while (done < n) {
        DWORD got = 0;

        if (!ReadFile(h, p + done, n - done, &got, NULL) || got == 0)
            return SARAPI_TRANSPORT_ERROR;
        done += got;
    }
    return SARAPI_OK;
}

static sarapi_result_t control_txn(const sar_control_command_t *cmd,
                                   sar_control_reply_t *reply)
{
    HANDLE          pipe = INVALID_HANDLE_VALUE;
    sarapi_result_t r;

    r = open_control(&pipe);
    if (r != SARAPI_OK)
        return r;

    r = sarapi_server_is_system(pipe);
    if (r != SARAPI_OK) {
        CloseHandle(pipe);
        return r;
    }

    r = write_all(pipe, cmd, (DWORD)sizeof(*cmd));
    if (r == SARAPI_OK)
        r = read_all(pipe, reply, (DWORD)sizeof(*reply));

    CloseHandle(pipe);
    return r;
}

static void copy_path_in(const uint16_t *src, uint16_t *dst, uint32_t cap)
{
    uint32_t i = 0;

    if (src)
        for (; i + 1 < cap && src[i] != 0; i++)
            dst[i] = src[i];
    dst[i] = 0;
}

sarapi_result_t __cdecl sarapi_catalog_page(uint32_t start,
                                            sarapi_catalog_entry_t *entries,
                                            uint32_t *out_total,
                                            uint32_t *out_returned)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;
    uint32_t              i;

    if (!entries || !out_total || !out_returned)
        return SARAPI_INVALID_ARG;

    *out_total = 0;
    *out_returned = 0;
    memset(entries, 0, sizeof(*entries) * SARAPI_PAGE);

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_LIST;
    cmd.mode = start;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    if (reply.result != 0)
        return SARAPI_ACCESS_DENIED;
    if (reply.returned > SARAPI_PAGE)
        return SARAPI_TRANSPORT_ERROR;

    for (i = 0; i < reply.returned; i++) {
        memcpy(entries[i].key_id, reply.entries[i].key_id, SARAPI_KEY_ID_SIZE);
        entries[i].algorithm = reply.entries[i].algorithm;
        entries[i].mode = reply.entries[i].mode;
        memcpy(entries[i].provenance_path, reply.entries[i].provenance_path,
               sizeof(entries[i].provenance_path));
        entries[i].capture_time = reply.entries[i].capture_time;
        entries[i].actor_start_key = reply.entries[i].actor_start_key;
    }
    *out_total = reply.total;
    *out_returned = reply.returned;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_preserve_page(uint32_t start,
                                             sarapi_preserve_entry_t *entries,
                                             uint32_t *out_total,
                                             uint32_t *out_returned)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;
    uint32_t              i;

    if (!entries || !out_total || !out_returned)
        return SARAPI_INVALID_ARG;

    *out_total = 0;
    *out_returned = 0;
    memset(entries, 0, sizeof(*entries) * SARAPI_PAGE);

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_PRESERVE_LIST;
    cmd.mode = start;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    if (reply.result != 0)
        return SARAPI_ACCESS_DENIED;
    if (reply.returned > SARAPI_PAGE)
        return SARAPI_TRANSPORT_ERROR;

    for (i = 0; i < reply.returned; i++) {
        memcpy(entries[i].provenance_path, reply.preserve_entries[i].provenance_path,
               sizeof(entries[i].provenance_path));
        entries[i].offset = reply.preserve_entries[i].offset;
        entries[i].length = reply.preserve_entries[i].length;
        entries[i].capture_time = reply.preserve_entries[i].capture_time;
        entries[i].size = reply.preserve_entries[i].size;
        entries[i].actor_start_key = reply.preserve_entries[i].actor_start_key;
        entries[i].app_identity_id = reply.preserve_entries[i].app_identity_id;
        entries[i].state = reply.preserve_entries[i].state;
    }
    *out_total = reply.total;
    *out_returned = reply.returned;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_app_identity_page(uint32_t start,
                                                 sarapi_app_identity_t *entries,
                                                 uint32_t *out_total,
                                                 uint32_t *out_returned)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;
    uint32_t              i;

    if (!entries || !out_total || !out_returned)
        return SARAPI_INVALID_ARG;

    *out_total = 0;
    *out_returned = 0;
    memset(entries, 0, sizeof(*entries) * SARAPI_PAGE);

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_APP_IDENTITY_LIST;
    cmd.mode = start;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    if (reply.result != 0)
        return SARAPI_ACCESS_DENIED;
    if (reply.returned > SARAPI_PAGE)
        return SARAPI_TRANSPORT_ERROR;

    for (i = 0; i < reply.returned; i++) {
        entries[i].app_identity_id = reply.app_identities[i].app_identity_id;
        memcpy(entries[i].image_path, reply.app_identities[i].image_path,
               sizeof(entries[i].image_path));
        memcpy(entries[i].cert_subject, reply.app_identities[i].cert_subject,
               sizeof(entries[i].cert_subject));
        memcpy(entries[i].content_hash, reply.app_identities[i].content_hash,
               sizeof(entries[i].content_hash));
        entries[i].verdict = reply.app_identities[i].verdict;
    }
    *out_total = reply.total;
    *out_returned = reply.returned;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_recover(const uint8_t *key_id,
                                       const uint16_t *target_path,
                                       int32_t *out_result)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;

    if (!key_id || !target_path || !out_result)
        return SARAPI_INVALID_ARG;
    *out_result = -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_RECOVER;
    memcpy(cmd.key_id, key_id, SARAPI_KEY_ID_SIZE);
    copy_path_in(target_path, cmd.image_path, SEMANTICS_AR_PROTO_PATH_MAX);

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    *out_result = reply.result;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_preserve_recover(const uint16_t *target_path,
                                                uint64_t offset,
                                                uint64_t length,
                                                int32_t *out_result)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;

    if (!target_path || !out_result)
        return SARAPI_INVALID_ARG;
    *out_result = -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_PRESERVE_RECOVER;
    copy_path_in(target_path, cmd.image_path, SEMANTICS_AR_PROTO_PATH_MAX);
    cmd.arg0 = offset;
    cmd.arg1 = length;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    *out_result = reply.result;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_set_mode(uint32_t mode, int32_t *out_result)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;

    if (!out_result)
        return SARAPI_INVALID_ARG;
    *out_result = -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_SET_MODE;
    cmd.mode = mode;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    *out_result = reply.result;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_set_budget(uint64_t retention_100ns,
                                          uint64_t capacity_bytes,
                                          int32_t *out_result)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;

    if (!out_result)
        return SARAPI_INVALID_ARG;
    *out_result = -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = SAR_CTL_OP_SET_BUDGET;
    cmd.arg0 = retention_100ns;
    cmd.arg1 = capacity_bytes;

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;
    *out_result = reply.result;
    return SARAPI_OK;
}

static sarapi_result_t identity_op(uint32_t op,
                                   const uint16_t *image_path,
                                   sarapi_identity_t *out_identity,
                                   uint32_t *out_verdict,
                                   int32_t *out_result)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    sarapi_result_t       r;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = op;
    copy_path_in(image_path, cmd.image_path, SEMANTICS_AR_PROTO_PATH_MAX);

    r = control_txn(&cmd, &reply);
    if (r != SARAPI_OK)
        return r;

    if (out_identity) {
        memcpy(out_identity->image_path, reply.resolved.image_path,
               sizeof(out_identity->image_path));
        memcpy(out_identity->cert_subject, reply.resolved.cert_subject,
               sizeof(out_identity->cert_subject));
        memcpy(out_identity->content_hash, reply.resolved.content_hash,
               sizeof(out_identity->content_hash));
    }
    *out_verdict = reply.verdict;
    *out_result = reply.result;
    return SARAPI_OK;
}

sarapi_result_t __cdecl sarapi_resolve_identity(const uint16_t *image_path,
                                                sarapi_identity_t *out_identity,
                                                uint32_t *out_verdict,
                                                int32_t *out_result)
{
    if (!image_path || !out_identity || !out_verdict || !out_result)
        return SARAPI_INVALID_ARG;
    *out_verdict = 0;
    *out_result = -1;
    memset(out_identity, 0, sizeof(*out_identity));
    return identity_op(SAR_CTL_OP_RESOLVE_IDENTITY, image_path,
                       out_identity, out_verdict, out_result);
}

sarapi_result_t __cdecl sarapi_whitelist_add(const uint16_t *image_path,
                                             uint32_t *out_verdict,
                                             int32_t *out_result)
{
    if (!image_path || !out_verdict || !out_result)
        return SARAPI_INVALID_ARG;
    *out_verdict = 0;
    *out_result = -1;
    return identity_op(SAR_CTL_OP_WHITELIST_ADD, image_path,
                       NULL, out_verdict, out_result);
}

sarapi_result_t __cdecl sarapi_whitelist_remove(const uint16_t *image_path,
                                                uint32_t *out_verdict,
                                                int32_t *out_result)
{
    if (!image_path || !out_verdict || !out_result)
        return SARAPI_INVALID_ARG;
    *out_verdict = 0;
    *out_result = -1;
    return identity_op(SAR_CTL_OP_WHITELIST_REMOVE, image_path,
                       NULL, out_verdict, out_result);
}
