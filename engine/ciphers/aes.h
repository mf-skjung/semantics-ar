#ifndef SEMANTICS_AR_CIPHER_AES_H
#define SEMANTICS_AR_CIPHER_AES_H

#include <stdint.h>

typedef struct {
    uint8_t rk[240];
    int     nr;
} aes_ctx_t;

void aes_setkey(aes_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
int  aes_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out);
int  aes_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out);

int aes_master_from_window(uint32_t key_len, const uint8_t *window,
                           int base_block, uint8_t *master_out);

int aes_schedule_is_valid(const uint8_t *schedule, uint32_t key_len);

void aes_word_byteswap(uint8_t *dst, const uint8_t *src, uint32_t len);

#endif
