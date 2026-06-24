#include "sar_control.h"

void sar_mode_init(sar_mode_state_t *st) {
    st->mode = SEMANTICS_AR_MODE_AUDIT;
}

int sar_mode_set(sar_mode_state_t *st, uint32_t requested) {
    if (st == NULL)
        return 0;
    if (requested != SEMANTICS_AR_MODE_AUDIT && requested != SEMANTICS_AR_MODE_ENFORCE)
        return 0;
    st->mode = requested;
    return 1;
}

uint32_t sar_mode_get(const sar_mode_state_t *st) {
    return st->mode;
}
