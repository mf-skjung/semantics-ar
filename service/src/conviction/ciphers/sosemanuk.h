#ifndef SEMANTICS_AR_CIPHER_SOSEMANUK_H
#define SEMANTICS_AR_CIPHER_SOSEMANUK_H

#include <stdint.h>

#define SOSEMANUK_FSM_CONST 0x54655307u

typedef struct {
    uint32_t sk[100];
} sosemanuk_key_ctx_t;

typedef struct {
    uint32_t s[10];
    uint32_t r1, r2;
} sosemanuk_ctx_t;

void sosemanuk_key_schedule(sosemanuk_key_ctx_t *kc, const uint8_t *key, uint32_t key_len);
void sosemanuk_iv_setup(sosemanuk_ctx_t *ctx, const sosemanuk_key_ctx_t *kc, const uint8_t *iv);
int sosemanuk_verify(const uint8_t *key, uint32_t key_len, const uint8_t *iv,
                     const uint8_t *pt, const uint8_t *ct,
                     uint32_t sample_size, uint64_t file_offset);

#endif