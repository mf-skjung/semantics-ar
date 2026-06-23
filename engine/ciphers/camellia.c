#include "camellia.h"
#include "cipher_common.h"
#include <string.h>

static const uint8_t SBOX1[256] = {
    112,130, 44,236,179, 39,192,229,228,133, 87, 53,234, 12,174, 65,
     35,239,107,147, 69, 25,165, 33,237, 14, 79, 78, 29,101,146,189,
    134,184,175,143,124,235, 31,206, 62, 48,220, 95, 94,197, 11, 26,
    166,225, 57,202,213, 71, 93, 61,217,  1, 90,214, 81, 86,108, 77,
    139, 13,154,102,251,204,176, 45,116, 18, 43, 32,240,177,132,153,
    223, 76,203,194, 52,126,118,  5,109,183,169, 49,209, 23,  4,215,
     20, 88, 58, 97,222, 27, 17, 28, 50, 15,156, 22, 83, 24,242, 34,
    254, 68,207,178,195,181,122,145, 36,  8,232,168, 96,252,105, 80,
    170,208,160,125,161,137, 98,151, 84, 91, 30,149,224,255,100,210,
     16,196,  0, 72,163,247,117,219,138,  3,230,218,  9, 63,221,148,
    135, 92,131,  2,205, 74,144, 51,115,103,246,243,157,127,191,226,
     82,155,216, 38,200, 55,198, 59,129,150,111, 75, 19,190, 99, 46,
    233,121,167,140,159,110,188,142, 41,245,249,182, 47,253,180, 89,
    120,152,  6,106,231, 70,113,186,212, 37,171, 66,136,162,141,250,
    114,  7,185, 85,248,238,172, 10, 54, 73, 42,104, 60, 56,241,164,
     64, 40,211,123,187,201, 67,193, 21,227,173,244,119,199,128,158
};

static uint8_t SBOX2[256];
static uint8_t SBOX3[256];
static uint8_t SBOX4[256];
static int sbox_ready = 0;

static void init_sboxes(void) {
    if (sbox_ready) return;
    for (int i = 0; i < 256; i++) {
        uint8_t x = SBOX1[i];
        SBOX2[i] = (uint8_t)((x << 1) | (x >> 7));
        SBOX3[i] = (uint8_t)((x >> 1) | (x << 7));
        SBOX4[i] = SBOX1[(uint8_t)((i << 1) | (i >> 7))];
    }
    sbox_ready = 1;
}

static const uint64_t SIGMA[6] = {
    0xA09E667F3BCC908BULL,
    0xB67AE8584CAA73B2ULL,
    0xC6EF372FE94F82BEULL,
    0x54FF53A5F1D36F1CULL,
    0x10E527FADE682D1DULL,
    0xB05688C2B3E6C1FDULL
};

static uint64_t camellia_f(uint64_t x, uint64_t k) {
    uint64_t y = x ^ k;
    uint8_t t[8];
    sar_store64be(t, y);
    uint8_t y1 = SBOX1[t[0]];
    uint8_t y2 = SBOX2[t[1]];
    uint8_t y3 = SBOX3[t[2]];
    uint8_t y4 = SBOX4[t[3]];
    uint8_t y5 = SBOX2[t[4]];
    uint8_t y6 = SBOX3[t[5]];
    uint8_t y7 = SBOX4[t[6]];
    uint8_t y8 = SBOX1[t[7]];

    uint8_t z1 = (uint8_t)(y1 ^ y3 ^ y4 ^ y6 ^ y7 ^ y8);
    uint8_t z2 = (uint8_t)(y1 ^ y2 ^ y4 ^ y5 ^ y7 ^ y8);
    uint8_t z3 = (uint8_t)(y1 ^ y2 ^ y3 ^ y5 ^ y6 ^ y8);
    uint8_t z4 = (uint8_t)(y2 ^ y3 ^ y4 ^ y5 ^ y6 ^ y7);
    uint8_t z5 = (uint8_t)(y1 ^ y2 ^ y6 ^ y7 ^ y8);
    uint8_t z6 = (uint8_t)(y2 ^ y3 ^ y5 ^ y7 ^ y8);
    uint8_t z7 = (uint8_t)(y3 ^ y4 ^ y5 ^ y6 ^ y8);
    uint8_t z8 = (uint8_t)(y1 ^ y4 ^ y5 ^ y6 ^ y7);

    uint8_t out[8] = { z1, z2, z3, z4, z5, z6, z7, z8 };
    return sar_load64be(out);
}

static uint64_t camellia_fl(uint64_t x, uint64_t k) {
    uint32_t xl = (uint32_t)(x >> 32);
    uint32_t xr = (uint32_t)(x & 0xFFFFFFFFULL);
    uint32_t kl = (uint32_t)(k >> 32);
    uint32_t kr = (uint32_t)(k & 0xFFFFFFFFULL);

    uint32_t yr = xr ^ SAR_ROTL32(xl & kl, 1);
    uint32_t yl = xl ^ (yr | kr);

    return ((uint64_t)yl << 32) | yr;
}

