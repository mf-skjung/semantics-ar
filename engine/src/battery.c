#include "conviction_engine.h"
#include "modes.h"
#include "eng_mem.h"
#include "../ciphers/aes.h"
#include "../ciphers/des.h"
#include "../ciphers/sm4.h"
#include "../ciphers/camellia.h"
#include "../ciphers/aria.h"
#include "../ciphers/seed.h"
#include "../ciphers/stream.h"

static int candidate_viable(const uint8_t *d) {
    uint8_t flags[256];
    sar_memset(flags, 0, 256);
    uint32_t unique = 0;
    for (int i = 0; i < 16; i++)
        if (!flags[d[i]]) { flags[d[i]] = 1; unique++; }
    if (unique < 4) return 0;
    for (int i = 1; i < 16; i++)
        if (d[i] != d[0]) return 1;
    return 0;
}

static int is_rc4_sbox(const uint8_t *d) {
    uint8_t seen[256];
    sar_memset(seen, 0, 256);
    for (int i = 0; i < 256; i++) {
        if (seen[d[i]]) return 0;
        seen[d[i]] = 1;
    }
    return 1;
}

static int fill(sar_verdict_t *v, const uint8_t *key, uint32_t klen,
                uint32_t alg, uint32_t mode, uint64_t mp) {
    v->convicted = 1;
    sar_memcpy(v->key, key, klen);
    v->key_length = klen;
    v->algorithm = alg;
    v->mode = mode;
    v->mode_params = mp;
    return 1;
}

static void apply_iv(sar_verdict_t *v, const sar_iv_out_t *iv) {
    sar_memcpy(v->iv, iv->bytes, SEMANTICS_AR_IV_MAX);
    v->iv_length = iv->length;
    v->ctr_layout_tag = iv->tag;
}

static int aes_try_master(const uint8_t *master, uint32_t klen,
                          const uint8_t *pt, const uint8_t *ct, uint32_t len,
                          uint64_t fo, uint32_t alg, sar_verdict_t *v) {
    aes_ctx_t c;
    aes_setkey(&c, master, klen);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
    uint32_t mode;
    sar_iv_out_t iv;
    if (engine_try_block_modes(&bc, pt, ct, len, fo, &mode, &iv)) {
        fill(v, master, klen, alg, mode, 0);
        apply_iv(v, &iv);
        return 1;
    }
    return 0;
}

static int aes_window_battery(const uint8_t *window, uint32_t klen, uint32_t alg,
                              int max_block, const uint8_t *pt, const uint8_t *ct,
                              uint32_t len, uint64_t fo, sar_verdict_t *v) {
    uint8_t variants[2][32];
    sar_memcpy(variants[0], window, klen);
    aes_word_byteswap(variants[1], window, klen);
    for (int var = 0; var < 2; var++) {
        if (aes_try_master(variants[var], klen, pt, ct, len, fo, alg, v))
            return 1;
        for (int b = 1; b <= max_block; b++) {
            uint8_t master[32];
            if (aes_master_from_window(klen, variants[var], b, master))
                if (aes_try_master(master, klen, pt, ct, len, fo, alg, v))
                    return 1;
        }
    }
    return 0;
}

static int block_try(block_cipher_ctx_t *bc, const uint8_t *key, uint32_t klen,
                     uint32_t alg, const uint8_t *pt, const uint8_t *ct,
                     uint32_t len, uint64_t fo, sar_verdict_t *v) {
    uint32_t mode;
    sar_iv_out_t iv;
    if (engine_try_block_modes(bc, pt, ct, len, fo, &mode, &iv)) {
        fill(v, key, klen, alg, mode, 0);
        apply_iv(v, &iv);
        return 1;
    }
    return 0;
}

