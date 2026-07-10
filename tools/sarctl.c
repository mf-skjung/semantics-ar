#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"

static int hex_to_key(const char *hex, uint8_t *out, size_t n)
{
    size_t i;
    if (strlen(hex) != n * 2)
        return -1;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1)
            return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

static int send_command(const sar_control_command_t *cmd, sar_control_reply_t *reply)
{
    HANDLE pipe;
    DWORD put = 0, got = 0;
    BOOL ok;

    pipe = CreateFileW(SAR_CONTROL_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"connect failed (%lu) - is the service running and are you elevated?\n",
                 GetLastError());
        return -1;
    }
    ok = WriteFile(pipe, cmd, (DWORD)sizeof(*cmd), &put, NULL) &&
         ReadFile(pipe, reply, (DWORD)sizeof(*reply), &got, NULL) &&
         got >= sizeof(int32_t) + sizeof(uint32_t);
    CloseHandle(pipe);
    if (!ok) {
        fwprintf(stderr, L"transport failed (%lu)\n", GetLastError());
        return -1;
    }
    return 0;
}

static void put_path(const char *utf8, uint16_t *out)
{
    wchar_t w[SEMANTICS_AR_PROTO_PATH_MAX];
    uint32_t i = 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, SEMANTICS_AR_PROTO_PATH_MAX);
    if (n > 0)
        for (; i + 1 < SEMANTICS_AR_PROTO_PATH_MAX && w[i] != 0; i++)
            out[i] = (uint16_t)w[i];
    out[i] = 0;
}

static int cmd_list(void)
{
    uint32_t start = 0, total = 0;

    do {
        sar_control_command_t cmd;
        sar_control_reply_t reply;
        uint32_t i;

        memset(&cmd, 0, sizeof cmd);
        cmd.op = SAR_CTL_OP_LIST;
        cmd.mode = start;
        if (send_command(&cmd, &reply) != 0)
            return 1;

        total = reply.total;
        if (reply.returned == 0)
            break;
        for (i = 0; i < reply.returned; i++) {
            const sar_catalog_entry_t *e = &reply.entries[i];
            wchar_t path[SEMANTICS_AR_PROTO_PATH_MAX];
            uint32_t j = 0;
            int k;
            for (; j < SEMANTICS_AR_PROTO_PATH_MAX && e->provenance_path[j] != 0; j++)
                path[j] = (wchar_t)e->provenance_path[j];
            path[j] = 0;
            wprintf(L"[%u] alg=%u mode=%u key_id=", start + i, e->algorithm, e->mode);
            for (k = 0; k < SEMANTICS_AR_KEY_ID_SIZE; k++)
                wprintf(L"%02x", e->key_id[k]);
            wprintf(L"  %ls\n", path);
        }
        start += reply.returned;
    } while (start < total);

    wprintf(L"%u captured key(s)\n", total);
    return 0;
}

static int cmd_recover(const char *keyhex, const char *path)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_RECOVER;
    if (hex_to_key(keyhex, cmd.key_id, SEMANTICS_AR_KEY_ID_SIZE) != 0) {
        fwprintf(stderr, L"key_id must be %d hex characters\n", SEMANTICS_AR_KEY_ID_SIZE * 2);
        return 2;
    }
    put_path(path, cmd.image_path);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"recover result=%d\n", reply.result);
    return reply.result == 0 ? 0 : 3;
}

static int cmd_preserve_list(void)
{
    uint32_t start = 0, total = 0;

    do {
        sar_control_command_t cmd;
        sar_control_reply_t reply;
        uint32_t i;

        memset(&cmd, 0, sizeof cmd);
        cmd.op = SAR_CTL_OP_PRESERVE_LIST;
        cmd.mode = start;
        if (send_command(&cmd, &reply) != 0)
            return 1;

        total = reply.total;
        if (reply.returned == 0)
            break;
        for (i = 0; i < reply.returned; i++) {
            const sar_preserve_list_entry_t *e = &reply.preserve_entries[i];
            wchar_t path[SEMANTICS_AR_PROTO_PATH_MAX];
            uint32_t j = 0;
            for (; j < SEMANTICS_AR_PROTO_PATH_MAX && e->provenance_path[j] != 0; j++)
                path[j] = (wchar_t)e->provenance_path[j];
            path[j] = 0;
            wprintf(L"[%u] off=%llu len=%llu size=%llu actor=%llu pool=%u app=%llu  %ls\n", start + i,
                    (unsigned long long)e->offset, (unsigned long long)e->length,
                    (unsigned long long)e->size, (unsigned long long)e->actor_start_key,
                    e->state, (unsigned long long)e->app_identity_id, path);
        }
        start += reply.returned;
    } while (start < total);

    wprintf(L"%u preserved region(s)\n", total);
    return 0;
}

