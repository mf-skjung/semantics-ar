#include "modes.h"
#include "../ciphers/cipher_common.h"

static int sar_be_eq(const uint8_t *a, const uint8_t *b, uint32_t n);

void gf128_mul_alpha(uint8_t *tweak) {
    uint8_t carry = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t next_carry = (uint8_t)((tweak[i] >> 7) & 1);
        tweak[i] = (uint8_t)((tweak[i] << 1) | carry);
        carry = next_carry;
    }
    if (carry)
        tweak[0] ^= 0x87;
}

static int generic_ecb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t out[16];
    for (uint32_t s = 0; s < bs && s + bs <= len; s++)
        if (bc->encrypt(bc->ctx, pt + s, out))
            if (sar_be_eq(out, ct + s, bs)) return 1;
    return 0;
}

static int generic_cbc(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t dec[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        if (bc->decrypt(bc->ctx, ct + s + bs, dec)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((dec[i] ^ ct[s + i]) != pt[s + bs + i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int generic_cfb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t enc[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        if (bc->encrypt(bc->ctx, ct + s, enc)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((enc[i] ^ ct[s + bs + i]) != pt[s + bs + i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int generic_ofb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], enc[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = (uint8_t)(pt[s + i] ^ ct[s + i]);
            ks1[i] = (uint8_t)(pt[s + bs + i] ^ ct[s + bs + i]);
        }
        if (bc->encrypt(bc->ctx, ks0, enc))
            if (sar_be_eq(enc, ks1, bs)) return 1;
    }
    return 0;
}

static int generic_ctr(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], c0[16], c1[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = (uint8_t)(pt[s + i] ^ ct[s + i]);
            ks1[i] = (uint8_t)(pt[s + bs + i] ^ ct[s + bs + i]);
        }
        if (!bc->decrypt(bc->ctx, ks0, c0)) continue;
        if (!bc->decrypt(bc->ctx, ks1, c1)) continue;
        if (bs == 16) {
            if (sar_be_eq(c0, c1, 12)) {
                uint32_t v0 = sar_be32(c0 + 12), v1 = sar_be32(c1 + 12);
                if (v1 == v0 + 1) return 1;
            }
            if (sar_be_eq(c0, c1, 8)) {
                uint64_t v0 = sar_le64(c0 + 8), v1 = sar_le64(c1 + 8);
                if (v1 == v0 + 1) return 1;
                v0 = sar_be64(c0 + 8); v1 = sar_be64(c1 + 8);
                if (v1 == v0 + 1) return 1;
            }
            if (c0[0] == c1[0] && sar_be_eq(c0 + 1, c1 + 1, 13)) {
                uint16_t v0 = (uint16_t)(((uint16_t)c0[14] << 8) | c0[15]);
                uint16_t v1 = (uint16_t)(((uint16_t)c1[14] << 8) | c1[15]);
                if (v1 == (uint16_t)(v0 + 1)) return 1;
            }
            { uint64_t lo0 = sar_le64(c0), lo1 = sar_le64(c1),
                       hi0 = sar_le64(c0 + 8), hi1 = sar_le64(c1 + 8);
              if (lo1 == lo0 + 1 && hi0 == hi1) return 1; }
            { uint64_t hi0 = sar_be64(c0), hi1 = sar_be64(c1),
                       lo0 = sar_be64(c0 + 8), lo1 = sar_be64(c1 + 8);
              if (lo1 == lo0 + 1 && hi0 == hi1) return 1; }
        } else if (bs == 8) {
            if (sar_be_eq(c0, c1, 4)) {
                uint32_t v0 = sar_be32(c0 + 4), v1 = sar_be32(c1 + 4);
                if (v1 == v0 + 1) return 1;
            }
            { uint64_t v0 = sar_le64(c0), v1 = sar_le64(c1); if (v1 == v0 + 1) return 1; }
            { uint64_t v0 = sar_be64(c0), v1 = sar_be64(c1); if (v1 == v0 + 1) return 1; }
        }
    }
    return 0;
}

static int sar_be_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

int engine_try_block_modes(const block_cipher_ctx_t *bc,
                           const uint8_t *pt, const uint8_t *ct,
                           uint32_t len, uint32_t *out_mode) {
    if (generic_ecb(bc, pt, ct, len)) { *out_mode = SAR_MODE_ECB; return 1; }
    if (len >= 2 * bc->block_size) {
        if (generic_cbc(bc, pt, ct, len)) { *out_mode = SAR_MODE_CBC; return 1; }
        if (generic_cfb(bc, pt, ct, len)) { *out_mode = SAR_MODE_CFB; return 1; }
        if (generic_ofb(bc, pt, ct, len)) { *out_mode = SAR_MODE_OFB; return 1; }
        if (generic_ctr(bc, pt, ct, len)) { *out_mode = SAR_MODE_CTR; return 1; }
    }
    return 0;
}

static int xts_tweak_match(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                           uint64_t data_unit, uint32_t block_in_unit,
                           const uint8_t *pt, const uint8_t *ct) {
    uint8_t tweak_in[16], tweak[16], tmp[16], enc[16];
    for (int i = 0; i < 16; i++) tweak_in[i] = 0;
    for (int i = 0; i < 8; i++) tweak_in[i] = (uint8_t)(data_unit >> (8 * i));
    if (!bc2->encrypt(bc2->ctx, tweak_in, tweak)) return 0;
    for (uint32_t b = 0; b < block_in_unit; b++) gf128_mul_alpha(tweak);
    for (int i = 0; i < 16; i++) tmp[i] = (uint8_t)(pt[i] ^ tweak[i]);
    if (!bc1->encrypt(bc1->ctx, tmp, enc)) return 0;
    for (int i = 0; i < 16; i++) enc[i] = (uint8_t)(enc[i] ^ tweak[i]);
    return sar_be_eq(enc, ct, 16);
}

int engine_verify_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint64_t file_offset,
                      uint64_t *out_data_unit) {
    uint32_t skip = (uint32_t)((16 - (file_offset & 15)) & 15);
    if (skip + 16 > len) return 0;
    uint64_t aligned = file_offset + skip;
    const uint8_t *apt = pt + skip, *act = ct + skip;
    static const uint64_t data_units[2] = { 512, 4096 };
    for (int i = 0; i < 2; i++) {
        uint64_t du = data_units[i];
        uint64_t unit = aligned / du;
        uint32_t block_in_unit = (uint32_t)((aligned % du) / 16);
        if (xts_tweak_match(bc1, bc2, unit, block_in_unit, apt, act)) {
            *out_data_unit = du;
            return 1;
        }
    }
    return 0;
}
