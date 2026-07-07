#include "sar_recover.h"
#include "sar_keystore.h"
#include "modes.h"
#include "ctr_layout.h"
#include "eng_mem.h"
#include "../ciphers/aes.h"
#include "../ciphers/des.h"
#include "../ciphers/sm4.h"
#include "../ciphers/camellia.h"
#include "../ciphers/aria.h"
#include "../ciphers/seed.h"
#include "../ciphers/stream.h"

typedef struct {
    union {
        aes_ctx_t      aes;
        des3_ctx_t     des3;
        sm4_ctx_t      sm4;
        camellia_ctx_t cam;
        aria_ctx_t     aria;
        seed_ctx_t     seed;
    } u;
} cipher_slot_t;

static int make_block(cipher_slot_t *slot, uint32_t alg, const uint8_t *key,
                      uint32_t klen, block_cipher_ctx_t *bc) {
    switch (alg) {
        case SAR_ALG_AES_128:
        case SAR_ALG_AES_192:
        case SAR_ALG_AES_256:
            aes_setkey(&slot->u.aes, key, klen);
            bc->encrypt = aes_encrypt_block; bc->decrypt = aes_decrypt_block;
            bc->ctx = &slot->u.aes; bc->block_size = 16; return 1;
        case SAR_ALG_3DES:
            des3_setkey(&slot->u.des3, key);
            bc->encrypt = des3_encrypt_block; bc->decrypt = des3_decrypt_block;
            bc->ctx = &slot->u.des3; bc->block_size = 8; return 1;
        case SAR_ALG_SM4:
            sm4_key_schedule(&slot->u.sm4, key);
            bc->encrypt = sm4_encrypt_block; bc->decrypt = sm4_decrypt_block;
            bc->ctx = &slot->u.sm4; bc->block_size = 16; return 1;
        case SAR_ALG_CAMELLIA:
            camellia_key_schedule(&slot->u.cam, key, klen);
            bc->encrypt = camellia_encrypt_block; bc->decrypt = camellia_decrypt_block;
            bc->ctx = &slot->u.cam; bc->block_size = 16; return 1;
        case SAR_ALG_ARIA:
            aria_key_schedule(&slot->u.aria, key, klen);
            bc->encrypt = aria_encrypt_block; bc->decrypt = aria_decrypt_block;
            bc->ctx = &slot->u.aria; bc->block_size = 16; return 1;
        case SAR_ALG_SEED:
            seed_key_schedule(&slot->u.seed, key);
            bc->encrypt = seed_encrypt_block; bc->decrypt = seed_decrypt_block;
            bc->ctx = &slot->u.seed; bc->block_size = 16; return 1;
        default:
            return 0;
    }
}

static int make_xts(cipher_slot_t *s1, cipher_slot_t *s2, uint32_t alg,
                    const uint8_t *key, uint32_t klen,
                    block_cipher_ctx_t *bc1, block_cipher_ctx_t *bc2) {
    uint32_t half = klen / 2;
    if (half == 0) return 0;
    return make_block(s1, alg, key, half, bc1) &&
           make_block(s2, alg, key + half, half, bc2);
}

static int is_block_alg(uint32_t alg) {
    return alg == SAR_ALG_AES_128 || alg == SAR_ALG_AES_192 || alg == SAR_ALG_AES_256 ||
           alg == SAR_ALG_3DES || alg == SAR_ALG_SM4 || alg == SAR_ALG_CAMELLIA ||
           alg == SAR_ALG_ARIA || alg == SAR_ALG_SEED;
}

static int is_chacha_alg(uint32_t alg) {
    return alg == SAR_ALG_CHACHA20 || alg == SAR_ALG_XCHACHA20;
}

static int is_salsa_alg(uint32_t alg) {
    return alg == SAR_ALG_SALSA20 || alg == SAR_ALG_XSALSA20;
}

