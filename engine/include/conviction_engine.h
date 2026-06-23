#ifndef SEMANTICS_AR_CONVICTION_ENGINE_H
#define SEMANTICS_AR_CONVICTION_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include "semantics_ar/keystore_format.h"

#define SAR_CANDIDATE_SIZE   16
#define SAR_ENGINE_KEY_MAX   SEMANTICS_AR_MAX_KEY_BYTES

typedef int (*block_cipher_fn)(void *ctx, const uint8_t *in, uint8_t *out);

typedef struct {
    block_cipher_fn encrypt;
    block_cipher_fn decrypt;
    void           *ctx;
    uint32_t        block_size;
} block_cipher_ctx_t;

typedef struct {
    int      convicted;
    uint32_t algorithm;
    uint32_t mode;
    uint8_t  key[SAR_ENGINE_KEY_MAX];
    uint32_t key_length;
    uint64_t mode_params;
    uint8_t  iv[SEMANTICS_AR_IV_MAX];
    uint8_t  iv_length;
    uint8_t  ctr_layout_tag;
} sar_verdict_t;

typedef struct {
    const uint8_t (*candidates)[SAR_CANDIDATE_SIZE];
    size_t          candidate_count;
    const uint8_t  *scan_buffer;
    size_t          scan_length;
    const uint8_t  *plaintext;
    const uint8_t  *ciphertext;
    uint32_t        sample_size;
    uint64_t        file_offset;
} sar_engine_input_t;

int sar_convict(const sar_engine_input_t *input, sar_verdict_t *out);

#endif
