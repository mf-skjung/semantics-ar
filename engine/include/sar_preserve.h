#ifndef SEMANTICS_AR_SAR_PRESERVE_H
#define SEMANTICS_AR_SAR_PRESERVE_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/preserve_format.h"
#include "sar_keystore.h"

typedef enum {
    SAR_PRES_OK = 0,
    SAR_PRES_INVALID_ARG = -1,
    SAR_PRES_FULL = -2,
    SAR_PRES_BUFFER_TOO_SMALL = -3,
    SAR_PRES_BAD_MAGIC = -4,
    SAR_PRES_TRUNCATED = -5,
    SAR_PRES_RECORD_MAC = -6,
    SAR_PRES_COUNT_MISMATCH = -7,
    SAR_PRES_ROLLBACK = -8,
    SAR_PRES_RESTORE_MISMATCH = -9
} sar_pres_status_t;

void sar_preserve_content_tag(const uint8_t *plaintext, uint64_t len,
                              uint8_t out[SEMANTICS_AR_MAC_SIZE]);

void sar_preserve_record_init(sar_preserve_record_t *rec,
                              const uint16_t *provenance_path,
                              uint64_t provenance_offset,
                              uint64_t provenance_length,
                              uint64_t capture_time,
                              uint64_t payload_offset,
                              uint64_t payload_length,
                              uint64_t actor_id,
                              const uint8_t *iv, uint8_t iv_length,
                              const uint8_t *plaintext, uint64_t plaintext_length);

int sar_preserve_covered(const sar_preserve_record_t *records, uint64_t count,
                         const uint16_t *provenance_path,
                         uint64_t offset, uint64_t length);

int sar_preserve_first_gap(const sar_preserve_record_t *records, uint64_t count,
                           const uint16_t *provenance_path,
                           uint64_t offset, uint64_t length,
                           uint64_t *gap_offset, uint64_t *gap_length);

int sar_preserve_append(sar_preserve_record_t *records,
                        uint64_t *count, uint64_t capacity,
                        const sar_preserve_record_t *rec);

uint64_t sar_preserve_promote(sar_preserve_record_t *records, uint64_t count,
                              uint64_t actor_id);

uint64_t sar_preserve_reconcile(sar_preserve_record_t *records, uint64_t *count,
                                const uint16_t *provenance_path,
                                uint64_t key_offset, uint64_t key_length);

uint64_t sar_preserve_total_bytes(const sar_preserve_record_t *records,
                                  uint64_t count);

uint64_t sar_preserve_protected_bytes(const sar_preserve_record_t *records,
                                      uint64_t count);

uint64_t sar_preserve_probation_bytes(const sar_preserve_record_t *records,
                                      uint64_t count);

uint64_t sar_preserve_evict_aged(sar_preserve_record_t *records, uint64_t *count,
                                 uint64_t now, uint64_t retention);

uint64_t sar_preserve_evict_probation_oldest(sar_preserve_record_t *records,
                                             uint64_t *count,
                                             uint64_t probation_cap_bytes);

int sar_preserve_would_exceed(const sar_preserve_record_t *records, uint64_t count,
                              uint64_t capacity_bytes, uint64_t incoming_bytes);

int sar_preserve_verify_extract(const sar_preserve_record_t *rec,
                                const uint16_t *target_path,
                                uint64_t target_offset, uint64_t target_length,
                                const uint8_t *decrypted_region, uint64_t region_length,
                                uint64_t *inner_offset);

size_t sar_preserve_serialized_size(uint64_t count);

int sar_preserve_serialize(const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                           const sar_preserve_record_t *records,
                           uint64_t count, uint64_t generation,
                           uint8_t *out_buf, size_t out_cap, size_t *out_len);

int sar_preserve_verify(const uint8_t *buf, size_t len,
                        const uint8_t mac_key[SEMANTICS_AR_MAC_SIZE],
                        const sar_keystore_anchor_t *anchor,
                        uint64_t *out_count);

#endif