static void stream_block(const sar_recovery_key_t *rk, uint64_t block_index,
                         uint8_t out[64]) {
    uint32_t rounds = (uint32_t)((rk->mode_params >> 8) & 0xff);
    uint32_t origin = (uint32_t)(rk->mode_params & 0xff);
    uint64_t counter = block_index + origin;
    if (is_chacha_alg(rk->algorithm)) {
        if (rk->algorithm == SAR_ALG_XCHACHA20) {
            uint8_t subkey[32], nonce12[12];
            hchacha20(rk->key, rk->iv, subkey);
            sar_memset(nonce12, 0, 4);
            sar_memcpy(nonce12 + 4, rk->iv + 16, 8);
            chacha_block(subkey, nonce12, (uint32_t)counter, (int)rounds, out);
        } else {
            chacha_block(rk->key, rk->iv, (uint32_t)counter, (int)rounds, out);
        }
    } else {
        if (rk->algorithm == SAR_ALG_XSALSA20) {
            uint8_t subkey[32], nonce8[8];
            hsalsa20(rk->key, rk->iv, subkey);
            sar_memcpy(nonce8, rk->iv + 16, 8);
            salsa_block(subkey, nonce8, counter, (int)rounds, out);
        } else {
            salsa_block(rk->key, rk->iv, counter, (int)rounds, out);
        }
    }
}

static void rc4_ksa(const sar_recovery_key_t *rk, uint8_t S[256]) {
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = (uint8_t)(j + S[i] + rk->key[(uint32_t)i % rk->key_length]);
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
}

static void rec_ecb(block_cipher_ctx_t *bc, const uint8_t *ct, uint8_t *pt,
                    uint64_t start, uint64_t length, uint64_t base) {
    uint32_t bs = bc->block_size;
    uint8_t tmp[16];
    for (uint64_t o = start; o + bs <= start + length; o += bs) {
        bc->decrypt(bc->ctx, ct + (o - base), tmp);
        for (uint32_t i = 0; i < bs; i++) pt[o - base + i] = tmp[i];
    }
}

static int range_iv_block(const sar_recovery_key_t *rk, uint32_t policy,
                          uint64_t o, uint64_t origin, uint32_t bs,
                          const uint8_t *ct, uint64_t base, const uint8_t **prev) {
    if (o == 0 || (policy == SAR_CTR_RESET_PER_CHUNK && o == origin)) {
        if (rk->iv_length < bs) return 0;
        *prev = rk->iv;
    } else {
        *prev = ct + (o - bs - base);
    }
    return 1;
}

static void rec_cbc(block_cipher_ctx_t *bc, const sar_recovery_key_t *rk, uint32_t policy,
                    const uint8_t *ct, uint8_t *pt, uint64_t origin, uint64_t start,
                    uint64_t length, uint64_t base) {
    uint32_t bs = bc->block_size;
    uint8_t tmp[16];
    for (uint64_t o = start; o + bs <= start + length; o += bs) {
        const uint8_t *prev;
        if (!range_iv_block(rk, policy, o, origin, bs, ct, base, &prev)) continue;
        bc->decrypt(bc->ctx, ct + (o - base), tmp);
        for (uint32_t i = 0; i < bs; i++) pt[o - base + i] = (uint8_t)(tmp[i] ^ prev[i]);
    }
}

static void rec_cfb(block_cipher_ctx_t *bc, const sar_recovery_key_t *rk, uint32_t policy,
                    const uint8_t *ct, uint8_t *pt, uint64_t origin, uint64_t start,
                    uint64_t length, uint64_t base) {
    uint32_t bs = bc->block_size;
    uint8_t ks[16];
    uint64_t o = start;
    for (; o + bs <= start + length; o += bs) {
        const uint8_t *prev;
        if (!range_iv_block(rk, policy, o, origin, bs, ct, base, &prev)) continue;
        bc->encrypt(bc->ctx, prev, ks);
        for (uint32_t i = 0; i < bs; i++) pt[o - base + i] = (uint8_t)(ct[o - base + i] ^ ks[i]);
    }
    uint64_t rem = start + length - o;
    if (rem > 0) {
        const uint8_t *prev;
        if (range_iv_block(rk, policy, o, origin, bs, ct, base, &prev)) {
            bc->encrypt(bc->ctx, prev, ks);
            for (uint64_t i = 0; i < rem; i++) pt[o - base + i] = (uint8_t)(ct[o - base + i] ^ ks[i]);
        }
    }
}

static void rec_ofb(block_cipher_ctx_t *bc, const sar_recovery_key_t *rk, uint32_t policy,
                    const uint8_t *ct, uint8_t *pt, uint64_t origin, uint64_t start,
                    uint64_t length, uint64_t base) {
    uint32_t bs = bc->block_size;
    uint8_t O[16];
    uint64_t first_blk = (policy == SAR_CTR_RESET_PER_CHUNK) ? ((start - origin) / bs) : (start / bs);
    bc->encrypt(bc->ctx, rk->iv, O);
    for (uint64_t m = 0; m < first_blk; m++) bc->encrypt(bc->ctx, O, O);
    for (uint64_t o = start; o < start + length; o += bs) {
        uint64_t rem = start + length - o;
        uint32_t n = (rem < bs) ? (uint32_t)rem : bs;
        for (uint32_t i = 0; i < n; i++) pt[o - base + i] = (uint8_t)(ct[o - base + i] ^ O[i]);
        bc->encrypt(bc->ctx, O, O);
    }
}

