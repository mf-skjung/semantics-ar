#include "sar_control.h"
#include "ctl_mem.h"

static int identity_equal(const sar_identity_t *a, const sar_identity_t *b) {
    return ctl_memcmp(a->content_hash, b->content_hash, sizeof(a->content_hash)) == 0 &&
           ctl_memcmp(a->cert_subject, b->cert_subject, sizeof(a->cert_subject)) == 0;
}

void sar_whitelist_init(sar_whitelist_t *wl, sar_identity_t *storage,
                        uint64_t *first_seen, uint32_t capacity) {
    wl->entries = storage;
    wl->first_seen = first_seen;
    wl->count = 0u;
    wl->capacity = capacity;
}

int sar_whitelist_match(const sar_whitelist_t *wl, const sar_identity_t *id) {
    if (wl == NULL || id == NULL || wl->entries == NULL)
        return 0;
    for (uint32_t i = 0; i < wl->count; i++) {
        if (identity_equal(&wl->entries[i], id))
            return 1;
    }
    return 0;
}

sar_wl_status_t sar_whitelist_add(sar_whitelist_t *wl, const sar_identity_t *id,
                                  uint64_t first_seen) {
    if (wl == NULL || id == NULL || wl->entries == NULL)
        return SAR_WL_INVALID;
    if (sar_identity_is_interpreter(id->image_path))
        return SAR_WL_INTERPRETER;
    for (uint32_t i = 0; i < wl->count; i++) {
        if (identity_equal(&wl->entries[i], id))
            return SAR_WL_DUPLICATE;
    }
    if (wl->count >= wl->capacity)
        return SAR_WL_FULL;
    ctl_memcpy(&wl->entries[wl->count], id, sizeof(*id));
    if (wl->first_seen != NULL)
        wl->first_seen[wl->count] = first_seen;
    wl->count++;
    return SAR_WL_OK;
}

sar_wl_status_t sar_whitelist_remove(sar_whitelist_t *wl, const sar_identity_t *id) {
    if (wl == NULL || id == NULL || wl->entries == NULL)
        return SAR_WL_INVALID;
    for (uint32_t i = 0; i < wl->count; i++) {
        if (identity_equal(&wl->entries[i], id)) {
            for (uint32_t j = i + 1u; j < wl->count; j++) {
                ctl_memcpy(&wl->entries[j - 1u], &wl->entries[j], sizeof(*id));
                if (wl->first_seen != NULL)
                    wl->first_seen[j - 1u] = wl->first_seen[j];
            }
            wl->count--;
            return SAR_WL_OK;
        }
    }
    return SAR_WL_NOT_FOUND;
}

sar_wl_status_t sar_whitelist_enumerate(const sar_whitelist_t *wl, uint32_t index,
                                        sar_identity_t *out_id, uint64_t *out_first_seen) {
    if (wl == NULL || out_id == NULL || wl->entries == NULL)
        return SAR_WL_INVALID;
    if (index >= wl->count)
        return SAR_WL_NOT_FOUND;
    ctl_memcpy(out_id, &wl->entries[index], sizeof(*out_id));
    if (out_first_seen != NULL)
        *out_first_seen = wl->first_seen != NULL ? wl->first_seen[index] : 0u;
    return SAR_WL_OK;
}

int sar_identity_is_interpreter(const uint16_t *image_path) {
    static const char *const names[] = {
        "powershell.exe", "pwsh.exe", "cmd.exe", "wscript.exe",
        "cscript.exe", "mshta.exe", "python.exe", "node.exe",
    };
    uint32_t len = 0u;
    uint32_t leaf = 0u;
    uint32_t end;
    uint32_t i;
    uint32_t k;

    if (image_path == NULL)
        return 0;
    while (len < SEMANTICS_AR_PROTO_PATH_MAX && image_path[len] != 0u)
        len++;
    for (i = 0; i < len; i++) {
        if (image_path[i] == (uint16_t)'\\' || image_path[i] == (uint16_t)'/')
            leaf = i + 1u;
    }

    end = len;
    while (end > leaf &&
           (image_path[end - 1u] == (uint16_t)' ' || image_path[end - 1u] == (uint16_t)'.'))
        end--;

    for (k = 0; k < (uint32_t)(sizeof(names) / sizeof(names[0])); k++) {
        const char *n = names[k];
        uint32_t j = 0u;
        uint32_t p = leaf;
        int match = 1;

        while (n[j] != 0) {
            uint16_t a;
            uint16_t b;
            if (p >= end) { match = 0; break; }
            a = image_path[p];
            b = (uint16_t)(unsigned char)n[j];
            if (a >= (uint16_t)'A' && a <= (uint16_t)'Z') a = (uint16_t)(a + 32);
            if (b >= (uint16_t)'A' && b <= (uint16_t)'Z') b = (uint16_t)(b + 32);
            if (a != b) { match = 0; break; }
            p++;
            j++;
        }
        if (match && p == end)
            return 1;
    }
    return 0;
}

sar_id_state_t sar_identity_resolve(int verified, int whitelist_hit) {
    if (!verified)
        return SAR_IDSTATE_OBSERVE_PENDING;
    if (whitelist_hit)
        return SAR_IDSTATE_EXEMPT;
    return SAR_IDSTATE_OBSERVE;
}
