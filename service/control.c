#include "control.h"

#include <string.h>

#include "semantics_ar/protocol.h"
#include "pipe_server.h"
#include "posture.h"
#include "recovery.h"

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

static int sar_preserve_fetch(sar_comm_client_t *client, uint32_t index,
                              semantics_ar_preserve_entry_t *out_entry,
                              uint32_t *out_total, uint32_t *out_valid)
{
    semantics_ar_preserve_query_t q;
    semantics_ar_preserve_reply_t r;
    sar_comm_status_t cs;

    memset(&q, 0, sizeof(q));
    q.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    q.header.message_type = SEMANTICS_AR_MSG_PRESERVE_QUERY;
    q.header.message_length = (uint32_t)sizeof(q);
    q.index = index;

    memset(&r, 0, sizeof(r));
    cs = sar_comm_send_recv(client, &q, (uint32_t)sizeof(q),
                            &r, (uint32_t)sizeof(r),
                            SEMANTICS_AR_MSG_PRESERVE_REPLY);
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
        if (cs == SAR_COMM_OK)
            InterlockedExchange((volatile LONG *)&client->mode, (LONG)cmd->mode);
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

    case SAR_CTL_OP_RESOLVE_IDENTITY: {
        sar_identity_eval_t eval;
        wchar_t path[SEMANTICS_AR_PROTO_PATH_MAX];
        uint32_t i;

        for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX
                    && cmd->image_path[i] != 0; i++)
            path[i] = (wchar_t)cmd->image_path[i];
        path[i] = L'\0';

        memset(&eval, 0, sizeof(eval));
        reply->verdict = sar_identity_evaluate(path, &eval);
        memcpy(&reply->resolved, &eval.identity, sizeof(reply->resolved));
        reply->result = (reply->verdict == SAR_IDENTITY_VERDICT_ERROR
                         || reply->verdict == SAR_IDENTITY_VERDICT_PATH_FAILED)
                        ? -1 : 0;
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
            reply->entries[n].capture_time = e.capture_time;
            reply->entries[n].actor_start_key = e.actor_start_key;
        }
        reply->total = total;
        reply->returned = n;
        reply->result = 0;
        reply->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
        break;
    }

    case SAR_CTL_OP_PRESERVE_LIST: {
        uint32_t start = cmd->mode;
        uint32_t n = 0;
        uint32_t total = 0;

        for (n = 0; n < SAR_CTL_LIST_PAGE; n++) {
            semantics_ar_preserve_entry_t e;
            uint32_t valid = 0;

            if (sar_preserve_fetch(client, start + n, &e, &total, &valid) != 0) {
                reply->result = -1;
                reply->verdict = SAR_IDENTITY_VERDICT_ERROR;
                return 0;
            }
            if (!valid)
                break;

            memcpy(reply->preserve_entries[n].provenance_path, e.provenance_path,
                   sizeof(reply->preserve_entries[n].provenance_path));
            reply->preserve_entries[n].offset = e.provenance_offset;
            reply->preserve_entries[n].length = e.provenance_length;
            reply->preserve_entries[n].capture_time = e.capture_time;
            reply->preserve_entries[n].size = e.payload_length;
        }
        reply->total = total;
        reply->returned = n;
        reply->result = 0;
        reply->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
        break;
    }

    case SAR_CTL_OP_PRESERVE_RECOVER: {
        wchar_t  path[SEMANTICS_AR_PROTO_PATH_MAX];
        uint64_t bytes = 0;
        uint32_t i;

        for (i = 0; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX && cmd->image_path[i] != 0; i++)
            path[i] = (wchar_t)cmd->image_path[i];
        path[i] = L'\0';

        reply->result = sar_preserve_recovery_run(client, path, cmd->arg0, cmd->arg1, &bytes);
        reply->verdict = (reply->result == 0) ? SAR_IDENTITY_VERDICT_VERIFIED
                                              : SAR_IDENTITY_VERDICT_ERROR;
        break;
    }

    case SAR_CTL_OP_SET_BUDGET: {
        semantics_ar_set_budget_t msg;

        memset(&msg, 0, sizeof(msg));
        msg.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
        msg.header.message_type = SEMANTICS_AR_MSG_SET_BUDGET;
        msg.header.message_length = (uint32_t)sizeof(msg);
        msg.retention_100ns = cmd->arg0;
        msg.capacity_bytes = cmd->arg1;

        cs = sar_comm_send(client, &msg, (uint32_t)sizeof(msg));
        reply->result = (cs == SAR_COMM_OK) ? 0 : (int32_t)cs;
        reply->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
        break;
    }

    default:
        reply->result = -1;
        break;
    }

    return 0;
}

typedef struct {
    sar_comm_client_t *client;
    CRITICAL_SECTION  *lock;
} sar_control_ctx_t;

#define SAR_CONTROL_INSTANCES    4u
#define SAR_POSTURE_INSTANCES    4u
#define SAR_CONTROL_READ_TIMEOUT 5000u

static sar_pipe_server_t g_control_server;
static sar_pipe_server_t g_posture_server;
static CRITICAL_SECTION  g_control_lock;
static sar_control_ctx_t g_control_ctx;

static BOOL sar_control_caller_is_admin(void)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    BOOL is_member = FALSE;

    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins)) {
        if (!CheckTokenMembership(NULL, admins, &is_member))
            is_member = FALSE;
        FreeSid(admins);
    }
    return is_member;
}