static void rec_ctr(block_cipher_ctx_t *bc, const sar_recovery_key_t *rk, uint32_t policy,
                    const uint8_t *ct, uint8_t *pt, uint64_t origin, uint64_t start,
                    uint64_t length, uint64_t base) {
    uint32_t bs = bc->block_size;
    const sar_ctr_layout_t *L = &SAR_CTR_LAYOUTS[rk->ctr_layout_tag - 1];
    uint64_t v0 = sar_ctr_field_read(rk->iv, L);
    uint8_t counter[16], ks[16];
    for (uint64_t o = start; o < start + length; o += bs) {
        uint64_t n = (policy == SAR_CTR_RESET_PER_CHUNK) ? ((o - origin) / bs) : (o / bs);
        for (uint32_t i = 0; i < bs; i++) counter[i] = rk->iv[i];
        sar_ctr_field_write(counter, L, v0 + n);
        bc->encrypt(bc->ctx, counter, ks);
        uint64_t rem = start + length - o;
        uint32_t cnt = (rem < bs) ? (uint32_t)rem : bs;
        for (uint32_t i = 0; i < cnt; i++) pt[o - base + i] = (uint8_t)(ct[o - base + i] ^ ks[i]);
    }
}

static void rec_stream(const sar_recovery_key_t *rk, uint32_t policy,
                       const uint8_t *ct, uint8_t *pt, uint64_t origin, uint64_t start,
                       uint64_t length, uint64_t base) {
    uint8_t ks[64];
    uint64_t o = start;
    while (o < start + length) {
        uint64_t eo = (policy == SAR_CTR_RESET_PER_CHUNK) ? (o - origin) : o;
        uint64_t bi = eo / 64;
        uint32_t within = (uint32_t)(eo % 64);
        stream_block(rk, bi, ks);
        uint64_t rem = start + length - o;
        uint32_t span = 64 - within;
        if ((uint64_t)span > rem) span = (uint32_t)rem;
        for (uint32_t i = 0; i < span; i++) pt[o - base + i] = (uint8_t)(ct[o - base + i] ^ ks[within + i]);
        o += span;
    }
}

static void rec_rc4(const sar_recovery_key_t *rk, uint32_t policy,
                    const uint8_t *ct, uint8_t *pt, uint64_t start, uint64_t length) {
    uint8_t S[256];
    rc4_ksa(rk, S);
    uint64_t skip = (policy == SAR_CTR_RESET_PER_CHUNK) ? 0 : start;
    uint8_t ri = 0, rj = 0;
    for (uint64_t n = 0; n < skip; n++) {
        ri++; rj = (uint8_t)(rj + S[ri]);
        uint8_t t = S[ri]; S[ri] = S[rj]; S[rj] = t;
    }
    for (uint64_t o = start; o < start + length; o++) {
        ri++; rj = (uint8_t)(rj + S[ri]);
        uint8_t t = S[ri]; S[ri] = S[rj]; S[rj] = t;
        pt[o] = (uint8_t)(ct[o] ^ S[(uint8_t)(S[ri] + S[rj])]);
    }
}

static void rec_xts(block_cipher_ctx_t *bc1, block_cipher_ctx_t *bc2, uint64_t data_unit,
                    const uint8_t *ct, uint8_t *pt, uint64_t start, uint64_t length, uint64_t base) {
    uint8_t tin[16], tweak[16], tmp[16], dec[16];
    for (uint64_t o = start; o + 16 <= start + length; o += 16) {
        uint64_t du = o / data_unit;
        uint32_t biu = (uint32_t)((o % data_unit) / 16);
        for (int i = 0; i < 16; i++) tin[i] = 0;
        for (int i = 0; i < 8; i++) tin[i] = (uint8_t)(du >> (8 * i));
        bc2->encrypt(bc2->ctx, tin, tweak);
        for (uint32_t b = 0; b < biu; b++) gf128_mul_alpha(tweak);
        for (int i = 0; i < 16; i++) tmp[i] = (uint8_t)(ct[o - base + i] ^ tweak[i]);
        bc1->decrypt(bc1->ctx, tmp, dec);
        for (int i = 0; i < 16; i++) pt[o - base + i] = (uint8_t)(dec[i] ^ tweak[i]);
    }
}

