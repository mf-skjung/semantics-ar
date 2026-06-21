#ifndef SEMANTICS_AR_CIPHER_HC128_H
#define SEMANTICS_AR_CIPHER_HC128_H

#include <stdint.h>

#define HC128_MAX_ADVANCE_BYTES ((uint64_t)2u * 1024u * 1024u * 1024u)

typedef struct {
    uint32_t P[512];
    uint32_t Q[512];
    uint32_t ctr;
} hc128_ctx_t;

void hc128_init(hc128_ctx_t *ctx, const uint8_t *key, const uint8_t *iv);
uint32_t hc128_step(hc128_ctx_t *ctx);
int hc128_verify(const uint8_t *key, const uint8_t *iv,
                 const uint8_t *pt, const uint8_t *ct,
                 uint32_t sample_size, uint64_t file_offset);

#endif