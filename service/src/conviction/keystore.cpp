#include "service_internal.h"
#include <string.h>

static CRITICAL_SECTION g_keystore_lock;
static svc_confirmed_key_t g_keys[SEMANTICS_AR_MAX_CONFIRMED_KEYS];
static uint32_t g_key_count;
static BOOL g_keystore_ready;

void svc_keystore_init(void) {
    InitializeCriticalSection(&g_keystore_lock);
    g_key_count = 0;
    memset(g_keys, 0, sizeof(g_keys));
    g_keystore_ready = TRUE;
}

void svc_keystore_cleanup(void) {
    if (!g_keystore_ready)
        return;
    EnterCriticalSection(&g_keystore_lock);
    SecureZeroMemory(g_keys, sizeof(g_keys));
    g_key_count = 0;
    LeaveCriticalSection(&g_keystore_lock);
    DeleteCriticalSection(&g_keystore_lock);
    g_keystore_ready = FALSE;
}

void svc_keystore_store(const oracle_result_t *result) {
    if (!g_keystore_ready || !result->confirmed)
        return;

    EnterCriticalSection(&g_keystore_lock);

    for (uint32_t i = 0; i < g_key_count; i++) {
        if (g_keys[i].algorithm == result->algorithm &&
            g_keys[i].mode == result->mode &&
            g_keys[i].key_length == result->key_length &&
            memcmp(g_keys[i].key, result->key, result->key_length) == 0) {
            LeaveCriticalSection(&g_keystore_lock);
            return;
        }
    }

    uint32_t slot;
    if (g_key_count < SEMANTICS_AR_MAX_CONFIRMED_KEYS)
        slot = g_key_count++;
    else
        slot = SEMANTICS_AR_MAX_CONFIRMED_KEYS - 1;

    memset(&g_keys[slot], 0, sizeof(g_keys[slot]));
    memcpy(g_keys[slot].key, result->key, result->key_length);
    g_keys[slot].key_length = result->key_length;
    g_keys[slot].algorithm = result->algorithm;
    g_keys[slot].mode = result->mode;
    g_keys[slot].thread_id = result->thread_id;
    g_keys[slot].register_index = result->register_index;

    LeaveCriticalSection(&g_keystore_lock);
}

uint32_t svc_get_confirmed_keys(svc_confirmed_key_t *out_keys, uint32_t max_keys) {
    if (!g_keystore_ready || !out_keys || max_keys == 0)
        return 0;

    EnterCriticalSection(&g_keystore_lock);
    uint32_t n = g_key_count < max_keys ? g_key_count : max_keys;
    memcpy(out_keys, g_keys, n * sizeof(svc_confirmed_key_t));
    LeaveCriticalSection(&g_keystore_lock);
    return n;
}