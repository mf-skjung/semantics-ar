#include "sar_control.h"
#include "ctl_mem.h"

static int identity_equal(const sar_identity_t *a, const sar_identity_t *b) {
    return ctl_memcmp(a->content_hash, b->content_hash, sizeof(a->content_hash)) == 0 &&
           ctl_memcmp(a->cert_subject, b->cert_subject, sizeof(a->cert_subject)) == 0;
}

void sar_whitelist_init(sar_whitelist_t *wl, sar_identity_t *storage,
                        uint32_t capacity) {
    wl->entries = storage;
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

sar_wl_status_t sar_whitelist_add(sar_whitelist_t *wl, const sar_identity_t *id) {
    if (wl == NULL || id == NULL || wl->entries == NULL)
        return SAR_WL_INVALID;
    for (uint32_t i = 0; i < wl->count; i++) {
        if (identity_equal(&wl->entries[i], id))
            return SAR_WL_DUPLICATE;
    }
    if (wl->count >= wl->capacity)
        return SAR_WL_FULL;
    ctl_memcpy(&wl->entries[wl->count], id, sizeof(*id));
    wl->count++;
    return SAR_WL_OK;
}

sar_wl_status_t sar_whitelist_remove(sar_whitelist_t *wl, const sar_identity_t *id) {
    if (wl == NULL || id == NULL || wl->entries == NULL)
        return SAR_WL_INVALID;
    for (uint32_t i = 0; i < wl->count; i++) {
        if (identity_equal(&wl->entries[i], id)) {
            for (uint32_t j = i + 1u; j < wl->count; j++)
                ctl_memcpy(&wl->entries[j - 1u], &wl->entries[j], sizeof(*id));
            wl->count--;
            return SAR_WL_OK;
        }
    }
    return SAR_WL_NOT_FOUND;
}

sar_id_state_t sar_identity_resolve(int verified, int whitelist_hit) {
    if (!verified)
        return SAR_IDSTATE_OBSERVE_PENDING;
    if (whitelist_hit)
        return SAR_IDSTATE_EXEMPT;
    return SAR_IDSTATE_OBSERVE;
}
