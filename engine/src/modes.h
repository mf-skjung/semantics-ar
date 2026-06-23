#ifndef SEMANTICS_AR_ENGINE_MODES_H
#define SEMANTICS_AR_ENGINE_MODES_H

#include <stdint.h>
#include "conviction_engine.h"

typedef struct {
    uint8_t bytes[SEMANTICS_AR_IV_MAX];
    uint8_t length;
    uint8_t tag;
} sar_iv_out_t;

void gf128_mul_alpha(uint8_t *tweak);

int engine_try_block_modes(const block_cipher_ctx_t *bc,
                           const uint8_t *pt, const uint8_t *ct,
                           uint32_t len, uint64_t file_offset,
                           uint32_t *out_mode, sar_iv_out_t *out_iv);

int engine_verify_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint64_t file_offset,
                      uint64_t *out_data_unit);

#endif