void sar_recovery_key_from_record(const semantics_ar_keystore_record_t *rec,
                                  sar_recovery_key_t *out) {
    sar_memset(out, 0, sizeof(*out));
    out->algorithm = rec->algorithm;
    out->mode = rec->mode;
    out->key_length = rec->key_length;
    uint32_t kl = rec->key_length > SEMANTICS_AR_MAX_KEY_BYTES
                      ? SEMANTICS_AR_MAX_KEY_BYTES : rec->key_length;
    sar_memcpy(out->key, rec->key_bytes, kl);
    out->mode_params = rec->mode_params;
    uint8_t ivl = rec->iv_length > SEMANTICS_AR_IV_MAX ? SEMANTICS_AR_IV_MAX : rec->iv_length;
    sar_memcpy(out->iv, rec->iv, ivl);
    out->iv_length = ivl;
    out->ctr_layout_tag = rec->ctr_layout_tag;
}

void sar_recovery_verify_from_record(const semantics_ar_keystore_record_t *rec,
                                     sar_recovery_verify_t *out) {
    sar_memset(out, 0, sizeof(*out));
    out->sample_offset = rec->sample_offset;
    out->sample_length = rec->sample_length;
    sar_memcpy(out->sample_tag, rec->sample_tag, SEMANTICS_AR_MAC_SIZE);
}

sar_recover_status_t sar_recover_verify(const uint8_t *pt, uint64_t file_size,
                                        const sar_recovery_verify_t *v) {
    uint8_t tag[SEMANTICS_AR_MAC_SIZE];
    if (!pt || !v)
        return SAR_RECOVER_INVALID;
    if (v->sample_length == 0)
        return SAR_RECOVER_DECLINED_MISMATCH;
    if (v->sample_offset > file_size ||
        file_size - v->sample_offset < v->sample_length)
        return SAR_RECOVER_DECLINED_MISMATCH;
    sar_sample_tag(pt + v->sample_offset, v->sample_length, tag);
    if (!sar_ct_equal(tag, v->sample_tag, SEMANTICS_AR_MAC_SIZE))
        return SAR_RECOVER_DECLINED_MISMATCH;
    return SAR_RECOVER_OK;
}

void sar_recovery_key_from_verdict(const sar_verdict_t *v, sar_recovery_key_t *out) {
    sar_memset(out, 0, sizeof(*out));
    out->algorithm = v->algorithm;
    out->mode = v->mode;
    out->key_length = v->key_length;
    uint32_t kl = v->key_length > SEMANTICS_AR_MAX_KEY_BYTES
                      ? SEMANTICS_AR_MAX_KEY_BYTES : v->key_length;
    sar_memcpy(out->key, v->key, kl);
    out->mode_params = v->mode_params;
    uint8_t ivl = v->iv_length > SEMANTICS_AR_IV_MAX ? SEMANTICS_AR_IV_MAX : v->iv_length;
    sar_memcpy(out->iv, v->iv, ivl);
    out->iv_length = ivl;
    out->ctr_layout_tag = v->ctr_layout_tag;
}

