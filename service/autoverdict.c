#include "autoverdict.h"

#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>

#include "semantics_ar/protocol.h"

#define SAR_AV_MAX_PATHS 256u
#define SAR_AV_MAX_PIDS  4096u
#define SAR_AV_POLL_MS   250u

static CRITICAL_SECTION g_av_reg_lock;
static wchar_t g_av_paths[SAR_AV_MAX_PATHS][SEMANTICS_AR_PROTO_PATH_MAX];
static uint32_t g_av_count;
static int g_av_reg_ready;
static volatile LONG g_av_rescan;

static sar_comm_client_t *g_av_client;
static CRITICAL_SECTION *g_av_comm_lock;
static HANDLE g_av_thread;
static HANDLE g_av_stop;

void sar_autoverdict_note(const wchar_t *path, int add)
{
    uint32_t i;

    if (!g_av_reg_ready || path == NULL || path[0] == 0)
        return;

    EnterCriticalSection(&g_av_reg_lock);
    for (i = 0; i < g_av_count; i++)
        if (_wcsicmp(g_av_paths[i], path) == 0)
            break;

    if (add) {
        if (i == g_av_count && g_av_count < SAR_AV_MAX_PATHS) {
            wcsncpy_s(g_av_paths[g_av_count], SEMANTICS_AR_PROTO_PATH_MAX, path, _TRUNCATE);
            g_av_count++;
            InterlockedExchange(&g_av_rescan, 1);
        }
    } else if (i < g_av_count) {
        uint32_t j;
        for (j = i + 1; j < g_av_count; j++)
            wcsncpy_s(g_av_paths[j - 1], SEMANTICS_AR_PROTO_PATH_MAX, g_av_paths[j], _TRUNCATE);
        g_av_count--;
        g_av_paths[g_av_count][0] = 0;
    }
    LeaveCriticalSection(&g_av_reg_lock);
}

static int sar_av_path_is_candidate(const wchar_t *path)
{
    uint32_t i;
    int hit = 0;

    EnterCriticalSection(&g_av_reg_lock);
    for (i = 0; i < g_av_count; i++)
        if (_wcsicmp(g_av_paths[i], path) == 0) {
            hit = 1;
            break;
        }
    LeaveCriticalSection(&g_av_reg_lock);
    return hit;
}

sar_identity_verdict_t sar_verdict_pid(sar_comm_client_t *client, uint32_t pid,
                                       uint32_t *out_id_state, uint64_t *out_start_key)
{
    wchar_t image[SEMANTICS_AR_PROTO_PATH_MAX];
    DWORD nchars = SEMANTICS_AR_PROTO_PATH_MAX;
    HANDLE hproc;
    semantics_ar_process_query_t q;
    semantics_ar_process_reply_t pr;
    semantics_ar_identity_verdict_t v;
    sar_identity_eval_t eval;
    sar_comm_status_t cs;
    sar_identity_verdict_t verdict;

    if (out_id_state != NULL)
        *out_id_state = 0;
    if (out_start_key != NULL)
        *out_start_key = 0;

    image[0] = 0;
    hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (hproc != NULL) {
        if (!QueryFullProcessImageNameW(hproc, 0, image, &nchars))
            image[0] = 0;
        CloseHandle(hproc);
    }
    if (image[0] == 0)
        return SAR_IDENTITY_VERDICT_ERROR;

    memset(&q, 0, sizeof(q));
    q.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    q.header.message_type = SEMANTICS_AR_MSG_PROCESS_QUERY;
    q.header.message_length = (uint32_t)sizeof(q);
    q.pid = pid;
    memset(&pr, 0, sizeof(pr));
    cs = sar_comm_send_recv(client, &q, (uint32_t)sizeof(q), &pr, (uint32_t)sizeof(pr),
                            SEMANTICS_AR_MSG_PROCESS_REPLY);
    if (cs != SAR_COMM_OK || !pr.valid || pr.start_key == 0)
        return SAR_IDENTITY_VERDICT_ERROR;
    if (out_id_state != NULL)
        *out_id_state = pr.id_state;
    if (out_start_key != NULL)
        *out_start_key = pr.start_key;

    memset(&eval, 0, sizeof(eval));
    verdict = sar_identity_evaluate(image, &eval);
    if (verdict != SAR_IDENTITY_VERDICT_VERIFIED)
        return verdict;

    memset(&v, 0, sizeof(v));
    v.header.protocol_version = SEMANTICS_AR_PROTOCOL_VERSION;
    v.header.message_type = SEMANTICS_AR_MSG_IDENTITY_VERDICT;
    v.header.message_length = (uint32_t)sizeof(v);
    v.pid = pid;
    v.start_key = pr.start_key;
    memcpy(v.image_path, eval.identity.image_path, sizeof(v.image_path));
    memcpy(v.cert_subject, eval.identity.cert_subject, sizeof(v.cert_subject));
    memcpy(v.content_hash, eval.identity.content_hash, sizeof(v.content_hash));
    cs = sar_comm_send(client, &v, (uint32_t)sizeof(v));
    return (cs == SAR_COMM_OK) ? SAR_IDENTITY_VERDICT_VERIFIED : SAR_IDENTITY_VERDICT_ERROR;
}

