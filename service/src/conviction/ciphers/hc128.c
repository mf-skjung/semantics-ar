#include "hc128.h"
#include "cipher_common.h"
#include <string.h>

static uint32_t f1(uint32_t x) { return SAR_ROTR32(x,7)^SAR_ROTR32(x,18)^(x>>3); }
static uint32_t f2(uint32_t x) { return SAR_ROTR32(x,17)^SAR_ROTR32(x,19)^(x>>10); }

void hc128_init(hc128_ctx_t *ctx, const uint8_t *key, const uint8_t *iv) {
    uint32_t W[1280];
    int i;
    for (i = 0; i < 4; i++) W[i] = sar_le32(key + i*4);
    for (i = 4; i < 8; i++) W[i] = W[i-4];
    for (i = 0; i < 4; i++) W[i+8] = sar_le32(iv + i*4);
    for (i = 12; i < 16; i++) W[i] = W[i-4];
    for (i = 16; i < 1280; i++)
        W[i] = f2(W[i-2]) + W[i-7] + f1(W[i-15]) + W[i-16] + (uint32_t)i;
    memcpy(ctx->P, W+256, 512*sizeof(uint32_t));
    memcpy(ctx->Q, W+768, 512*sizeof(uint32_t));
    ctx->ctr = 0;
    for (i = 0; i < 1024; i++) hc128_step(ctx);
}

uint32_t hc128_step(hc128_ctx_t *ctx) {
    uint32_t j = ctx->ctr & 0x1FF;
    uint32_t ret;
    if (ctx->ctr < 512) {
        uint32_t t3  = ctx->P[(j+509)&0x1FF];
        uint32_t t10 = ctx->P[(j+502)&0x1FF];
        uint32_t tp1 = ctx->P[(j+1)&0x1FF];
        uint32_t t12 = ctx->P[(j+500)&0x1FF];
        ctx->P[j] += t10 + (SAR_ROTR32(t3,10) ^ SAR_ROTR32(tp1,23)) + SAR_ROTR32(t10,8);
        ret = (ctx->Q[(uint8_t)t12] + ctx->Q[256+((t12>>16)&0xFF)]) ^ ctx->P[j];
    } else {
        uint32_t t3  = ctx->Q[(j+509)&0x1FF];
        uint32_t t10 = ctx->Q[(j+502)&0x1FF];
        uint32_t tp1 = ctx->Q[(j+1)&0x1FF];
        uint32_t t12 = ctx->Q[(j+500)&0x1FF];
        ctx->Q[j] += t10 + (SAR_ROTR32(t3,10) ^ SAR_ROTR32(tp1,23)) + SAR_ROTR32(t10,8);
        ret = (ctx->P[(uint8_t)t12] + ctx->P[256+((t12>>16)&0xFF)]) ^ ctx->Q[j];
    }
    ctx->ctr = (ctx->ctr + 1) & 0x3FF;
    return ret;
}

int hc128_verify(const uint8_t *key, const uint8_t *iv,
                 const uint8_t *pt, const uint8_t *ct,
                 uint32_t sample_size, uint64_t file_offset) {
    if (file_offset > HC128_MAX_ADVANCE_BYTES) return 0;
    if (sample_size < 16) return 0;
    hc128_ctx_t ctx;
    hc128_init(&ctx, key, iv);
    uint64_t skip_words = file_offset / 4;
    uint32_t byte_off = (uint32_t)(file_offset % 4);
    for (uint64_t s = 0; s < skip_words; s++) hc128_step(&ctx);
    uint32_t word = 0;
    uint32_t wpos = 4;
    if (byte_off > 0) { word = hc128_step(&ctx); wpos = byte_off; }
    for (uint32_t i = 0; i < sample_size; i++) {
        if (wpos >= 4) { word = hc128_step(&ctx); wpos = 0; }
        uint8_t ks = (uint8_t)(word >> (wpos*8));
        if ((pt[i] ^ ks) != ct[i]) return 0;
        wpos++;
    }
    return 1;
}