static int cmd_app_identity(void)
{
    uint32_t start = 0, total = 0;

    do {
        sar_control_command_t cmd;
        sar_control_reply_t reply;
        uint32_t i;

        memset(&cmd, 0, sizeof cmd);
        cmd.op = SAR_CTL_OP_APP_IDENTITY_LIST;
        cmd.mode = start;
        if (send_command(&cmd, &reply) != 0)
            return 1;

        total = reply.total;
        if (reply.returned == 0)
            break;
        for (i = 0; i < reply.returned; i++) {
            const sar_app_identity_entry_t *e = &reply.app_identities[i];
            wchar_t path[SEMANTICS_AR_PROTO_PATH_MAX];
            wchar_t subj[SEMANTICS_AR_PROTO_SUBJECT_MAX];
            uint32_t j = 0;
            for (; j < SEMANTICS_AR_PROTO_PATH_MAX && e->image_path[j] != 0; j++)
                path[j] = (wchar_t)e->image_path[j];
            path[j] = 0;
            for (j = 0; j < SEMANTICS_AR_PROTO_SUBJECT_MAX && e->cert_subject[j] != 0; j++)
                subj[j] = (wchar_t)e->cert_subject[j];
            subj[j] = 0;
            wprintf(L"[app] id=%llu verdict=%u signer=\"%ls\"  %ls\n",
                    (unsigned long long)e->app_identity_id, e->verdict, subj, path);
        }
        start += reply.returned;
    } while (start < total);

    wprintf(L"%u app identit(ies)\n", total);
    return 0;
}

static int cmd_preserve_recover(const char *path, const char *off, const char *len)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_PRESERVE_RECOVER;
    put_path(path, cmd.image_path);
    cmd.arg0 = _strtoui64(off, NULL, 10);
    cmd.arg1 = _strtoui64(len, NULL, 10);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"preserve-recover result=%d\n", reply.result);
    return reply.result == 0 ? 0 : 3;
}

static int cmd_budget(const char *retention_sec, const char *capacity_mb)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_SET_BUDGET;
    cmd.arg0 = _strtoui64(retention_sec, NULL, 10) * 10000000ull;
    cmd.arg1 = _strtoui64(capacity_mb, NULL, 10) * 1024ull * 1024ull;
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"budget result=%d\n", reply.result);
    return reply.result == 0 ? 0 : 3;
}

static int cmd_status(void)
{
    HANDLE pipe;
    sar_posture_frame_t frame;
    DWORD got = 0;

    pipe = CreateFileW(SAR_POSTURE_PIPE_NAME, GENERIC_READ, 0, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"posture connect failed (%lu) - is the service running?\n",
                 GetLastError());
        return 1;
    }

    memset(&frame, 0, sizeof frame);
    if (!ReadFile(pipe, &frame, (DWORD)sizeof frame, &got, NULL)
        || got < sizeof frame) {
        fwprintf(stderr, L"posture read failed (%lu)\n", GetLastError());
        CloseHandle(pipe);
        return 1;
    }
    CloseHandle(pipe);

    wprintf(L"service_running=%d driver_connected=%d mode=%ls captured=%llu\n",
            (frame.flags & SAR_POSTURE_FLAG_SERVICE_RUNNING) ? 1 : 0,
            (frame.flags & SAR_POSTURE_FLAG_DRIVER_CONNECTED) ? 1 : 0,
            frame.mode == SEMANTICS_AR_MODE_ENFORCE ? L"enforce" : L"audit",
            (unsigned long long)frame.captured_key_count);
    return 0;
}

