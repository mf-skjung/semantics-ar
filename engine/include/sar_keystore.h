#ifndef SEMANTICS_AR_SAR_KEYSTORE_H
#define SEMANTICS_AR_SAR_KEYSTORE_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/keystore_format.h"
#include "conviction_engine.h"

typedef enum {
    SAR_KS_OK = 0,
    SAR_KS_INVALID_ARG = -1,
    SAR_KS_FULL = -2,
    SAR_KS_BUFFER_TOO_SMALL = -3,
    SAR_KS_BAD_MAGIC = -4,
    SAR_KS_BAD_VERSION = -5,
    SAR_KS_TRUNCATED = -6,
    SAR_KS_RECORD_MAC = -7,
    SAR_KS_COUNT_MISMATCH = -8,
    SAR_KS_ROLLBACK = -9
} sar_ks_status_t;

typedef struct {
    int      present;
    uint64_t generation;
    uint8_t  head_mac[SEMANTICS_AR_MAC_SIZE];
} sar_keystore_anchor_t;

void sar_keystore_key_id(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                         uint32_t algorithm, uint32_t mode,
                         uint32_t key_length, const uint8_t *key_bytes,
                         uint8_t out_id[SEMANTICS_AR_KEY_ID_SIZE]);

void sar_sample_tag(const uint8_t *data, uint32_t len,
                    uint8_t out[SEMANTICS_AR_MAC_SIZE]);

void sar_keystore_record_init(semantics_ar_keystore_record_t *rec,
                              const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                              const sar_verdict_t *verdict,
                              const uint16_t *provenance_path,
                              uint64_t provenance_offset,
                              uint64_t provenance_length,
                              const uint8_t *sample,
                              uint32_t sample_len,
                              uint64_t sample_offset);

int sar_keystore_append(semantics_ar_keystore_record_t *records,
                        uint64_t *count, uint64_t capacity,
                        const semantics_ar_keystore_record_t *rec);

void sar_keystore_mac_record(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                             const semantics_ar_keystore_record_t *fields,
                             const uint8_t prev_mac[SEMANTICS_AR_MAC_SIZE],
                             uint8_t out_mac[SEMANTICS_AR_MAC_SIZE]);

int sar_keystore_serialize(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                           const semantics_ar_keystore_record_t *records,
                           uint64_t count, uint64_t generation,
                           uint8_t *out_buf, size_t out_cap, size_t *out_len);

int sar_keystore_verify(const uint8_t *buf, size_t len,
                        const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                        const sar_keystore_anchor_t *anchor,
                        uint64_t *out_count);

size_t sar_keystore_serialized_size(uint64_t count);

#endif