static void sar_control_serve(sar_pipe_conn_t *conn, void *vctx)
{
    sar_control_ctx_t    *ctx = (sar_control_ctx_t *)vctx;
    sar_control_command_t cmd;
    sar_control_reply_t   reply;
    DWORD                 got = 0;
    BOOL                  authorized = FALSE;

    memset(&cmd, 0, sizeof(cmd));
    memset(&reply, 0, sizeof(reply));
    reply.result = -1;
    reply.verdict = SAR_IDENTITY_VERDICT_ERROR;

    if (!sar_pipe_recv(conn, &cmd, (DWORD)sizeof(cmd), &got,
                       SAR_CONTROL_READ_TIMEOUT)
        || got != (DWORD)sizeof(cmd)) {
        sar_pipe_send(conn, &reply, (DWORD)sizeof(reply));
        return;
    }

    if (ImpersonateNamedPipeClient(conn->pipe)) {
        authorized = sar_control_caller_is_admin();
        RevertToSelf();
    }

    if (!authorized) {
        sar_pipe_send(conn, &reply, (DWORD)sizeof(reply));
        return;
    }

    EnterCriticalSection(ctx->lock);
    sar_control_apply(ctx->client, &cmd, &reply);
    LeaveCriticalSection(ctx->lock);

    sar_pipe_send(conn, &reply, (DWORD)sizeof(reply));
}

static uint32_t sar_coarsen_health(uint64_t used, uint64_t capacity)
{
    uint64_t pct;

    if (capacity == 0)
        return SAR_POSTURE_PRESERVE_UNKNOWN;
    pct = (used * 100) / capacity;
    if (pct >= 90)
        return SAR_POSTURE_PRESERVE_CRITICAL;
    if (pct >= 75)
        return SAR_POSTURE_PRESERVE_LOW;
    return SAR_POSTURE_PRESERVE_HEALTHY;
}

static uint32_t sar_coarsen_expiry(uint64_t oldest_protected_100ns, uint64_t retention_100ns)
{
    FILETIME ft;
    ULARGE_INTEGER now;
    uint64_t expiry;
    uint64_t remaining;

    if (oldest_protected_100ns == 0)
        return SAR_POSTURE_EXPIRY_NONE;

    GetSystemTimeAsFileTime(&ft);
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;

    expiry = oldest_protected_100ns + retention_100ns;
    if (expiry <= now.QuadPart)
        return SAR_POSTURE_EXPIRY_LE_24H;

    remaining = expiry - now.QuadPart;
    if (remaining <= 24ULL * 3600ULL * 10000000ULL)
        return SAR_POSTURE_EXPIRY_LE_24H;
    if (remaining <= 7ULL * 24ULL * 3600ULL * 10000000ULL)
        return SAR_POSTURE_EXPIRY_LE_7D;
    return SAR_POSTURE_EXPIRY_GT_7D;
}

static void sar_posture_serve(sar_pipe_conn_t *conn, void *vctx)
{
    sar_comm_client_t          *client = (sar_comm_client_t *)vctx;
    sar_posture_frame_t         frame;
    semantics_ar_status_reply_t status;
    sar_comm_status_t           cs;

    memset(&frame, 0, sizeof(frame));
    frame.frame_type = SAR_POSTURE_FRAME_STATUS;
    frame.frame_length = (uint32_t)sizeof(frame);
    frame.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    frame.flags = SAR_POSTURE_FLAG_SERVICE_RUNNING;

    memset(&status, 0, sizeof(status));
    EnterCriticalSection(&g_control_lock);
    cs = sar_comm_query_status(client, &status);
    LeaveCriticalSection(&g_control_lock);

    if (cs == SAR_COMM_OK) {
        frame.flags |= SAR_POSTURE_FLAG_DRIVER_CONNECTED;
        frame.mode = status.mode;
        frame.captured_key_count = status.captured_key_count;
        frame.preserve_health = sar_coarsen_health(status.preserve_used_bytes,
                                                   status.preserve_capacity_bytes);
        frame.oldest_expiry_bucket = sar_coarsen_expiry(
            status.preserve_oldest_protected_time, status.preserve_retention_100ns);
    } else {
        frame.mode = client->mode;
        frame.captured_key_count = (uint64_t)InterlockedCompareExchange64(
            (volatile LONG64 *)&client->captured_key_count, 0, 0);
    }
    frame.descents = sar_posture_descents();

    sar_pipe_send(conn, &frame, (DWORD)sizeof(frame));
}

int sar_control_listener_start(sar_comm_client_t *client)
{
    if (!client)
        return -1;

    InitializeCriticalSection(&g_control_lock);
    g_control_ctx.client = client;
    g_control_ctx.lock = &g_control_lock;

    if (sar_pipe_server_start(&g_control_server, SAR_CONTROL_PIPE_NAME,
                              L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                              PIPE_ACCESS_DUPLEX,
                              (DWORD)sizeof(sar_control_reply_t),
                              (DWORD)sizeof(sar_control_command_t),
                              SAR_CONTROL_INSTANCES,
                              sar_control_serve, &g_control_ctx) != 0) {
        DeleteCriticalSection(&g_control_lock);
        return -1;
    }

    if (sar_pipe_server_start(&g_posture_server, SAR_POSTURE_PIPE_NAME,
                              L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x120189;;;IU)",
                              PIPE_ACCESS_OUTBOUND,
                              (DWORD)sizeof(sar_posture_frame_t), 0,
                              SAR_POSTURE_INSTANCES,
                              sar_posture_serve, client) != 0) {
        sar_pipe_server_stop(&g_control_server);
        DeleteCriticalSection(&g_control_lock);
        return -1;
    }

    return 0;
}

void sar_control_listener_stop(void)
{
    sar_pipe_server_stop(&g_posture_server);
    sar_pipe_server_stop(&g_control_server);
    DeleteCriticalSection(&g_control_lock);
}
