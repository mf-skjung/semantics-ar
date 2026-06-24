#ifndef SEMANTICS_AR_SAR_KEYSTORE_MGR_H
#define SEMANTICS_AR_SAR_KEYSTORE_MGR_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/keystore_format.h"
#include "sar_keystore.h"

typedef enum {
    SAR_KSM_OK = 0,
    SAR_KSM_EMPTY = 1,
    SAR_KSM_INVALID_ARG = -1,
    SAR_KSM_CORRUPT = -2,
    SAR_KSM_ROLLBACK = -3,
    SAR_KSM_OVERFLOW = -4,
    SAR_KSM_BUFFER_TOO_SMALL = -5
} sar_ksm_status_t;

typedef struct {
    uint64_t generation;
    int      anchor_advanced;
} sar_ksm_load_result_t;

sar_ksm_status_t sar_ksm_load(const uint8_t *buf, size_t len,
                              const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                              const sar_keystore_anchor_t *anchor,
                              semantics_ar_keystore_record_t *records_out,
                              uint64_t records_capacity,
                              uint64_t *count_out,
                              sar_keystore_anchor_t *anchor_out,
                              sar_ksm_load_result_t *result);

sar_ksm_status_t sar_ksm_persist(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                                 const semantics_ar_keystore_record_t *records,
                                 uint64_t count, uint64_t prev_generation,
                                 uint8_t *out_buf, size_t out_cap, size_t *out_len,
                                 sar_keystore_anchor_t *anchor_out);

#endif