static int xts_try(block_cipher_ctx_t *bc1, block_cipher_ctx_t *bc2,
                   const uint8_t *keybytes, uint32_t klen, uint32_t alg,
                   const uint8_t *pt, const uint8_t *ct, uint32_t len,
                   uint64_t file_offset, sar_verdict_t *v) {
    uint64_t du;
    if (engine_verify_xts(bc1, bc2, pt, ct, len, file_offset, &du))
        return fill(v, keybytes, klen, alg, SAR_MODE_XTS, du);
    return 0;
}

static int try_block_single(const uint8_t *key, const uint8_t *pt, const uint8_t *ct,
                            uint32_t len, uint64_t fo, sar_verdict_t *v) {
    if (aes_window_battery(key, 16, SAR_ALG_AES_128, 10, pt, ct, len, fo, v))
        return 1;
    {
        sm4_ctx_t s; sm4_key_schedule(&s, key);
        block_cipher_ctx_t bc = { sm4_encrypt_block, sm4_decrypt_block, &s, 16 };
        if (block_try(&bc, key, 16, SAR_ALG_SM4, pt, ct, len, fo, v)) return 1;
    }
    {
        camellia_ctx_t c; camellia_key_schedule(&c, key, 16);
        block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
        if (block_try(&bc, key, 16, SAR_ALG_CAMELLIA, pt, ct, len, fo, v)) return 1;
    }
    {
        aria_ctx_t a; aria_key_schedule(&a, key, 16);
        block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &a, 16 };
        if (block_try(&bc, key, 16, SAR_ALG_ARIA, pt, ct, len, fo, v)) return 1;
    }
    {
        seed_ctx_t s; seed_key_schedule(&s, key);
        block_cipher_ctx_t bc = { seed_encrypt_block, seed_decrypt_block, &s, 16 };
        if (block_try(&bc, key, 16, SAR_ALG_SEED, pt, ct, len, fo, v)) return 1;
    }
    return 0;
}

