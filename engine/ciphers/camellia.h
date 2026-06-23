#ifndef SEMANTICS_AR_CIPHER_CAMELLIA_H
#define SEMANTICS_AR_CIPHER_CAMELLIA_H

#include <stdint.h>

typedef struct {
    uint64_t kw[4];
    uint64_t k[24];
    uint64_t ke[6];
    int rounds;
} camellia_ctx_t;

void camellia_key_schedule(camellia_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
int  camellia_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  camellia_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

#endif