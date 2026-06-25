#ifndef SEMANTICS_AR_SAR_RECOVER_H
#define SEMANTICS_AR_SAR_RECOVER_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/keystore_format.h"
#include "conviction_engine.h"

typedef enum {
    SAR_RECOVER_OK = 0,
    SAR_RECOVER_DECLINED_KEY = -1,
    SAR_RECOVER_DECLINED_IV = -2,
    SAR_RECOVER_DECLINED_GEOMETRY = -3,
    SAR_RECOVER_DECLINED_STATE_ONLY = -4,
    SAR_RECOVER_DECLINED_MISMATCH = -5,
    SAR_RECOVER_INVALID = -6,
    SAR_RECOVER_DECLINED_NO_KEY_ID = -7,
    SAR_RECOVER_DECLINED_TARGET_IO = -8,
    SAR_RECOVER_DECLINED_TOO_LARGE = -9
} sar_recover_status_t;

typedef struct {
    uint32_t algorithm;
    uint32_t mode;
    uint32_t key_length;
    uint8_t  key[SEMANTICS_AR_MAX_KEY_BYTES];
    uint64_t mode_params;
    uint8_t  iv[SEMANTICS_AR_IV_MAX];
    uint8_t  iv_length;
    uint8_t  ctr_layout_tag;
} sar_recovery_key_t;

typedef struct {
    uint64_t       file_offset;
    const uint8_t *plaintext;
    const uint8_t *ciphertext;
    uint32_t       length;
} sar_recovery_sample_t;

typedef struct {
    uint64_t sample_offset;
    uint32_t sample_length;
    uint8_t  sample_tag[SEMANTICS_AR_MAC_SIZE];
} sar_recovery_verify_t;

#define SAR_GEOM_FULL     0
#define SAR_GEOM_HEAD     1
#define SAR_GEOM_STRIDE   2
#define SAR_GEOM_PERCENT  3
#define SAR_GEOM_EXPLICIT 4

#define SAR_CTR_CONTINUOUS      0
#define SAR_CTR_RESET_PER_CHUNK 1

#define SAR_MAX_RANGES 1024

typedef struct {
    uint64_t offset;
    uint64_t length;
} sar_range_t;

typedef struct {
    uint32_t mode;
    uint64_t head_bytes;
    uint64_t chunk_bytes;
    uint64_t stride_bytes;
    uint32_t percent;
    uint32_t block_count;
    uint32_t counter_policy;
    const sar_range_t *ranges;
    uint32_t range_count;
} sar_geometry_t;

typedef int (*sar_geometry_mapper_fn)(const uint8_t *family_blob, uint32_t blob_len,
                                      uint64_t file_size, sar_geometry_t *out);

void sar_recovery_key_from_record(const semantics_ar_keystore_record_t *rec,
                                  sar_recovery_key_t *out);

void sar_recovery_key_from_verdict(const sar_verdict_t *v, sar_recovery_key_t *out);

void sar_recovery_verify_from_record(const semantics_ar_keystore_record_t *rec,
                                     sar_recovery_verify_t *out);

sar_recover_status_t sar_recover_verify(const uint8_t *pt, uint64_t file_size,
                                        const sar_recovery_verify_t *v);

int sar_geometry_expand(const sar_geometry_t *geom, uint64_t file_size,
                        sar_range_t *out, uint32_t cap, uint32_t *out_count);

int sar_geometry_from_family(sar_geometry_mapper_fn mapper,
                             const uint8_t *family_blob, uint32_t blob_len,
                             uint64_t file_size, sar_geometry_t *out);

int sar_recover_locate_iv(sar_recovery_key_t *rk,
                          const uint8_t *file, uint64_t file_size,
                          const sar_recovery_sample_t *sample);

sar_recover_status_t sar_recover_buffer(const sar_recovery_key_t *rk,
                                        const sar_geometry_t *geom,
                                        const uint8_t *ct, uint8_t *pt,
                                        uint64_t file_size);

#endif