static uint64_t camellia_flinv(uint64_t y, uint64_t k) {
    uint32_t yl = (uint32_t)(y >> 32);
    uint32_t yr = (uint32_t)(y & 0xFFFFFFFFULL);
    uint32_t kl = (uint32_t)(k >> 32);
    uint32_t kr = (uint32_t)(k & 0xFFFFFFFFULL);

    uint32_t xl = yl ^ (yr | kr);
    uint32_t xr = yr ^ SAR_ROTL32(xl & kl, 1);

    return ((uint64_t)xl << 32) | xr;
}

static void rotl128(uint64_t hi, uint64_t lo, int n, uint64_t *rhi, uint64_t *rlo) {
    n &= 127;
    if (n == 0) {
        *rhi = hi;
        *rlo = lo;
        return;
    }
    if (n < 64) {
        *rhi = (hi << n) | (lo >> (64 - n));
        *rlo = (lo << n) | (hi >> (64 - n));
    } else {
        n -= 64;
        if (n == 0) {
            *rhi = lo;
            *rlo = hi;
        } else {
            *rhi = (lo << n) | (hi >> (64 - n));
            *rlo = (hi << n) | (lo >> (64 - n));
        }
    }
}

void camellia_key_schedule(camellia_ctx_t *ctx, const uint8_t *key, uint32_t key_len) {
    init_sboxes();
    memset(ctx, 0, sizeof(*ctx));

    uint64_t kl_hi, kl_lo, kr_hi, kr_lo;
    kl_hi = sar_load64be(key);
    kl_lo = sar_load64be(key + 8);

    if (key_len == 16) {
        kr_hi = 0;
        kr_lo = 0;
        ctx->rounds = 18;
    } else if (key_len == 24) {
        kr_hi = sar_load64be(key + 16);
        kr_lo = ~kr_hi;
        ctx->rounds = 24;
    } else {
        kr_hi = sar_load64be(key + 16);
        kr_lo = sar_load64be(key + 24);
        ctx->rounds = 24;
    }

    uint64_t d1 = kl_hi ^ kr_hi;
    uint64_t d2 = kl_lo ^ kr_lo;
    d2 ^= camellia_f(d1, SIGMA[0]);
    d1 ^= camellia_f(d2, SIGMA[1]);
    d1 ^= kl_hi;
    d2 ^= kl_lo;
    d2 ^= camellia_f(d1, SIGMA[2]);
    d1 ^= camellia_f(d2, SIGMA[3]);

    uint64_t ka_hi = d1, ka_lo = d2;

    d1 = ka_hi ^ kr_hi;
    d2 = ka_lo ^ kr_lo;
    d2 ^= camellia_f(d1, SIGMA[4]);
    d1 ^= camellia_f(d2, SIGMA[5]);

    uint64_t kb_hi = d1, kb_lo = d2;

    uint64_t hi, lo;

    if (ctx->rounds == 18) {
        ctx->kw[0] = kl_hi;
        ctx->kw[1] = kl_lo;

        ctx->k[0] = ka_hi;
        ctx->k[1] = ka_lo;

        rotl128(kl_hi, kl_lo, 15, &hi, &lo);
        ctx->k[2] = hi; ctx->k[3] = lo;
        rotl128(ka_hi, ka_lo, 15, &hi, &lo);
        ctx->k[4] = hi; ctx->k[5] = lo;

        rotl128(ka_hi, ka_lo, 30, &hi, &lo);
        ctx->ke[0] = hi; ctx->ke[1] = lo;

        rotl128(kl_hi, kl_lo, 45, &hi, &lo);
        ctx->k[6] = hi; ctx->k[7] = lo;
        rotl128(ka_hi, ka_lo, 45, &hi, &lo);
        ctx->k[8] = hi;
        rotl128(kl_hi, kl_lo, 60, &hi, &lo);
        ctx->k[9] = lo;
        rotl128(ka_hi, ka_lo, 60, &hi, &lo);
        ctx->k[10] = hi; ctx->k[11] = lo;

        rotl128(kl_hi, kl_lo, 77, &hi, &lo);
        ctx->ke[2] = hi; ctx->ke[3] = lo;

        rotl128(kl_hi, kl_lo, 94, &hi, &lo);
        ctx->k[12] = hi; ctx->k[13] = lo;
        rotl128(ka_hi, ka_lo, 94, &hi, &lo);
        ctx->k[14] = hi; ctx->k[15] = lo;
        rotl128(kl_hi, kl_lo, 111, &hi, &lo);
        ctx->k[16] = hi; ctx->k[17] = lo;

        rotl128(ka_hi, ka_lo, 111, &hi, &lo);
        ctx->kw[2] = hi; ctx->kw[3] = lo;
    } else {
        ctx->kw[0] = kl_hi;
        ctx->kw[1] = kl_lo;

        ctx->k[0] = kb_hi;
        ctx->k[1] = kb_lo;

        rotl128(kr_hi, kr_lo, 15, &hi, &lo);
        ctx->k[2] = hi; ctx->k[3] = lo;
        rotl128(ka_hi, ka_lo, 15, &hi, &lo);
        ctx->k[4] = hi; ctx->k[5] = lo;

        rotl128(kr_hi, kr_lo, 30, &hi, &lo);
        ctx->ke[0] = hi; ctx->ke[1] = lo;

        rotl128(kb_hi, kb_lo, 30, &hi, &lo);
        ctx->k[6] = hi; ctx->k[7] = lo;
        rotl128(kl_hi, kl_lo, 45, &hi, &lo);
        ctx->k[8] = hi; ctx->k[9] = lo;
        rotl128(ka_hi, ka_lo, 45, &hi, &lo);
        ctx->k[10] = hi; ctx->k[11] = lo;

        rotl128(kl_hi, kl_lo, 60, &hi, &lo);
        ctx->ke[2] = hi; ctx->ke[3] = lo;

        rotl128(kr_hi, kr_lo, 60, &hi, &lo);
        ctx->k[12] = hi; ctx->k[13] = lo;
        rotl128(kb_hi, kb_lo, 60, &hi, &lo);
        ctx->k[14] = hi; ctx->k[15] = lo;
        rotl128(kl_hi, kl_lo, 77, &hi, &lo);
        ctx->k[16] = hi; ctx->k[17] = lo;

        rotl128(ka_hi, ka_lo, 77, &hi, &lo);
        ctx->ke[4] = hi; ctx->ke[5] = lo;

        rotl128(kr_hi, kr_lo, 94, &hi, &lo);
        ctx->k[18] = hi; ctx->k[19] = lo;
        rotl128(ka_hi, ka_lo, 94, &hi, &lo);
        ctx->k[20] = hi; ctx->k[21] = lo;
        rotl128(kl_hi, kl_lo, 111, &hi, &lo);
        ctx->k[22] = hi; ctx->k[23] = lo;

        rotl128(kb_hi, kb_lo, 111, &hi, &lo);
        ctx->kw[2] = hi; ctx->kw[3] = lo;
    }
}