int sar_geometry_expand(const sar_geometry_t *geom, uint64_t file_size,
                        sar_range_t *out, uint32_t cap, uint32_t *out_count) {
    uint32_t n = 0;
    if (!geom || !out || !out_count) return -1;
    if (file_size == 0) { *out_count = 0; return 0; }

    switch (geom->mode) {
        case SAR_GEOM_FULL:
            if (cap < 1) return -1;
            out[0].offset = 0; out[0].length = file_size; n = 1;
            break;
        case SAR_GEOM_HEAD: {
            uint64_t len = geom->head_bytes < file_size ? geom->head_bytes : file_size;
            if (len > 0) { if (cap < 1) return -1; out[0].offset = 0; out[0].length = len; n = 1; }
            break;
        }
        case SAR_GEOM_STRIDE: {
            if (geom->stride_bytes == 0 || geom->chunk_bytes == 0) return -1;
            for (uint64_t pos = 0; pos < file_size; pos += geom->stride_bytes) {
                uint64_t avail = file_size - pos;
                uint64_t len = geom->chunk_bytes < avail ? geom->chunk_bytes : avail;
                if (len == 0) continue;
                if (n >= cap) return -1;
                out[n].offset = pos; out[n].length = len; n++;
            }
            break;
        }
        case SAR_GEOM_PERCENT: {
            uint32_t bc = geom->block_count ? geom->block_count : 1;
            uint64_t total = file_size * (uint64_t)geom->percent / 100u;
            uint64_t chunk = total / bc;
            if (chunk == 0) chunk = total;
            uint64_t step = file_size / bc;
            if (chunk == 0 || step == 0) break;
            for (uint32_t i = 0; i < bc; i++) {
                uint64_t off = (uint64_t)i * step;
                if (off >= file_size) break;
                uint64_t avail = file_size - off;
                uint64_t len = chunk < avail ? chunk : avail;
                if (len == 0) continue;
                if (n >= cap) return -1;
                out[n].offset = off; out[n].length = len; n++;
            }
            break;
        }
        case SAR_GEOM_EXPLICIT: {
            if (geom->range_count > 0 && !geom->ranges) return -1;
            for (uint32_t i = 0; i < geom->range_count; i++) {
                uint64_t off = geom->ranges[i].offset;
                if (off >= file_size) continue;
                uint64_t avail = file_size - off;
                uint64_t len = geom->ranges[i].length < avail ? geom->ranges[i].length : avail;
                if (len == 0) continue;
                if (n >= cap) return -1;
                out[n].offset = off; out[n].length = len; n++;
            }
            break;
        }
        default:
            return -1;
    }
    *out_count = n;
    return 0;
}

int sar_geometry_from_family(sar_geometry_mapper_fn mapper,
                             const uint8_t *family_blob, uint32_t blob_len,
                             uint64_t file_size, sar_geometry_t *out) {
    if (!mapper) return -1;
    return mapper(family_blob, blob_len, file_size, out);
}

