#ifndef SEMANTICS_AR_CIPHER_SM4_H
#define SEMANTICS_AR_CIPHER_SM4_H

#include <stdint.h>

typedef struct { uint32_t rk[32]; } sm4_ctx_t;

void sm4_key_schedule(sm4_ctx_t *ctx, const uint8_t *key);
int  sm4_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  sm4_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

#endif