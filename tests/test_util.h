#ifndef SEMANTICS_AR_TEST_UTIL_H
#define SEMANTICS_AR_TEST_UTIL_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "conviction_engine.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }       \
    else { g_fail++; printf("  FAIL %s\n", (name)); }            \
} while (0)

static void fill_pattern(uint8_t *p, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + 7u * (uint8_t)i + ((uint8_t)i >> 1));
}

static void ref_ecb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                    uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    for (uint32_t s = 0; s + bs <= len; s += bs)
        bc->encrypt(bc->ctx, pt + s, ct + s);
}

static void ref_cbc(const block_cipher_ctx_t *bc, const uint8_t *iv,
                    const uint8_t *pt, uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t prev[16];
    memcpy(prev, iv, bs);
    for (uint32_t s = 0; s + bs <= len; s += bs) {
        uint8_t tmp[16];
        for (uint32_t i = 0; i < bs; i++) tmp[i] = (uint8_t)(pt[s + i] ^ prev[i]);
        bc->encrypt(bc->ctx, tmp, ct + s);
        memcpy(prev, ct + s, bs);
    }
}

static void ref_cfb(const block_cipher_ctx_t *bc, const uint8_t *iv,
                    const uint8_t *pt, uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t prev[16], ks[16];
    memcpy(prev, iv, bs);
    for (uint32_t s = 0; s + bs <= len; s += bs) {
        bc->encrypt(bc->ctx, prev, ks);
        for (uint32_t i = 0; i < bs; i++) ct[s + i] = (uint8_t)(pt[s + i] ^ ks[i]);
        memcpy(prev, ct + s, bs);
    }
}

static void ref_ofb(const block_cipher_ctx_t *bc, const uint8_t *iv,
                    const uint8_t *pt, uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ks[16];
    memcpy(ks, iv, bs);
    for (uint32_t s = 0; s + bs <= len; s += bs) {
        bc->encrypt(bc->ctx, ks, ks);
        for (uint32_t i = 0; i < bs; i++) ct[s + i] = (uint8_t)(pt[s + i] ^ ks[i]);
    }
}

static void ref_ctr(const block_cipher_ctx_t *bc, const uint8_t *nonce,
                    const uint8_t *pt, uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ctr[16], ks[16];
    memcpy(ctr, nonce, bs);
    uint64_t c = 0;
    for (uint32_t s = 0; s + bs <= len; s += bs) {
        for (int i = 0; i < 8; i++)
            ctr[bs - 8 + i] = (uint8_t)(c >> (56 - 8 * i));
        bc->encrypt(bc->ctx, ctr, ks);
        for (uint32_t i = 0; i < bs; i++) ct[s + i] = (uint8_t)(pt[s + i] ^ ks[i]);
        c++;
    }
}

static void ref_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                    uint64_t data_unit, const uint8_t *pt, uint8_t *ct, uint32_t len) {
    uint8_t tin[16], tweak[16];
    memset(tin, 0, 16);
    for (int i = 0; i < 8; i++) tin[i] = (uint8_t)(data_unit >> (8 * i));
    bc2->encrypt(bc2->ctx, tin, tweak);
    for (uint32_t s = 0; s + 16 <= len; s += 16) {
        uint8_t tmp[16];
        for (int i = 0; i < 16; i++) tmp[i] = (uint8_t)(pt[s + i] ^ tweak[i]);
        bc1->encrypt(bc1->ctx, tmp, ct + s);
        for (int i = 0; i < 16; i++) ct[s + i] = (uint8_t)(ct[s + i] ^ tweak[i]);
        uint8_t carry = 0;
        for (int i = 0; i < 16; i++) {
            uint8_t nc = (uint8_t)((tweak[i] >> 7) & 1);
            tweak[i] = (uint8_t)((tweak[i] << 1) | carry);
            carry = nc;
        }
        if (carry) tweak[0] ^= 0x87;
    }
}

#endif