static int forward_relation(const sar_recovery_key_t *rk, const sar_recovery_sample_t *s) {
    const uint8_t *P = s->plaintext, *C = s->ciphertext;
    uint32_t n = s->length;
    uint64_t so = s->file_offset;
    if (n < 16) return 0;

    if (rk->algorithm == SAR_ALG_RC4) {
        if (rk->key_length == 0) return 0;
        uint8_t S[256]; rc4_ksa(rk, S);
        uint8_t ri = 0, rj = 0;
        for (uint64_t k = 0; k < so; k++) {
            ri++; rj = (uint8_t)(rj + S[ri]); uint8_t t = S[ri]; S[ri] = S[rj]; S[rj] = t;
        }
        for (uint32_t k = 0; k < n; k++) {
            ri++; rj = (uint8_t)(rj + S[ri]); uint8_t t = S[ri]; S[ri] = S[rj]; S[rj] = t;
            if ((uint8_t)(P[k] ^ S[(uint8_t)(S[ri] + S[rj])]) != C[k]) return 0;
        }
        return 1;
    }

    if (is_chacha_alg(rk->algorithm) || is_salsa_alg(rk->algorithm)) {
        if (rk->iv_length == 0) return 0;
        uint8_t ks[64];
        for (uint32_t k = 0; k < n; ) {
            uint64_t eo = so + k;
            uint64_t bi = eo / 64; uint32_t within = (uint32_t)(eo % 64);
            stream_block(rk, bi, ks);
            uint32_t span = 64 - within; if (span > n - k) span = n - k;
            for (uint32_t i = 0; i < span; i++)
                if ((uint8_t)(P[k + i] ^ ks[within + i]) != C[k + i]) return 0;
            k += span;
        }
        return 1;
    }

    if (rk->mode == SAR_MODE_XTS) {
        if (!is_block_alg(rk->algorithm)) return 0;
        cipher_slot_t a, b; block_cipher_ctx_t bc1, bc2;
        if (!make_xts(&a, &b, rk->algorithm, rk->key, rk->key_length, &bc1, &bc2)) return 0;
        uint64_t du = rk->mode_params;
        if (du == 0) return 0;
        uint8_t tin[16], tweak[16], tmp[16], enc[16];
        uint64_t unit = so / du; uint32_t biu = (uint32_t)((so % du) / 16);
        for (int i = 0; i < 16; i++) tin[i] = 0;
        for (int i = 0; i < 8; i++) tin[i] = (uint8_t)(unit >> (8 * i));
        bc2.encrypt(bc2.ctx, tin, tweak);
        for (uint32_t bk = 0; bk < biu; bk++) gf128_mul_alpha(tweak);
        for (int i = 0; i < 16; i++) tmp[i] = (uint8_t)(P[i] ^ tweak[i]);
        bc1.encrypt(bc1.ctx, tmp, enc);
        for (int i = 0; i < 16; i++) if ((uint8_t)(enc[i] ^ tweak[i]) != C[i]) return 0;
        return 1;
    }

    if (is_block_alg(rk->algorithm)) {
        cipher_slot_t slot; block_cipher_ctx_t bc;
        if (!make_block(&slot, rk->algorithm, rk->key, rk->key_length, &bc)) return 0;
        uint32_t bs = bc.block_size;
        if (n < bs) return 0;
        uint8_t e[16], ks[16];
        switch (rk->mode) {
            case SAR_MODE_ECB:
                bc.encrypt(bc.ctx, P, e);
                for (uint32_t i = 0; i < bs; i++) if (e[i] != C[i]) return 0;
                return 1;
            case SAR_MODE_CBC:
                if (so == 0 && rk->iv_length >= bs) {
                    uint8_t tmp[16];
                    for (uint32_t i = 0; i < bs; i++) tmp[i] = (uint8_t)(P[i] ^ rk->iv[i]);
                    bc.encrypt(bc.ctx, tmp, e);
                    for (uint32_t i = 0; i < bs; i++) if (e[i] != C[i]) return 0;
                    return 1;
                }
                return 0;
            case SAR_MODE_CFB:
                if (so == 0 && rk->iv_length >= bs) {
                    bc.encrypt(bc.ctx, rk->iv, ks);
                    for (uint32_t i = 0; i < bs; i++) if ((uint8_t)(P[i] ^ ks[i]) != C[i]) return 0;
                    return 1;
                }
                return 0;
            case SAR_MODE_OFB: {
                if (rk->iv_length < bs) return 0;
                uint8_t O[16];
                uint64_t blk = so / bs;
                bc.encrypt(bc.ctx, rk->iv, O);
                for (uint64_t m = 0; m < blk; m++) bc.encrypt(bc.ctx, O, O);
                for (uint32_t i = 0; i < bs; i++) if ((uint8_t)(P[i] ^ O[i]) != C[i]) return 0;
                return 1;
            }
            case SAR_MODE_CTR: {
                if (rk->iv_length < bs || rk->ctr_layout_tag == 0 ||
                    rk->ctr_layout_tag > SAR_CTR_LAYOUT_COUNT) return 0;
                const sar_ctr_layout_t *L = &SAR_CTR_LAYOUTS[rk->ctr_layout_tag - 1];
                if (L->block_size != bs) return 0;
                uint8_t counter[16];
                for (uint32_t i = 0; i < bs; i++) counter[i] = rk->iv[i];
                sar_ctr_field_write(counter, L, sar_ctr_field_read(rk->iv, L) + so / bs);
                bc.encrypt(bc.ctx, counter, ks);
                for (uint32_t i = 0; i < bs; i++) if ((uint8_t)(P[i] ^ ks[i]) != C[i]) return 0;
                return 1;
            }
            default:
                return 0;
        }
    }
    return 0;
}

int sar_recover_locate_iv(sar_recovery_key_t *rk, const uint8_t *file, uint64_t file_size,
                          const sar_recovery_sample_t *sample) {
    static const uint8_t widths[4] = { 8, 12, 16, 24 };
    if (!rk || !file || !sample) return 0;
    if (rk->mode == SAR_MODE_ECB ||
        (rk->mode == SAR_MODE_STREAM && rk->algorithm == SAR_ALG_RC4)) return 0;

    uint64_t regions[2][2];
    uint64_t head_hi = file_size < 256 ? file_size : 256;
    regions[0][0] = 0; regions[0][1] = head_hi;
    uint64_t tail_lo = file_size > 256 ? file_size - 256 : 0;
    regions[1][0] = tail_lo; regions[1][1] = file_size;

    for (int r = 0; r < 2; r++) {
        for (uint64_t pos = regions[r][0]; pos < regions[r][1]; pos++) {
            for (int wi = 0; wi < 4; wi++) {
                uint8_t w = widths[wi];
                if (pos + w > file_size) continue;
                sar_recovery_key_t cand = *rk;
                sar_memset(cand.iv, 0, SEMANTICS_AR_IV_MAX);
                sar_memcpy(cand.iv, file + pos, w);
                cand.iv_length = w;
                if (cand.mode == SAR_MODE_CTR) {
                    for (uint8_t t = 1; t <= SAR_CTR_LAYOUT_COUNT; t++) {
                        cand.ctr_layout_tag = t;
                        if (forward_relation(&cand, sample)) { *rk = cand; return 1; }
                    }
                } else if (forward_relation(&cand, sample)) {
                    *rk = cand; return 1;
                }
            }
        }
    }
    return 0;
}