static const wchar_t *event_class_str(uint32_t c)
{
    switch (c) {
    case SAR_EVENT_CLASS_KEY_CAPTURED:      return L"key-captured";
    case SAR_EVENT_CLASS_BLOCK_FORWARD:     return L"block-forward";
    case SAR_EVENT_CLASS_BLOCK_PHANTOM:     return L"block-phantom";
    case SAR_EVENT_CLASS_BLOCK_CAPACITY:    return L"block-capacity";
    case SAR_EVENT_CLASS_MODE_CHANGED:      return L"mode-changed";
    case SAR_EVENT_CLASS_WHITELIST_ADDED:   return L"whitelist-added";
    case SAR_EVENT_CLASS_WHITELIST_REMOVED: return L"whitelist-removed";
    default:                                return L"unknown";
    }
}

static int events_read_frame(HANDLE pipe, sar_events_frame_t *frame)
{
    uint8_t *buf = (uint8_t *)frame;
    DWORD have = 0;

    while (have < (DWORD)sizeof(*frame)) {
        DWORD got = 0;
        if (!ReadFile(pipe, buf + have, (DWORD)sizeof(*frame) - have, &got, NULL) || got == 0)
            return -1;
        have += got;
    }
    return 0;
}

static int cmd_events(int count)
{
    HANDLE pipe;
    int i;

    pipe = CreateFileW(SAR_EVENTS_PIPE_NAME, GENERIC_READ, 0, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"events connect failed (%lu) - is the service running?\n",
                 GetLastError());
        return 1;
    }

    for (i = 0; i < count; i++) {
        sar_events_frame_t frame;

        memset(&frame, 0, sizeof frame);
        if (events_read_frame(pipe, &frame) != 0) {
            fwprintf(stderr, L"events read failed (%lu)\n", GetLastError());
            CloseHandle(pipe);
            return 1;
        }
        if (!frame.valid)
            break;
        wprintf(L"event %d: class=%ls gap=%d generation=%llu sequence=%llu timestamp=%llu actor=%016llx\n",
                i, event_class_str(frame.event_class), frame.gap,
                (unsigned long long)frame.generation, (unsigned long long)frame.sequence,
                (unsigned long long)frame.timestamp, (unsigned long long)frame.actor_start_key);
    }

    CloseHandle(pipe);
    return 0;
}

static const wchar_t *verdict_str(uint32_t v)
{
    switch (v) {
    case 0: return L"verified";
    case 1: return L"unsigned";
    case 2: return L"hash-failed";
    case 3: return L"path-failed";
    default: return L"error";
    }
}

static int cmd_resolve(const char *path)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;
    wchar_t img[SEMANTICS_AR_PROTO_PATH_MAX], subj[SEMANTICS_AR_PROTO_SUBJECT_MAX];
    uint32_t j;
    int k;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_RESOLVE_IDENTITY;
    put_path(path, cmd.image_path);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    if (reply.result != 0) {
        wprintf(L"resolve verdict=%ls (could not resolve)\n", verdict_str(reply.verdict));
        return 3;
    }
    for (j = 0; j + 1 < SEMANTICS_AR_PROTO_PATH_MAX && reply.resolved.image_path[j] != 0; j++)
        img[j] = (wchar_t)reply.resolved.image_path[j];
    img[j] = 0;
    for (j = 0; j + 1 < SEMANTICS_AR_PROTO_SUBJECT_MAX && reply.resolved.cert_subject[j] != 0; j++)
        subj[j] = (wchar_t)reply.resolved.cert_subject[j];
    subj[j] = 0;
    wprintf(L"verdict=%ls\n  image =%ls\n  signer=%ls\n  sha256=", verdict_str(reply.verdict), img, subj);
    for (k = 0; k < SEMANTICS_AR_CONTENT_HASH_SIZE; k++)
        wprintf(L"%02x", reply.resolved.content_hash[k]);
    wprintf(L"\n");
    return 0;
}

static int cmd_whitelist(uint32_t op, const char *path)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = op;
    put_path(path, cmd.image_path);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"whitelist result=%d verdict=%ls\n", reply.result, verdict_str(reply.verdict));
    return reply.result == 0 ? 0 : 3;
}

static int cmd_mode(const char *m)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_SET_MODE;
    if (strcmp(m, "audit") == 0)
        cmd.mode = SEMANTICS_AR_MODE_AUDIT;
    else if (strcmp(m, "enforce") == 0)
        cmd.mode = SEMANTICS_AR_MODE_ENFORCE;
    else {
        fwprintf(stderr, L"mode must be audit or enforce\n");
        return 2;
    }
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"mode result=%d\n", reply.result);
    return reply.result == 0 ? 0 : 3;
}