static int try_block_pair(const uint8_t *a, const uint8_t *b, const uint8_t *pt,
                          const uint8_t *ct, uint32_t len, uint64_t file_offset,
                          sar_verdict_t *v) {
    uint8_t k32[32], k24a[24], k24b[24];
    sar_memcpy(k32, a, 16);
    sar_memcpy(k32 + 16, b, 16);
    sar_memcpy(k24a, a, 16);
    sar_memcpy(k24a + 16, b, 8);
    sar_memcpy(k24b, a + 8, 8);
    sar_memcpy(k24b + 8, b, 16);

    if (aes_window_battery(k32, 32, SAR_ALG_AES_256, 7, pt, ct, len, file_offset, v))
        return 1;
    if (aes_window_battery(k24a, 24, SAR_ALG_AES_192, 7, pt, ct, len, file_offset, v))
        return 1;
    if (aes_window_battery(k24b, 24, SAR_ALG_AES_192, 7, pt, ct, len, file_offset, v))
        return 1;

    {
        des3_ctx_t d;
        des3_setkey(&d, k24a);
        block_cipher_ctx_t bc = { des3_encrypt_block, des3_decrypt_block, &d, 8 };
        if (block_try(&bc, k24a, 24, SAR_ALG_3DES, pt, ct, len, file_offset, v)) return 1;
        des3_setkey(&d, k24b);
        if (block_try(&bc, k24b, 24, SAR_ALG_3DES, pt, ct, len, file_offset, v)) return 1;
    }
    {
        camellia_ctx_t c; camellia_key_schedule(&c, k32, 32);
        block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
        if (block_try(&bc, k32, 32, SAR_ALG_CAMELLIA, pt, ct, len, file_offset, v)) return 1;
    }
    {
        aria_ctx_t ar; aria_key_schedule(&ar, k32, 32);
        block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &ar, 16 };
        if (block_try(&bc, k32, 32, SAR_ALG_ARIA, pt, ct, len, file_offset, v)) return 1;
    }

    {
        aes_ctx_t c1, c2;
        aes_setkey(&c1, a, 16); aes_setkey(&c2, b, 16);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &c1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &c2, 16 };
        if (xts_try(&bc1, &bc2, k32, 32, SAR_ALG_AES_128, pt, ct, len, file_offset, v))
            return 1;
    }
    {
        sm4_ctx_t s1, s2;
        sm4_key_schedule(&s1, a); sm4_key_schedule(&s2, b);
        block_cipher_ctx_t bc1 = { sm4_encrypt_block, sm4_decrypt_block, &s1, 16 };
        block_cipher_ctx_t bc2 = { sm4_encrypt_block, sm4_decrypt_block, &s2, 16 };
        if (xts_try(&bc1, &bc2, k32, 32, SAR_ALG_SM4, pt, ct, len, file_offset, v))
            return 1;
    }
    {
        camellia_ctx_t c1, c2;
        camellia_key_schedule(&c1, a, 16); camellia_key_schedule(&c2, b, 16);
        block_cipher_ctx_t bc1 = { camellia_encrypt_block, camellia_decrypt_block, &c1, 16 };
        block_cipher_ctx_t bc2 = { camellia_encrypt_block, camellia_decrypt_block, &c2, 16 };
        if (xts_try(&bc1, &bc2, k32, 32, SAR_ALG_CAMELLIA, pt, ct, len, file_offset, v))
            return 1;
    }
    {
        aria_ctx_t a1, a2;
        aria_key_schedule(&a1, a, 16); aria_key_schedule(&a2, b, 16);
        block_cipher_ctx_t bc1 = { aria_encrypt_block, aria_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aria_encrypt_block, aria_decrypt_block, &a2, 16 };
        if (xts_try(&bc1, &bc2, k32, 32, SAR_ALG_ARIA, pt, ct, len, file_offset, v))
            return 1;
    }
    {
        seed_ctx_t s1, s2;
        seed_key_schedule(&s1, a); seed_key_schedule(&s2, b);
        block_cipher_ctx_t bc1 = { seed_encrypt_block, seed_decrypt_block, &s1, 16 };
        block_cipher_ctx_t bc2 = { seed_encrypt_block, seed_decrypt_block, &s2, 16 };
        if (xts_try(&bc1, &bc2, k32, 32, SAR_ALG_SEED, pt, ct, len, file_offset, v))
            return 1;
    }
    return 0;
}

static int aes256_xts_window(const uint8_t *win64, const uint8_t *pt, const uint8_t *ct,
                             uint32_t len, uint64_t file_offset, sar_verdict_t *v) {
    aes_ctx_t c1, c2;
    aes_setkey(&c1, win64, 32);
    aes_setkey(&c2, win64 + 32, 32);
    block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &c1, 16 };
    block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &c2, 16 };
    return xts_try(&bc1, &bc2, win64, 64, SAR_ALG_AES_256, pt, ct, len, file_offset, v);
}

static int fill_stream(sar_verdict_t *v, const uint8_t *report_key, uint32_t alg,
                       const uint8_t *nonce, uint8_t nlen, int rounds, int origin) {
    fill(v, report_key, 32, alg, SAR_MODE_STREAM,
         ((uint64_t)(uint32_t)rounds << 8) | (uint64_t)(uint32_t)origin);
    sar_memcpy(v->iv, nonce, nlen);
    v->iv_length = nlen;
    v->ctr_layout_tag = 0;
    return 1;
}

