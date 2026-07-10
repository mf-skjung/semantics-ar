#include "attrib.h"
#include "identity.h"

#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#define SAR_ATTRIB_MAX 512u

typedef struct {
    int      used;
    uint64_t id;
    sar_identity_eval_t eval;
} sar_attrib_app_t;

typedef struct {
    int      used;
    uint16_t nt_path[SEMANTICS_AR_PROTO_PATH_MAX];
    uint64_t last_write;
    uint64_t size;
    uint64_t id;
} sar_attrib_path_t;

static sar_attrib_app_t  g_apps[SAR_ATTRIB_MAX];
static sar_attrib_path_t g_paths[SAR_ATTRIB_MAX];
static CRITICAL_SECTION  g_lock;
static int               g_ready;

void sar_attrib_init(void)
{
    if (g_ready)
        return;
    InitializeCriticalSection(&g_lock);
    memset(g_apps, 0, sizeof(g_apps));
    memset(g_paths, 0, sizeof(g_paths));
    g_ready = 1;
}

static uint64_t sar_attrib_fnv(const uint16_t *s)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < SEMANTICS_AR_PROTO_PATH_MAX && s[i] != 0; i++) {
        h ^= (uint64_t)s[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t sar_attrib_hash_id(const uint8_t *hash)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        v |= (uint64_t)hash[i] << (i * 8);
    return v;
}

static int sar_attrib_nt_to_win32(const wchar_t *nt, wchar_t *out, size_t cap)
{
    wchar_t drive[3];
    wchar_t dev[512];
    wchar_t c;

    drive[1] = L':';
    drive[2] = 0;
    for (c = L'A'; c <= L'Z'; c++) {
        size_t dl;
        drive[0] = c;
        if (QueryDosDeviceW(drive, dev, 512) == 0)
            continue;
        dl = wcslen(dev);
        if (_wcsnicmp(nt, dev, dl) == 0 && (nt[dl] == L'\\' || nt[dl] == 0)) {
            _snwprintf_s(out, cap, _TRUNCATE, L"%s%s", drive, nt + dl);
            return 1;
        }
    }
    _snwprintf_s(out, cap, _TRUNCATE, L"\\\\?\\GLOBALROOT%s", nt);
    return 1;
}

static void sar_attrib_upsert_app(uint64_t id, const sar_identity_eval_t *ev)
{
    uint32_t i;
    int free_slot = -1;
    for (i = 0; i < SAR_ATTRIB_MAX; i++) {
        if (g_apps[i].used && g_apps[i].id == id) {
            g_apps[i].eval = *ev;
            return;
        }
        if (!g_apps[i].used && free_slot < 0)
            free_slot = (int)i;
    }
    if (free_slot < 0)
        free_slot = (int)(id % SAR_ATTRIB_MAX);
    g_apps[free_slot].used = 1;
    g_apps[free_slot].id = id;
    g_apps[free_slot].eval = *ev;
}

static void sar_attrib_upsert_path(const uint16_t *nt, uint64_t lw, uint64_t sz, uint64_t id)
{
    uint32_t i;
    int free_slot = -1;
    for (i = 0; i < SAR_ATTRIB_MAX; i++) {
        if (g_paths[i].used &&
            memcmp(g_paths[i].nt_path, nt, SEMANTICS_AR_PROTO_PATH_MAX * sizeof(uint16_t)) == 0) {
            g_paths[i].last_write = lw;
            g_paths[i].size = sz;
            g_paths[i].id = id;
            return;
        }
        if (!g_paths[i].used && free_slot < 0)
            free_slot = (int)i;
    }
    if (free_slot < 0)
        free_slot = (int)(sar_attrib_fnv(nt) % SAR_ATTRIB_MAX);
    g_paths[free_slot].used = 1;
    memcpy(g_paths[free_slot].nt_path, nt, SEMANTICS_AR_PROTO_PATH_MAX * sizeof(uint16_t));
    g_paths[free_slot].last_write = lw;
    g_paths[free_slot].size = sz;
    g_paths[free_slot].id = id;
}

static int sar_attrib_app_exists(uint64_t id)
{
    uint32_t i;
    for (i = 0; i < SAR_ATTRIB_MAX; i++)
        if (g_apps[i].used && g_apps[i].id == id)
            return 1;
    return 0;
}

uint64_t sar_attrib_resolve(const uint16_t *nt_image_path)
{
    wchar_t win32[SEMANTICS_AR_PROTO_PATH_MAX + 32];
    WIN32_FILE_ATTRIBUTE_DATA fad;
    uint64_t lw = 0, sz = 0;
    int have_stat = 0;
    uint64_t id = 0;
    uint64_t hash_id;
    uint32_t i;
    sar_identity_eval_t ev;

    if (!g_ready || nt_image_path == NULL || nt_image_path[0] == 0)
        return 0;

    sar_attrib_nt_to_win32((const wchar_t *)nt_image_path, win32, SEMANTICS_AR_PROTO_PATH_MAX + 32);

    if (GetFileAttributesExW(win32, GetFileExInfoStandard, &fad)) {
        lw = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
        sz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        have_stat = 1;
    }

    EnterCriticalSection(&g_lock);
    for (i = 0; i < SAR_ATTRIB_MAX; i++) {
        if (g_paths[i].used &&
            memcmp(g_paths[i].nt_path, nt_image_path,
                   SEMANTICS_AR_PROTO_PATH_MAX * sizeof(uint16_t)) == 0) {
            if (have_stat && g_paths[i].last_write == lw && g_paths[i].size == sz &&
                sar_attrib_app_exists(g_paths[i].id)) {
                id = g_paths[i].id;
                LeaveCriticalSection(&g_lock);
                return id;
            }
            break;
        }
    }
    LeaveCriticalSection(&g_lock);

    memset(&ev, 0, sizeof(ev));
    sar_identity_evaluate(win32, &ev);
    hash_id = sar_attrib_hash_id(ev.identity.content_hash);
    id = hash_id != 0 ? hash_id : (sar_attrib_fnv(nt_image_path) | 1ull);

    EnterCriticalSection(&g_lock);
    sar_attrib_upsert_app(id, &ev);
    if (hash_id != 0)
        sar_attrib_upsert_path(nt_image_path, lw, sz, id);
    LeaveCriticalSection(&g_lock);
    return id;
}

uint32_t sar_attrib_count(void)
{
    uint32_t i, n = 0;
    if (!g_ready)
        return 0;
    EnterCriticalSection(&g_lock);
    for (i = 0; i < SAR_ATTRIB_MAX; i++)
        if (g_apps[i].used)
            n++;
    LeaveCriticalSection(&g_lock);
    return n;
}

int sar_attrib_enumerate(uint32_t index, sar_app_identity_entry_t *out)
{
    uint32_t i, seen = 0;
    int found = 0;
    if (!g_ready || out == NULL)
        return 0;
    memset(out, 0, sizeof(*out));
    EnterCriticalSection(&g_lock);
    for (i = 0; i < SAR_ATTRIB_MAX; i++) {
        if (!g_apps[i].used)
            continue;
        if (seen == index) {
            out->app_identity_id = g_apps[i].id;
            memcpy(out->image_path, g_apps[i].eval.identity.image_path, sizeof(out->image_path));
            memcpy(out->cert_subject, g_apps[i].eval.identity.cert_subject, sizeof(out->cert_subject));
            memcpy(out->content_hash, g_apps[i].eval.identity.content_hash, sizeof(out->content_hash));
            out->verdict = (uint32_t)g_apps[i].eval.verdict;
            found = 1;
            break;
        }
        seen++;
    }
    LeaveCriticalSection(&g_lock);
    return found;
}