static void camellia_crypt(const camellia_ctx_t *ctx, const uint8_t *in, uint8_t *out, int decrypt) {
    uint64_t d1 = sar_load64be(in);
    uint64_t d2 = sar_load64be(in + 8);

    int nk = (ctx->rounds == 18) ? 18 : 24;

    uint64_t kw_pre0, kw_pre1, kw_post0, kw_post1;
    uint64_t rk[24];
    uint64_t fk[6];

    if (!decrypt) {
        kw_pre0 = ctx->kw[0];
        kw_pre1 = ctx->kw[1];
        kw_post0 = ctx->kw[2];
        kw_post1 = ctx->kw[3];
        for (int i = 0; i < nk; i++) rk[i] = ctx->k[i];
        for (int i = 0; i < 6; i++) fk[i] = ctx->ke[i];
    } else {
        kw_pre0 = ctx->kw[2];
        kw_pre1 = ctx->kw[3];
        kw_post0 = ctx->kw[0];
        kw_post1 = ctx->kw[1];
        for (int i = 0; i < nk; i++) rk[i] = ctx->k[nk - 1 - i];
        if (ctx->rounds == 18) {
            fk[0] = ctx->ke[3];
            fk[1] = ctx->ke[2];
            fk[2] = ctx->ke[1];
            fk[3] = ctx->ke[0];
        } else {
            fk[0] = ctx->ke[5];
            fk[1] = ctx->ke[4];
            fk[2] = ctx->ke[3];
            fk[3] = ctx->ke[2];
            fk[4] = ctx->ke[1];
            fk[5] = ctx->ke[0];
        }
    }

    d1 ^= kw_pre0;
    d2 ^= kw_pre1;

    int nfl = (ctx->rounds == 18) ? 2 : 3;

    for (int g = 0; g < nfl + 1; g++) {
        int base = g * 6;
        for (int r = 0; r < 6; r++) {
            if (r % 2 == 0) {
                d2 ^= camellia_f(d1, rk[base + r]);
            } else {
                d1 ^= camellia_f(d2, rk[base + r]);
            }
        }
        if (g < nfl) {
            d1 = camellia_fl(d1, fk[g * 2]);
            d2 = camellia_flinv(d2, fk[g * 2 + 1]);
        }
    }

    d2 ^= kw_post0;
    d1 ^= kw_post1;

    sar_store64be(out, d2);
    sar_store64be(out + 8, d1);
}

int camellia_encrypt_block(void *ctx, const uint8_t *in, uint8_t *out) {
    camellia_crypt((const camellia_ctx_t *)ctx, in, out, 0);
    return 1;
}

int camellia_decrypt_block(void *ctx, const uint8_t *in, uint8_t *out) {
    camellia_crypt((const camellia_ctx_t *)ctx, in, out, 1);
    return 1;
}