static int try_chacha(const uint8_t *gen_key, const uint8_t *report_key,
                      const uint8_t *gen_nonce12, const uint8_t *persist_nonce,
                      uint8_t persist_len, const uint8_t *pt, const uint8_t *ct,
                      uint32_t sample_size, uint64_t file_offset,
                      uint32_t alg, sar_verdict_t *v) {
    for (int origin = 0; origin < 2; origin++) {
        for (int ri = 0; ri < STREAM_ROUNDS_COUNT; ri++) {
            uint32_t blk = (uint32_t)(file_offset / 64) + (uint32_t)origin;
            uint32_t boff = (uint32_t)(file_offset % 64);
            uint8_t ks[64];
            chacha_block(gen_key, gen_nonce12, blk, STREAM_ROUNDS[ri], ks);
            uint32_t clen = 64 - boff;
            if (clen > sample_size) clen = sample_size;
            if (clen < 16) continue;
            int match = 1;
            for (uint32_t m = 0; m < clen; m++)
                if ((pt[m] ^ ks[boff + m]) != ct[m]) { match = 0; break; }
            if (match)
                return fill_stream(v, report_key, alg, persist_nonce, persist_len,
                                   STREAM_ROUNDS[ri], origin);
        }
    }
    return 0;
}

static int try_salsa(const uint8_t *gen_key, const uint8_t *report_key,
                     const uint8_t *gen_nonce8, const uint8_t *persist_nonce,
                     uint8_t persist_len, const uint8_t *pt, const uint8_t *ct,
                     uint32_t sample_size, uint64_t file_offset,
                     uint32_t alg, sar_verdict_t *v) {
    for (int origin = 0; origin < 2; origin++) {
        uint64_t ctr = file_offset / 64 + (uint64_t)origin;
        uint32_t boff = (uint32_t)(file_offset % 64);
        uint8_t ks[64];
        salsa_block(gen_key, gen_nonce8, ctr, 20, ks);
        uint32_t clen = 64 - boff;
        if (clen > sample_size) clen = sample_size;
        if (clen < 16) continue;
        int match = 1;
        for (uint32_t m = 0; m < clen; m++)
            if ((pt[m] ^ ks[boff + m]) != ct[m]) { match = 0; break; }
        if (match)
            return fill_stream(v, report_key, alg, persist_nonce, persist_len, 20, origin);
    }
    return 0;
}

static int try_stream(const uint8_t (*cands)[16], size_t count, const uint8_t *pt,
                      const uint8_t *ct, uint32_t sample_size, uint64_t file_offset,
                      sar_verdict_t *v) {
    for (size_t i = 0; i + 1 < count; i++) {
        if (!candidate_viable(cands[i])) continue;
        uint8_t key32[32];
        sar_memcpy(key32, cands[i], 16);
        sar_memcpy(key32 + 16, cands[i + 1], 16);
        for (size_t k = 0; k < count; k++) {
            if (k == i || k == i + 1) continue;
            uint8_t nonce12[12];
            sar_memcpy(nonce12, cands[k], 12);
            if (try_chacha(key32, key32, nonce12, nonce12, 12, pt, ct, sample_size,
                           file_offset, SAR_ALG_CHACHA20, v)) return 1;
            uint8_t nonce8[8];
            sar_memcpy(nonce8, cands[k], 8);
            if (try_salsa(key32, key32, nonce8, nonce8, 8, pt, ct, sample_size,
                          file_offset, SAR_ALG_SALSA20, v)) return 1;
            if (k + 1 < count) {
                uint8_t nonce24[24];
                sar_memcpy(nonce24, cands[k], 16);
                sar_memcpy(nonce24 + 16, cands[k + 1], 8);
                uint8_t subkey[32], derived12[12], derived8[8];
                hchacha20(key32, nonce24, subkey);
                sar_memset(derived12, 0, 4);
                sar_memcpy(derived12 + 4, nonce24 + 16, 8);
                if (try_chacha(subkey, key32, derived12, nonce24, 24, pt, ct, sample_size,
                               file_offset, SAR_ALG_XCHACHA20, v)) return 1;
                hsalsa20(key32, nonce24, subkey);
                sar_memcpy(derived8, nonce24 + 16, 8);
                if (try_salsa(subkey, key32, derived8, nonce24, 24, pt, ct, sample_size,
                              file_offset, SAR_ALG_XSALSA20, v)) return 1;
            }
        }
    }
    return 0;
}

