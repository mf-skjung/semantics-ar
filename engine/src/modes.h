#ifndef SEMANTICS_AR_ENGINE_MODES_H
#define SEMANTICS_AR_ENGINE_MODES_H

#include <stdint.h>
#include "conviction_engine.h"

void gf128_mul_alpha(uint8_t *tweak);

int engine_try_block_modes(const block_cipher_ctx_t *bc,
                           const uint8_t *pt, const uint8_t *ct,
                           uint32_t len, uint32_t *out_mode);

int engine_verify_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint64_t file_offset,
                      uint64_t *out_data_unit);

#endif
