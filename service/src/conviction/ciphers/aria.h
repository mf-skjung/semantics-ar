#ifndef SEMANTICS_AR_CIPHER_ARIA_H
#define SEMANTICS_AR_CIPHER_ARIA_H

#include <stdint.h>

typedef struct {
    uint32_t ek[68];
    uint32_t dk[68];
    int rounds;
} aria_ctx_t;

void aria_key_schedule(aria_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
int  aria_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  aria_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

#endif