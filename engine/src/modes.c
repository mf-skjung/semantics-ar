#include "modes.h"
#include "ctr_layout.h"
#include "../ciphers/cipher_common.h"

static int sar_be_eq(const uint8_t *a, const uint8_t *b, uint32_t n);

static void iv_clear(sar_iv_out_t *iv) {
    if (!iv) return;
    for (int i = 0; i < SEMANTICS_AR_IV_MAX; i++) iv->bytes[i] = 0;
    iv->length = 0;
    iv->tag = 0;
}

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
                       const uint8_t *ct, uint32_t len, uint64_t file_offset,
                       sar_iv_out_t *iv) {
    uint32_t bs = bc->block_size;
    uint8_t dec[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        if (bc->decrypt(bc->ctx, ct + s + bs, dec)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((dec[i] ^ ct[s + i]) != pt[s + bs + i]) { match = 0; break; }
            if (match) {
                if (iv && file_offset == 0 && bc->decrypt(bc->ctx, ct, dec)) {
                    for (uint32_t i = 0; i < bs; i++)
                        iv->bytes[i] = (uint8_t)(dec[i] ^ pt[i]);
                    iv->length = (uint8_t)bs;
                }
                return 1;
            }
        }
    }
    return 0;
}

static int generic_cfb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len, uint64_t file_offset,
                       sar_iv_out_t *iv) {
    uint32_t bs = bc->block_size;
    uint8_t enc[16], seed[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        if (bc->encrypt(bc->ctx, ct + s, enc)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((enc[i] ^ ct[s + bs + i]) != pt[s + bs + i]) { match = 0; break; }
            if (match) {
                if (iv && file_offset == 0) {
                    for (uint32_t i = 0; i < bs; i++)
                        seed[i] = (uint8_t)(pt[i] ^ ct[i]);
                    if (bc->decrypt(bc->ctx, seed, iv->bytes))
                        iv->length = (uint8_t)bs;
                }
                return 1;
            }
        }
    }
    return 0;
}

static int generic_ofb(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len, uint64_t file_offset,
                       sar_iv_out_t *iv) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], enc[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = (uint8_t)(pt[s + i] ^ ct[s + i]);
            ks1[i] = (uint8_t)(pt[s + bs + i] ^ ct[s + bs + i]);
        }
        if (bc->encrypt(bc->ctx, ks0, enc) && sar_be_eq(enc, ks1, bs)) {
            if (iv) {
                uint64_t blk = (file_offset + s) / bs;
                uint8_t cur[16], prev[16];
                for (uint32_t i = 0; i < bs; i++) cur[i] = ks0[i];
                for (uint64_t step = 0; step < blk; step++) {
                    if (!bc->decrypt(bc->ctx, cur, prev)) return 1;
                    for (uint32_t i = 0; i < bs; i++) cur[i] = prev[i];
                }
                if (bc->decrypt(bc->ctx, cur, iv->bytes))
                    iv->length = (uint8_t)bs;
            }
            return 1;
        }
    }
    return 0;
}

static int generic_ctr(const block_cipher_ctx_t *bc, const uint8_t *pt,
                       const uint8_t *ct, uint32_t len, uint64_t file_offset,
                       sar_iv_out_t *iv) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], c0[16], c1[16];
    for (uint32_t s = 0; s < bs && s + 2 * bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = (uint8_t)(pt[s + i] ^ ct[s + i]);
            ks1[i] = (uint8_t)(pt[s + bs + i] ^ ct[s + bs + i]);
        }
        if (!bc->decrypt(bc->ctx, ks0, c0)) continue;
        if (!bc->decrypt(bc->ctx, ks1, c1)) continue;
        for (int t = 0; t < SAR_CTR_LAYOUT_COUNT; t++) {
            const sar_ctr_layout_t *L = &SAR_CTR_LAYOUTS[t];
            if (L->block_size != bs) continue;
            if (!sar_ctr_const_equal(c0, c1, L)) continue;
            uint64_t v0 = sar_ctr_field_read(c0, L);
            uint64_t v1 = sar_ctr_field_read(c1, L);
            if (((v0 + 1) & sar_ctr_field_mask(L->width)) != v1) continue;
            if (iv) {
                uint64_t blk = (file_offset + s) / bs;
                for (uint32_t i = 0; i < bs; i++) iv->bytes[i] = c0[i];
                sar_ctr_field_write(iv->bytes, L, v0 - blk);
                iv->length = (uint8_t)bs;
                iv->tag = (uint8_t)(t + 1);
            }
            return 1;
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
                           uint32_t len, uint64_t file_offset,
                           uint32_t *out_mode, sar_iv_out_t *out_iv) {
    iv_clear(out_iv);
    if (generic_ecb(bc, pt, ct, len)) { *out_mode = SAR_MODE_ECB; return 1; }
    if (len >= 2 * bc->block_size) {
        if (generic_cbc(bc, pt, ct, len, file_offset, out_iv)) { *out_mode = SAR_MODE_CBC; return 1; }
        if (generic_cfb(bc, pt, ct, len, file_offset, out_iv)) { *out_mode = SAR_MODE_CFB; return 1; }
        if (generic_ofb(bc, pt, ct, len, file_offset, out_iv)) { *out_mode = SAR_MODE_OFB; return 1; }
        if (generic_ctr(bc, pt, ct, len, file_offset, out_iv)) { *out_mode = SAR_MODE_CTR; return 1; }
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
