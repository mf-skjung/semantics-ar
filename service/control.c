#include "control.h"

#include <string.h>
#include <sddl.h>

#include "semantics_ar/protocol.h"
#include "recovery.h"

typedef struct {
    sar_comm_client_t *client;
    volatile LONG      running;
} sar_control_listener_t;

static sar_control_listener_t g_control_listener;

static int sar_catalog_fetch(sar_comm_client_t *client, uint32_t index,
                             semantics_ar_catalog_entry_t *out_entry,
                             uint32_t *out_total, uint32_t *out_valid)
{
    semantics_ar_catalog_query_t q;
    semantics_ar_catalog_reply_t r;
    sar_comm_status_t cs;

    memset(&q, 0, sizeof(q));
    q.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    q.header.message_type = SEMANTICS_AR_MSG_CATALOG_QUERY;
    q.header.message_length = (uint32_t)sizeof(q);
    q.index = index;

    memset(&r, 0, sizeof(r));
    cs = sar_comm_send_recv(client, &q, (uint32_t)sizeof(q),
                            &r, (uint32_t)sizeof(r),
                            SEMANTICS_AR_MSG_CATALOG_REPLY);
    if (cs != SAR_COMM_OK)
        return -1;

    *out_total = r.total;
    *out_valid = r.valid;
    if (r.valid)
        memcpy(out_entry, &r.entry, sizeof(*out_entry));
    return 0;
}

sar_comm_status_t sar_control_send_mode(sar_comm_client_t *client,
                                        uint32_t mode)
{
    semantics_ar_set_mode_t msg;

    if (mode != SEMANTICS_AR_MODE_AUDIT && mode != SEMANTICS_AR_MODE_ENFORCE)
        return SAR_COMM_ERR_PROTOCOL;

    memset(&msg, 0, sizeof(msg));
    msg.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    msg.header.message_type = SEMANTICS_AR_MSG_SET_MODE;
    msg.header.message_length = (uint32_t)sizeof(msg);
    msg.mode = mode;

    return sar_comm_send(client, &msg, (uint32_t)sizeof(msg));
}

sar_comm_status_t sar_control_send_whitelist(sar_comm_client_t *client,
                                             uint32_t op,
                                             const sar_identity_t *identity)
{
    semantics_ar_whitelist_control_t msg;
    uint32_t type;

    if (!identity)
        return SAR_COMM_ERR_PROTOCOL;

    if (op == SAR_CTL_OP_WHITELIST_ADD)
        type = SEMANTICS_AR_MSG_WHITELIST_ADD;
    else if (op == SAR_CTL_OP_WHITELIST_REMOVE)
        type = SEMANTICS_AR_MSG_WHITELIST_REMOVE;
    else
        return SAR_COMM_ERR_PROTOCOL;

    memset(&msg, 0, sizeof(msg));
    msg.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    msg.header.message_type = type;
    msg.header.message_length = (uint32_t)sizeof(msg);
    memcpy(msg.image_path, identity->image_path, sizeof(msg.image_path));
    memcpy(msg.cert_subject, identity->cert_subject, sizeof(msg.cert_subject));
    memcpy(msg.content_hash, identity->content_hash, sizeof(msg.content_hash));

    return sar_comm_send(client, &msg, (uint32_t)sizeof(msg));
}

