#ifndef SEMANTICS_AR_SHA256_H
#define SEMANTICS_AR_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SAR_SHA256_DIGEST 32
#define SAR_SHA256_BLOCK  64

typedef struct {
    uint32_t state[8];
    uint64_t total;
    uint8_t  buf[SAR_SHA256_BLOCK];
    size_t   buf_len;
} sar_sha256_ctx_t;

void sar_sha256_init(sar_sha256_ctx_t *ctx);
void sar_sha256_update(sar_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sar_sha256_final(sar_sha256_ctx_t *ctx, uint8_t out[SAR_SHA256_DIGEST]);

void sar_hmac_sha256(const uint8_t *key, size_t key_len,
                     const uint8_t *msg, size_t msg_len,
                     uint8_t out[SAR_SHA256_DIGEST]);

#endif