static const wchar_t *idstate_str(uint32_t s)
{
    switch (s) {
    case 0:  return L"observe-pending";
    case 1:  return L"observe";
    case 2:  return L"exempt";
    default: return L"?";
    }
}

static int cmd_procquery(const char *pidstr)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_PROCESS_QUERY;
    cmd.arg0 = (uint64_t)strtoull(pidstr, NULL, 10);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    if (reply.result != 0) {
        wprintf(L"procquery pid=%llu not-found\n", (unsigned long long)cmd.arg0);
        return 2;
    }
    wprintf(L"procquery pid=%llu id_state=%u(%ls) start_key=%llu\n",
            (unsigned long long)cmd.arg0, reply.id_state, idstate_str(reply.id_state),
            (unsigned long long)reply.proc_start_key);
    return 0;
}

static int cmd_verdict(const char *pidstr)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_VERDICT;
    cmd.arg0 = (uint64_t)strtoull(pidstr, NULL, 10);
    if (send_command(&cmd, &reply) != 0)
        return 1;
    wprintf(L"verdict pid=%llu result=%d verdict=%ls id_state_before=%u(%ls)\n",
            (unsigned long long)cmd.arg0, reply.result, verdict_str(reply.verdict),
            reply.id_state, idstate_str(reply.id_state));
    return reply.result == 0 ? 0 : 3;
}

static int cmd_inflight(void)
{
    sar_control_command_t cmd;
    sar_control_reply_t reply;

    memset(&cmd, 0, sizeof cmd);
    cmd.op = SAR_CTL_OP_STATUS;
    if (send_command(&cmd, &reply) != 0)
        return 1;
    if (reply.result != 0)
        return 1;
    wprintf(L"inflight=%llu used=%llu\n",
            (unsigned long long)reply.capture_inflight,
            (unsigned long long)reply.preserve_used_bytes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "list") == 0)
        return cmd_list();
    if (argc == 4 && strcmp(argv[1], "recover") == 0)
        return cmd_recover(argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "mode") == 0)
        return cmd_mode(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "preserve-list") == 0)
        return cmd_preserve_list();
    if (argc >= 2 && strcmp(argv[1], "app-identity") == 0)
        return cmd_app_identity();
    if (argc == 5 && strcmp(argv[1], "preserve-recover") == 0)
        return cmd_preserve_recover(argv[2], argv[3], argv[4]);
    if (argc == 4 && strcmp(argv[1], "budget") == 0)
        return cmd_budget(argv[2], argv[3]);
    if (argc >= 2 && strcmp(argv[1], "status") == 0)
        return cmd_status();
    if (argc == 3 && strcmp(argv[1], "resolve") == 0)
        return cmd_resolve(argv[2]);
    if (argc == 3 && strcmp(argv[1], "whitelist-add") == 0)
        return cmd_whitelist(SAR_CTL_OP_WHITELIST_ADD, argv[2]);
    if (argc == 3 && strcmp(argv[1], "whitelist-remove") == 0)
        return cmd_whitelist(SAR_CTL_OP_WHITELIST_REMOVE, argv[2]);
    if (argc == 2 && strcmp(argv[1], "events") == 0)
        return cmd_events(1);
    if (argc == 3 && strcmp(argv[1], "events") == 0)
        return cmd_events(atoi(argv[2]));
    if (argc == 3 && strcmp(argv[1], "verdict") == 0)
        return cmd_verdict(argv[2]);
    if (argc == 3 && strcmp(argv[1], "procquery") == 0)
        return cmd_procquery(argv[2]);
    if (argc == 2 && strcmp(argv[1], "inflight") == 0)
        return cmd_inflight();

    fwprintf(stderr,
             L"usage: sarctl status | list | recover <key_id-hex> <path> | mode <audit|enforce>\n"
             L"       | preserve-list | preserve-recover <path> <offset> <length>\n"
             L"       | budget <retention-seconds> <capacity-MB>\n"
             L"       | resolve <path> | whitelist-add <path> | whitelist-remove <path>\n"
             L"       | events [count]\n");
    return 2;
}