int sar_control_apply(sar_comm_client_t *client,
                      const sar_control_command_t *cmd,
                      sar_control_reply_t *reply)
{
    sar_comm_status_t cs;

    if (!client || !cmd || !reply)
        return -1;

    reply->result = -1;
    reply->verdict = SAR_IDENTITY_VERDICT_ERROR;

    switch (cmd->op) {
    case SAR_CTL_OP_SET_MODE:
        cs = sar_control_send_mode(client, cmd->mode);
        reply->result = (cs == SAR_COMM_OK) ? 0 : (int32_t)cs;
        reply->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
        break;

    case SAR_CTL_OP_WHITELIST_ADD:
    case SAR_CTL_OP_WHITELIST_REMOVE: {
        sar_identity_eval_t eval;
        wchar_t path[SEMANTICS_AR_PROTO_PATH_MAX];
        uint32_t i;

        for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX
                    && cmd->image_path[i] != 0; i++)
            path[i] = (wchar_t)cmd->image_path[i];
        path[i] = L'\0';

        memset(&eval, 0, sizeof(eval));
        reply->verdict = sar_identity_evaluate(path, &eval);
        if (reply->verdict != SAR_IDENTITY_VERDICT_VERIFIED) {
            reply->result = -1;
            break;
        }

        cs = sar_control_send_whitelist(client, cmd->op, &eval.identity);
        reply->result = (cs == SAR_COMM_OK) ? 0 : (int32_t)cs;
        break;
    }

    case SAR_CTL_OP_RECOVER: {
        wchar_t  path[SEMANTICS_AR_PROTO_PATH_MAX];
        uint64_t bytes = 0;
        uint32_t i;

        for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX && cmd->image_path[i] != 0; i++)
            path[i] = (wchar_t)cmd->image_path[i];
        path[i] = L'\0';

        reply->result = sar_recovery_run(client, cmd->key_id, path, &bytes);
        reply->verdict = (reply->result == 0) ? SAR_IDENTITY_VERDICT_VERIFIED
                                              : SAR_IDENTITY_VERDICT_ERROR;
        break;
    }

    case SAR_CTL_OP_LIST: {
        uint32_t start = cmd->mode;
        uint32_t n = 0;
        uint32_t total = 0;

        for (n = 0; n < SAR_CTL_LIST_PAGE; n++) {
            semantics_ar_catalog_entry_t e;
            uint32_t valid = 0;

            if (sar_catalog_fetch(client, start + n, &e, &total, &valid) != 0) {
                reply->result = -1;
                reply->verdict = SAR_IDENTITY_VERDICT_ERROR;
                return 0;
            }
            if (!valid)
                break;

            memcpy(reply->entries[n].key_id, e.key_id, SEMANTICS_AR_KEY_ID_SIZE);
            reply->entries[n].algorithm = e.algorithm;
            reply->entries[n].mode = e.mode;
            memcpy(reply->entries[n].provenance_path, e.provenance_path,
                   sizeof(reply->entries[n].provenance_path));
        }
        reply->total = total;
        reply->returned = n;
        reply->result = 0;
        reply->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
        break;
    }

    default:
        reply->result = -1;
        break;
    }

    return 0;
}

static void sar_control_serve(sar_comm_client_t *client, HANDLE pipe)
{
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    DWORD                 got = 0;
    DWORD                 put = 0;

    memset(&cmd, 0, sizeof(cmd));
    memset(&reply, 0, sizeof(reply));

    if (ReadFile(pipe, &cmd, (DWORD)sizeof(cmd), &got, NULL)
        && got == (DWORD)sizeof(cmd)) {
        sar_control_apply(client, &cmd, &reply);
    } else {
        reply.result = -1;
        reply.verdict = SAR_IDENTITY_VERDICT_ERROR;
    }

    WriteFile(pipe, &reply, (DWORD)sizeof(reply), &put, NULL);
    FlushFileBuffers(pipe);
}

static DWORD WINAPI sar_control_thread(LPVOID param)
{
    sar_control_listener_t *self = (sar_control_listener_t *)param;
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = NULL;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd, NULL))
        return 0;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    while (InterlockedCompareExchange(&self->running, 1, 1) == 1) {
        HANDLE pipe = CreateNamedPipeW(SAR_CONTROL_PIPE_NAME,
                                       PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE
                                       | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1,
                                       (DWORD)sizeof(sar_control_reply_t),
                                       (DWORD)sizeof(sar_control_command_t),
                                       0, &sa);
        if (pipe == INVALID_HANDLE_VALUE)
            break;

        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            if (InterlockedCompareExchange(&self->running, 1, 1) == 1)
                sar_control_serve(self->client, pipe);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    LocalFree(sd);
    return 0;
}

HANDLE sar_control_listener_start(sar_comm_client_t *client)
{
    g_control_listener.client = client;
    InterlockedExchange(&g_control_listener.running, 1);
    return CreateThread(NULL, 0, sar_control_thread, &g_control_listener,
                        0, NULL);
}

void sar_control_listener_stop(HANDLE thread)
{
    InterlockedExchange(&g_control_listener.running, 0);
    if (thread) {
        HANDLE client = CreateFileW(SAR_CONTROL_PIPE_NAME, GENERIC_READ,
                                    0, NULL, OPEN_EXISTING, 0, NULL);
        if (client != INVALID_HANDLE_VALUE)
            CloseHandle(client);
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
    }
}
