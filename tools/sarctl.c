#include <windows.h>
#include <stdio.h>
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
            wprintf(L"[%u] off=%llu len=%llu size=%llu  %ls\n", start + i,
                    (unsigned long long)e->offset, (unsigned long long)e->length,
                    (unsigned long long)e->size, path);
        }
        start += reply.returned;
    } while (start < total);

    wprintf(L"%u preserved region(s)\n", total);
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
    if (argc == 5 && strcmp(argv[1], "preserve-recover") == 0)
        return cmd_preserve_recover(argv[2], argv[3], argv[4]);
    if (argc == 4 && strcmp(argv[1], "budget") == 0)
        return cmd_budget(argv[2], argv[3]);

    fwprintf(stderr,
             L"usage: sarctl list | recover <key_id-hex> <path> | mode <audit|enforce>\n"
             L"       | preserve-list | preserve-recover <path> <offset> <length>\n"
             L"       | budget <retention-seconds> <capacity-MB>\n");
    return 2;
}