static int try_rc4_shortkey(const uint8_t *key, const uint8_t *pt, const uint8_t *ct,
                            uint32_t sample_size, uint64_t file_offset, sar_verdict_t *v) {
    static const uint32_t key_lens[3] = { 5, 8, 16 };
    for (int i = 0; i < 3; i++) {
        if (rc4_verify(key, key_lens[i], pt, ct, sample_size, file_offset))
            return fill(v, key, key_lens[i], SAR_ALG_RC4, SAR_MODE_STREAM, 0);
    }
    return 0;
}

static int scan_battery(const uint8_t *buf, size_t buf_len, const uint8_t *pt,
                        const uint8_t *ct, uint32_t sample_size, uint64_t file_offset,
                        sar_verdict_t *v) {
    for (size_t off = 0; off + 256 <= buf_len; off++) {
        if (is_rc4_sbox(buf + off)) {
            if (off + 258 <= buf_len) {
                if (rc4_verify_state(buf + off, buf[off + 256], buf[off + 257],
                                     pt, ct, sample_size))
                    return fill(v, buf + off, 0, SAR_ALG_RC4, SAR_MODE_STREAM, 0);
            }
            if (off + 264 <= buf_len) {
                uint8_t si = buf[off + 256];
                uint8_t sj = buf[off + 260];
                if (rc4_verify_state(buf + off, si, sj, pt, ct, sample_size))
                    return fill(v, buf + off, 0, SAR_ALG_RC4, SAR_MODE_STREAM, 0);
            }
        }
    }
    for (size_t off = 0; off + 16 <= buf_len; off += 16) {
        if (!candidate_viable(buf + off)) continue;
        if (try_block_single(buf + off, pt, ct, sample_size, file_offset, v))
            return 1;
        if (off + 32 <= buf_len && candidate_viable(buf + off + 16)) {
            if (try_block_pair(buf + off, buf + off + 16, pt, ct, sample_size,
                               file_offset, v))
                return 1;
        }
        if (off + 64 <= buf_len) {
            if (aes256_xts_window(buf + off, pt, ct, sample_size, file_offset, v))
                return 1;
        }
        if (try_rc4_shortkey(buf + off, pt, ct, sample_size, file_offset, v))
            return 1;
    }
    return 0;
}

int sar_convict(const sar_engine_input_t *in, sar_verdict_t *out) {
    sar_memset(out, 0, sizeof(*out));
    if (!in || in->sample_size < 16 || !in->plaintext || !in->ciphertext)
        return 0;

    const uint8_t (*cands)[16] = in->candidates;
    size_t count = in->candidate_count;
    uint32_t len = in->sample_size;
    uint64_t fo = in->file_offset;

    for (size_t i = 0; i < count; i++) {
        if (!candidate_viable(cands[i])) continue;
        if (try_block_single(cands[i], in->plaintext, in->ciphertext, len, fo, out))
            return 1;
    }

    for (size_t i = 0; i < count; i++) {
        if (!candidate_viable(cands[i])) continue;
        for (size_t j = 0; j < count; j++) {
            if (i == j) continue;
            if (try_block_pair(cands[i], cands[j], in->plaintext, in->ciphertext,
                               len, fo, out))
                return 1;
        }
    }

    if (try_stream(cands, count, in->plaintext, in->ciphertext, len, fo, out))
        return 1;

    for (size_t i = 0; i < count; i++) {
        if (!candidate_viable(cands[i])) continue;
        if (try_rc4_shortkey(cands[i], in->plaintext, in->ciphertext, len, fo, out))
            return 1;
    }

    if (in->scan_buffer && in->scan_length >= 16) {
        if (scan_battery(in->scan_buffer, in->scan_length, in->plaintext,
                         in->ciphertext, len, fo, out))
            return 1;
    }

    return 0;
}
