#ifndef SEMANTICS_AR_CIPHER_DES_H
#define SEMANTICS_AR_CIPHER_DES_H

#include <stdint.h>

typedef struct {
    uint64_t ks[3][16];
} des3_ctx_t;

void des3_setkey(des3_ctx_t *ctx, const uint8_t *key24);
int  des3_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  des3_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

#endif