static sar_recover_status_t precheck(const sar_recovery_key_t *rk) {
    switch (rk->mode) {
        case SAR_MODE_ECB:
        case SAR_MODE_CBC:
        case SAR_MODE_CFB:
            if (!is_block_alg(rk->algorithm)) return SAR_RECOVER_DECLINED_KEY;
            return SAR_RECOVER_OK;
        case SAR_MODE_OFB:
            if (!is_block_alg(rk->algorithm)) return SAR_RECOVER_DECLINED_KEY;
            if (rk->iv_length == 0) return SAR_RECOVER_DECLINED_IV;
            return SAR_RECOVER_OK;
        case SAR_MODE_CTR:
            if (!is_block_alg(rk->algorithm)) return SAR_RECOVER_DECLINED_KEY;
            if (rk->iv_length == 0 || rk->ctr_layout_tag == 0 ||
                rk->ctr_layout_tag > SAR_CTR_LAYOUT_COUNT) return SAR_RECOVER_DECLINED_IV;
            return SAR_RECOVER_OK;
        case SAR_MODE_XTS:
            if (!is_block_alg(rk->algorithm)) return SAR_RECOVER_DECLINED_KEY;
            if (rk->mode_params != 512 && rk->mode_params != 4096) return SAR_RECOVER_DECLINED_KEY;
            return SAR_RECOVER_OK;
        case SAR_MODE_STREAM:
            if (rk->algorithm == SAR_ALG_RC4) {
                if (rk->key_length == 0) return SAR_RECOVER_DECLINED_STATE_ONLY;
                return SAR_RECOVER_OK;
            }
            if (!is_chacha_alg(rk->algorithm) && !is_salsa_alg(rk->algorithm))
                return SAR_RECOVER_DECLINED_KEY;
            if (rk->iv_length == 0) return SAR_RECOVER_DECLINED_IV;
            return SAR_RECOVER_OK;
        default:
            return SAR_RECOVER_DECLINED_KEY;
    }
}

static sar_recover_status_t build_ciphers(const sar_recovery_key_t *rk,
                                          cipher_slot_t *s1, cipher_slot_t *s2,
                                          block_cipher_ctx_t *bc, block_cipher_ctx_t *bc1,
                                          block_cipher_ctx_t *bc2) {
    if (rk->mode == SAR_MODE_XTS) {
        if (!make_xts(s1, s2, rk->algorithm, rk->key, rk->key_length, bc1, bc2))
            return SAR_RECOVER_DECLINED_KEY;
    } else if (is_block_alg(rk->algorithm)) {
        if (!make_block(s1, rk->algorithm, rk->key, rk->key_length, bc))
            return SAR_RECOVER_DECLINED_KEY;
    }
    return SAR_RECOVER_OK;
}

static void recover_one_range(const sar_recovery_key_t *rk, uint32_t policy,
                              block_cipher_ctx_t *bc, block_cipher_ctx_t *bc1,
                              block_cipher_ctx_t *bc2, const uint8_t *ct, uint8_t *pt,
                              uint64_t origin, uint64_t start, uint64_t length, uint64_t base) {
    switch (rk->mode) {
        case SAR_MODE_ECB: rec_ecb(bc, ct, pt, start, length, base); break;
        case SAR_MODE_CBC: rec_cbc(bc, rk, policy, ct, pt, origin, start, length, base); break;
        case SAR_MODE_CFB: rec_cfb(bc, rk, policy, ct, pt, origin, start, length, base); break;
        case SAR_MODE_OFB: rec_ofb(bc, rk, policy, ct, pt, origin, start, length, base); break;
        case SAR_MODE_CTR: rec_ctr(bc, rk, policy, ct, pt, origin, start, length, base); break;
        case SAR_MODE_XTS: rec_xts(bc1, bc2, rk->mode_params, ct, pt, start, length, base); break;
        case SAR_MODE_STREAM:
            if (rk->algorithm == SAR_ALG_RC4) rec_rc4(rk, policy, ct, pt, start, length);
            else rec_stream(rk, policy, ct, pt, origin, start, length, base);
            break;
        default: break;
    }
}