static void sar_av_evaluate_pid(uint32_t pid)
{
    wchar_t image[SEMANTICS_AR_PROTO_PATH_MAX];
    DWORD nchars = SEMANTICS_AR_PROTO_PATH_MAX;
    HANDLE hproc;

    image[0] = 0;
    hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (hproc != NULL) {
        if (!QueryFullProcessImageNameW(hproc, 0, image, &nchars))
            image[0] = 0;
        CloseHandle(hproc);
    }
    if (image[0] == 0)
        return;
    if (!sar_av_path_is_candidate(image))
        return;

    EnterCriticalSection(g_av_comm_lock);
    (void)sar_verdict_pid(g_av_client, pid, NULL, NULL);
    LeaveCriticalSection(g_av_comm_lock);
}

static DWORD WINAPI sar_av_thread(LPVOID param)
{
    DWORD *prev;
    DWORD *cur;
    uint32_t prev_n = 0;

    (void)param;

    prev = (DWORD *)malloc(SAR_AV_MAX_PIDS * sizeof(DWORD));
    cur = (DWORD *)malloc(SAR_AV_MAX_PIDS * sizeof(DWORD));
    if (prev == NULL || cur == NULL) {
        free(prev);
        free(cur);
        return 0;
    }

    for (;;) {
        HANDLE snap;
        PROCESSENTRY32W pe;
        uint32_t cur_n = 0;

        if (WaitForSingleObject(g_av_stop, SAR_AV_POLL_MS) == WAIT_OBJECT_0)
            break;

        if (InterlockedExchange(&g_av_rescan, 0) == 1)
            prev_n = 0;

        snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            continue;

        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                DWORD pid = pe.th32ProcessID;
                uint32_t i;
                int seen = 0;

                if (pid == 0)
                    continue;
                if (cur_n < SAR_AV_MAX_PIDS)
                    cur[cur_n++] = pid;
                for (i = 0; i < prev_n; i++)
                    if (prev[i] == pid) {
                        seen = 1;
                        break;
                    }
                if (!seen)
                    sar_av_evaluate_pid((uint32_t)pid);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);

        memcpy(prev, cur, cur_n * sizeof(DWORD));
        prev_n = cur_n;
    }

    free(prev);
    free(cur);
    return 0;
}

int sar_autoverdict_start(sar_comm_client_t *client, CRITICAL_SECTION *comm_lock)
{
    if (client == NULL || comm_lock == NULL)
        return -1;

    InitializeCriticalSection(&g_av_reg_lock);
    g_av_count = 0;
    g_av_rescan = 0;
    g_av_reg_ready = 1;
    g_av_client = client;
    g_av_comm_lock = comm_lock;

    g_av_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_av_stop == NULL) {
        g_av_reg_ready = 0;
        DeleteCriticalSection(&g_av_reg_lock);
        return -1;
    }

    g_av_thread = CreateThread(NULL, 0, sar_av_thread, NULL, 0, NULL);
    if (g_av_thread == NULL) {
        CloseHandle(g_av_stop);
        g_av_stop = NULL;
        g_av_reg_ready = 0;
        DeleteCriticalSection(&g_av_reg_lock);
        return -1;
    }
    return 0;
}

void sar_autoverdict_stop(void)
{
    if (g_av_stop != NULL)
        SetEvent(g_av_stop);
    if (g_av_thread != NULL) {
        WaitForSingleObject(g_av_thread, INFINITE);
        CloseHandle(g_av_thread);
        g_av_thread = NULL;
    }
    if (g_av_stop != NULL) {
        CloseHandle(g_av_stop);
        g_av_stop = NULL;
    }
    if (g_av_reg_ready) {
        g_av_reg_ready = 0;
        DeleteCriticalSection(&g_av_reg_lock);
    }
}
