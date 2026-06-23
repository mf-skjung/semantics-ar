#ifndef SEMANTICS_AR_CIPHER_SEED_H
#define SEMANTICS_AR_CIPHER_SEED_H

#include <stdint.h>

typedef struct { uint32_t rk[32]; } seed_ctx_t;

void seed_key_schedule(seed_ctx_t *ctx, const uint8_t *key);
int  seed_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  seed_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

#endif