static sar_recover_status_t recover_ranges_into(const sar_recovery_key_t *rk,
                                                const sar_range_t *ranges, uint32_t nr,
                                                uint32_t policy,
                                                const uint8_t *ct, uint8_t *pt,
                                                uint64_t file_size, uint64_t base) {
    sar_recover_status_t pc = precheck(rk);
    if (pc != SAR_RECOVER_OK) return pc;

    if (base != 0 && rk->mode == SAR_MODE_STREAM && rk->algorithm == SAR_ALG_RC4)
        return SAR_RECOVER_DECLINED_GEOMETRY;

    cipher_slot_t s1, s2;
    block_cipher_ctx_t bc, bc1, bc2;
    sar_recover_status_t cs = build_ciphers(rk, &s1, &s2, &bc, &bc1, &bc2);
    if (cs != SAR_RECOVER_OK) return cs;

    for (uint32_t r = 0; r < nr; r++) {
        uint64_t start = ranges[r].offset, length = ranges[r].length;
        if (start > file_size || file_size - start < length) continue;
        recover_one_range(rk, policy, &bc, &bc1, &bc2, ct, pt, start, start, length, base);
    }

    return SAR_RECOVER_OK;
}

sar_recover_status_t sar_recover_buffer(const sar_recovery_key_t *rk,
                                        const sar_geometry_t *geom,
                                        const uint8_t *ct, uint8_t *pt,
                                        uint64_t file_size) {
    if (!rk || !ct || !pt) return SAR_RECOVER_INVALID;

    static sar_range_t ranges[SAR_MAX_RANGES];
    sar_geometry_t full;
    uint32_t nr = 0;
    uint32_t policy = SAR_CTR_CONTINUOUS;

    if (geom) {
        if (sar_geometry_expand(geom, file_size, ranges, SAR_MAX_RANGES, &nr) != 0)
            return SAR_RECOVER_DECLINED_GEOMETRY;
        policy = geom->counter_policy;
    } else {
        sar_memset(&full, 0, sizeof(full));
        full.mode = SAR_GEOM_FULL;
        if (sar_geometry_expand(&full, file_size, ranges, SAR_MAX_RANGES, &nr) != 0)
            return SAR_RECOVER_DECLINED_GEOMETRY;
    }

    for (uint64_t i = 0; i < file_size; i++) pt[i] = ct[i];

    return recover_ranges_into(rk, ranges, nr, policy, ct, pt, file_size, 0);
}

sar_recover_status_t sar_recover_range(const sar_recovery_key_t *rk,
                                       const uint8_t *ct, uint8_t *pt,
                                       uint64_t file_size,
                                       uint64_t range_offset, uint64_t range_length) {
    sar_range_t range;
    if (!rk || !ct || !pt) return SAR_RECOVER_INVALID;
    if (range_length == 0) return SAR_RECOVER_OK;
    range.offset = range_offset;
    range.length = range_length;
    return recover_ranges_into(rk, &range, 1, SAR_CTR_CONTINUOUS, ct, pt, file_size, 0);
}

sar_recover_status_t sar_recover_chunk(const sar_recovery_key_t *rk, uint32_t policy,
                                       const uint8_t *ct, uint8_t *pt,
                                       uint64_t origin, uint64_t start, uint64_t length,
                                       uint64_t base, uint64_t file_size) {
    if (!rk || !ct || !pt) return SAR_RECOVER_INVALID;
    if (length == 0) return SAR_RECOVER_OK;
    if (start < origin || start > file_size || file_size - start < length)
        return SAR_RECOVER_INVALID;
    sar_recover_status_t pc = precheck(rk);
    if (pc != SAR_RECOVER_OK) return pc;
    if (rk->mode == SAR_MODE_STREAM && rk->algorithm == SAR_ALG_RC4)
        return SAR_RECOVER_DECLINED_GEOMETRY;
    cipher_slot_t s1, s2;
    block_cipher_ctx_t bc, bc1, bc2;
    sar_recover_status_t cs = build_ciphers(rk, &s1, &s2, &bc, &bc1, &bc2);
    if (cs != SAR_RECOVER_OK) return cs;
    recover_one_range(rk, policy, &bc, &bc1, &bc2, ct, pt, origin, start, length, base);
    return SAR_RECOVER_OK;
}
