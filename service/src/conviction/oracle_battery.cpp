#include "oracle_battery.h"
#include "mode_verify.h"
#include "ciphers/cipher_common.h"
#include "ciphers/sm4.h"
#include "ciphers/camellia.h"
#include "ciphers/aria.h"
#include "ciphers/seed.h"
#include "ciphers/stream.h"
#include "ciphers/sosemanuk.h"
#include "ciphers/hc128.h"
#include <string.h>

BCRYPT_ALG_HANDLE g_hAesEcb = NULL;
BCRYPT_ALG_HANDLE g_h3DesEcb = NULL;

static int aes_modes_for_key(const uint8_t *key, uint32_t klen,
                             const uint8_t *pt, const uint8_t *ct,
                             uint32_t len, uint32_t *out_mode) {
    BCRYPT_KEY_HANDLE hk = NULL;
    if (BCryptGenerateSymmetricKey(g_hAesEcb, &hk, NULL, 0, (PUCHAR)key, klen, 0) != 0)
        return 0;
    int r = try_bcrypt_modes(hk, pt, ct, len, out_mode);
    BCryptDestroyKey(hk);
    return r;
}

static int aes_xts_for_keys(const uint8_t *k1, uint32_t k1len,
                            const uint8_t *k2, uint32_t k2len,
                            const uint8_t *pt, const uint8_t *ct,
                            uint32_t len, uint64_t file_offset) {
    BCRYPT_KEY_HANDLE hk1 = NULL, hk2 = NULL;
    if (BCryptGenerateSymmetricKey(g_hAesEcb, &hk1, NULL, 0, (PUCHAR)k1, k1len, 0) != 0)
        return 0;
    if (BCryptGenerateSymmetricKey(g_hAesEcb, &hk2, NULL, 0, (PUCHAR)k2, k2len, 0) != 0) {
        BCryptDestroyKey(hk1);
        return 0;
    }
    int r = verify_bcrypt_xts(hk1, hk2, pt, ct, len, file_offset);
    BCryptDestroyKey(hk1);
    BCryptDestroyKey(hk2);
    return r;
}

static int des3_block_encrypt(void *vctx, const uint8_t *in, uint8_t *out) {
    ULONG cb;
    return BCryptEncrypt(*(BCRYPT_KEY_HANDLE *)vctx, (PUCHAR)in, 8, NULL, NULL, 0,
                         out, 8, &cb, 0) == 0;
}

static int des3_block_decrypt(void *vctx, const uint8_t *in, uint8_t *out) {
    ULONG cb;
    return BCryptDecrypt(*(BCRYPT_KEY_HANDLE *)vctx, (PUCHAR)in, 8, NULL, NULL, 0,
                         out, 8, &cb, 0) == 0;
}

static int des3_modes_for_key(const uint8_t *k24,
                              const uint8_t *pt, const uint8_t *ct,
                              uint32_t len, uint32_t *out_mode) {
    BCRYPT_KEY_HANDLE h3 = NULL;
    if (BCryptGenerateSymmetricKey(g_h3DesEcb, &h3, NULL, 0, (PUCHAR)k24, 24, 0) != 0)
        return 0;
    block_cipher_ctx_t bc = { des3_block_encrypt, des3_block_decrypt, &h3, 8 };
    int r = generic_try_all_modes(&bc, pt, ct, len, out_mode);
    BCryptDestroyKey(h3);
    return r;
}

static void fill_result(oracle_result_t *result, const uint8_t *key, uint32_t key_length,
                        uint32_t algorithm, uint32_t mode, uint32_t tid, uint32_t ridx) {
    result->confirmed = 1;
    memcpy(result->key, key, key_length);
    result->key_length = key_length;
    result->algorithm = algorithm;
    result->mode = mode;
    result->thread_id = tid;
    result->register_index = ridx;
}

static int sm4_modes(const uint8_t *key, const uint8_t *pt, const uint8_t *ct,
                     uint32_t len, uint32_t *out_mode) {
    sm4_ctx_t sm4;
    sm4_key_schedule(&sm4, key);
    block_cipher_ctx_t bc = { sm4_encrypt_block, sm4_decrypt_block, &sm4, 16 };
    return generic_try_all_modes(&bc, pt, ct, len, out_mode);
}

static int sm4_xts(const uint8_t *k1, const uint8_t *k2, const uint8_t *pt,
                   const uint8_t *ct, uint32_t len, uint64_t file_offset) {
    sm4_ctx_t s1, s2;
    sm4_key_schedule(&s1, k1);
    sm4_key_schedule(&s2, k2);
    block_cipher_ctx_t bc1 = { sm4_encrypt_block, sm4_decrypt_block, &s1, 16 };
    block_cipher_ctx_t bc2 = { sm4_encrypt_block, sm4_decrypt_block, &s2, 16 };
    return generic_verify_xts(&bc1, &bc2, pt, ct, len, file_offset);
}

static int camellia_modes(const uint8_t *key, uint32_t klen, const uint8_t *pt,
                          const uint8_t *ct, uint32_t len, uint32_t *out_mode) {
    camellia_ctx_t cam;
    camellia_key_schedule(&cam, key, klen);
    block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &cam, 16 };
    return generic_try_all_modes(&bc, pt, ct, len, out_mode);
}

static int camellia_xts(const uint8_t *k1, const uint8_t *k2, uint32_t klen,
                        const uint8_t *pt, const uint8_t *ct, uint32_t len,
                        uint64_t file_offset) {
    camellia_ctx_t c1, c2;
    camellia_key_schedule(&c1, k1, klen);
    camellia_key_schedule(&c2, k2, klen);
    block_cipher_ctx_t bc1 = { camellia_encrypt_block, camellia_decrypt_block, &c1, 16 };
    block_cipher_ctx_t bc2 = { camellia_encrypt_block, camellia_decrypt_block, &c2, 16 };
    return generic_verify_xts(&bc1, &bc2, pt, ct, len, file_offset);
}

static int aria_modes(const uint8_t *key, uint32_t klen, const uint8_t *pt,
                      const uint8_t *ct, uint32_t len, uint32_t *out_mode) {
    aria_ctx_t ar;
    aria_key_schedule(&ar, key, klen);
    block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &ar, 16 };
    return generic_try_all_modes(&bc, pt, ct, len, out_mode);
}

static int aria_xts(const uint8_t *k1, const uint8_t *k2, uint32_t klen,
                    const uint8_t *pt, const uint8_t *ct, uint32_t len,
                    uint64_t file_offset) {
    aria_ctx_t a1, a2;
    aria_key_schedule(&a1, k1, klen);
    aria_key_schedule(&a2, k2, klen);
    block_cipher_ctx_t bc1 = { aria_encrypt_block, aria_decrypt_block, &a1, 16 };
    block_cipher_ctx_t bc2 = { aria_encrypt_block, aria_decrypt_block, &a2, 16 };
    return generic_verify_xts(&bc1, &bc2, pt, ct, len, file_offset);
}

static int seed_modes(const uint8_t *key, const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint32_t *out_mode) {
    seed_ctx_t sd;
    seed_key_schedule(&sd, key);
    block_cipher_ctx_t bc = { seed_encrypt_block, seed_decrypt_block, &sd, 16 };
    return generic_try_all_modes(&bc, pt, ct, len, out_mode);
}

static int seed_xts(const uint8_t *k1, const uint8_t *k2, const uint8_t *pt,
                    const uint8_t *ct, uint32_t len, uint64_t file_offset) {
    seed_ctx_t s1, s2;
    seed_key_schedule(&s1, k1);
    seed_key_schedule(&s2, k2);
    block_cipher_ctx_t bc1 = { seed_encrypt_block, seed_decrypt_block, &s1, 16 };
    block_cipher_ctx_t bc2 = { seed_encrypt_block, seed_decrypt_block, &s2, 16 };
    return generic_verify_xts(&bc1, &bc2, pt, ct, len, file_offset);
}

static int try_block_single(const uint8_t *key,
                            const uint8_t *pt, const uint8_t *ct,
                            uint32_t len, uint64_t file_offset,
                            oracle_result_t *result, uint32_t tid, uint32_t ridx) {
    uint32_t mode;
    if (!is_candidate_viable(key))
        return 0;
    if (aes_modes_for_key(key, 16, pt, ct, len, &mode)) {
        fill_result(result, key, 16, ORACLE_ALG_AES_128, mode, tid, ridx);
        return 1;
    }
    if (sm4_modes(key, pt, ct, len, &mode)) {
        fill_result(result, key, 16, ORACLE_ALG_SM4, mode, tid, ridx);
        return 1;
    }
    if (camellia_modes(key, 16, pt, ct, len, &mode)) {
        fill_result(result, key, 16, ORACLE_ALG_CAMELLIA, mode, tid, ridx);
        return 1;
    }
    if (aria_modes(key, 16, pt, ct, len, &mode)) {
        fill_result(result, key, 16, ORACLE_ALG_ARIA, mode, tid, ridx);
        return 1;
    }
    if (seed_modes(key, pt, ct, len, &mode)) {
        fill_result(result, key, 16, ORACLE_ALG_SEED, mode, tid, ridx);
        return 1;
    }
    return 0;
}

static int try_block_pair(const uint8_t *a, const uint8_t *b,
                          const uint8_t *pt, const uint8_t *ct,
                          uint32_t len, uint64_t file_offset,
                          oracle_result_t *result, uint32_t tid, uint32_t ridx) {
    uint32_t mode;
    uint8_t k32[32];
    memcpy(k32, a, 16);
    memcpy(k32 + 16, b, 16);

    if (aes_modes_for_key(k32, 32, pt, ct, len, &mode)) {
        fill_result(result, k32, 32, ORACLE_ALG_AES_256, mode, tid, ridx);
        return 1;
    }
    if (aes_xts_for_keys(a, 16, b, 16, pt, ct, len, file_offset)) {
        fill_result(result, k32, 32, ORACLE_ALG_AES_128, ORACLE_MODE_XTS, tid, ridx);
        return 1;
    }

    uint8_t k24a[24], k24b[24];
    memcpy(k24a, a, 16);
    memcpy(k24a + 16, b, 8);
    memcpy(k24b, a + 8, 8);
    memcpy(k24b + 8, b, 16);
    if (aes_modes_for_key(k24a, 24, pt, ct, len, &mode)) {
        fill_result(result, k24a, 24, ORACLE_ALG_AES_192, mode, tid, ridx);
        return 1;
    }
    if (aes_modes_for_key(k24b, 24, pt, ct, len, &mode)) {
        fill_result(result, k24b, 24, ORACLE_ALG_AES_192, mode, tid, ridx);
        return 1;
    }
    if (des3_modes_for_key(k24a, pt, ct, len, &mode)) {
        fill_result(result, k24a, 24, ORACLE_ALG_3DES, mode, tid, ridx);
        return 1;
    }
    if (des3_modes_for_key(k24b, pt, ct, len, &mode)) {
        fill_result(result, k24b, 24, ORACLE_ALG_3DES, mode, tid, ridx);
        return 1;
    }

    if (sm4_xts(a, b, pt, ct, len, file_offset)) {
        fill_result(result, k32, 32, ORACLE_ALG_SM4, ORACLE_MODE_XTS, tid, ridx);
        return 1;
    }
    if (camellia_modes(k32, 32, pt, ct, len, &mode)) {
        fill_result(result, k32, 32, ORACLE_ALG_CAMELLIA, mode, tid, ridx);
        return 1;
    }
    if (camellia_xts(a, b, 16, pt, ct, len, file_offset)) {
        fill_result(result, k32, 32, ORACLE_ALG_CAMELLIA, ORACLE_MODE_XTS, tid, ridx);
        return 1;
    }
    if (aria_modes(k32, 32, pt, ct, len, &mode)) {
        fill_result(result, k32, 32, ORACLE_ALG_ARIA, mode, tid, ridx);
        return 1;
    }
    if (aria_xts(a, b, 16, pt, ct, len, file_offset)) {
        fill_result(result, k32, 32, ORACLE_ALG_ARIA, ORACLE_MODE_XTS, tid, ridx);
        return 1;
    }
    if (seed_xts(a, b, pt, ct, len, file_offset)) {
        fill_result(result, k32, 32, ORACLE_ALG_SEED, ORACLE_MODE_XTS, tid, ridx);
        return 1;
    }
    return 0;
}

int oracle_try_register_block_keys(
    const oracle_candidate_t *cands, uint32_t count,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result) {

    for (uint32_t i = 0; i < count; i++) {
        if (try_block_single(cands[i].value, pt, ct, sample_size, file_offset,
                             result, cands[i].thread_id, cands[i].register_index))
            return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = 0; j < count; j++) {
            if (i == j) continue;
            if (try_block_pair(cands[i].value, cands[j].value, pt, ct, sample_size,
                              file_offset, result,
                              cands[i].thread_id, cands[i].register_index))
                return 1;
        }
    }
    return 0;
}

static int try_chacha_family(const uint8_t *key, const uint8_t *nonce12,
                             const uint8_t *pt, const uint8_t *ct,
                             uint32_t sample_size, uint64_t file_offset,
                             oracle_result_t *result, uint32_t algorithm,
                             uint32_t tid, uint32_t ridx) {
    for (int ri = 0; ri < STREAM_ROUNDS_COUNT; ri++) {
        int rounds = STREAM_ROUNDS[ri];
        uint32_t blk = (uint32_t)(file_offset / 64);
        uint32_t boff = (uint32_t)(file_offset % 64);
        uint8_t ks[64];
        chacha_block(key, nonce12, blk, rounds, ks);
        uint32_t clen = 64 - boff;
        if (clen > sample_size) clen = sample_size;
        if (clen < 16) continue;
        int match = 1;
        for (uint32_t m = 0; m < clen; m++)
            if ((pt[m] ^ ks[boff + m]) != ct[m]) { match = 0; break; }
        if (match) {
            fill_result(result, key, 32, algorithm, ORACLE_MODE_STREAM, tid, ridx);
            return 1;
        }
    }
    return 0;
}

static int try_salsa_family(const uint8_t *key, const uint8_t *nonce8,
                            const uint8_t *pt, const uint8_t *ct,
                            uint32_t sample_size, uint64_t file_offset,
                            oracle_result_t *result, uint32_t algorithm,
                            uint32_t tid, uint32_t ridx) {
    uint64_t ctr = file_offset / 64;
    uint32_t boff = (uint32_t)(file_offset % 64);
    uint8_t ks[64];
    salsa_block(key, nonce8, ctr, 20, ks);
    uint32_t clen = 64 - boff;
    if (clen > sample_size) clen = sample_size;
    if (clen < 16) return 0;
    int match = 1;
    for (uint32_t m = 0; m < clen; m++)
        if ((pt[m] ^ ks[boff + m]) != ct[m]) { match = 0; break; }
    if (match) {
        fill_result(result, key, 32, algorithm, ORACLE_MODE_STREAM, tid, ridx);
        return 1;
    }
    return 0;
}

static int try_stream_for_key(const oracle_candidate_t *cands, uint32_t count,
                              uint32_t i, uint32_t k,
                              const uint8_t *pt, const uint8_t *ct,
                              uint32_t sample_size, uint64_t file_offset,
                              oracle_result_t *result) {
    const uint8_t *key = cands[i].value + 0;
    uint8_t key32[32];
    if (i + 1 >= count || cands[i + 1].thread_id != cands[i].thread_id)
        return 0;
    memcpy(key32, cands[i].value, 16);
    memcpy(key32 + 16, cands[i + 1].value, 16);
    key = key32;

    uint32_t tid = cands[i].thread_id;
    uint32_t ridx = cands[i].register_index;

    uint8_t nonce12[12];
    if (k + 1 < count && cands[k + 1].thread_id == cands[k].thread_id) {
        memset(nonce12, 0, 4);
        memcpy(nonce12 + 4, cands[k].value, 8);
    } else {
        memset(nonce12, 0, 4);
        memcpy(nonce12 + 4, cands[k].value, 8);
    }
    if (try_chacha_family(key, nonce12, pt, ct, sample_size, file_offset,
                          result, ORACLE_ALG_CHACHA20, tid, ridx))
        return 1;

    uint8_t nonce8[8];
    memcpy(nonce8, cands[k].value, 8);
    if (try_salsa_family(key, nonce8, pt, ct, sample_size, file_offset,
                         result, ORACLE_ALG_SALSA20, tid, ridx))
        return 1;

    uint8_t nonce24[24];
    if (k + 1 < count && cands[k + 1].thread_id == cands[k].thread_id) {
        memcpy(nonce24, cands[k].value, 16);
        memcpy(nonce24 + 16, cands[k + 1].value, 8);
    } else {
        memcpy(nonce24, cands[k].value, 16);
        memset(nonce24 + 16, 0, 8);
    }

    uint8_t subkey[32];
    hchacha20(key, nonce24, subkey);
    uint8_t derived12[12];
    memset(derived12, 0, 4);
    memcpy(derived12 + 4, nonce24 + 16, 8);
    if (try_chacha_family(subkey, derived12, pt, ct, sample_size, file_offset,
                          result, ORACLE_ALG_XCHACHA20, tid, ridx))
        return 1;

    hsalsa20(key, nonce24, subkey);
    uint8_t derived8[8];
    memcpy(derived8, nonce24 + 16, 8);
    if (try_salsa_family(subkey, derived8, pt, ct, sample_size, file_offset,
                         result, ORACLE_ALG_XSALSA20, tid, ridx))
        return 1;

    return 0;
}

int oracle_try_register_stream_keys(
    const oracle_candidate_t *cands, uint32_t count, DWORD writing_tid,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result) {

    UNREFERENCED_PARAMETER(writing_tid);

    for (uint32_t i = 0; i < count; i++) {
        if (!is_candidate_viable(cands[i].value)) continue;
        for (uint32_t k = 0; k < count; k++) {
            if (k == i) continue;
            if (try_stream_for_key(cands, count, i, k, pt, ct, sample_size,
                                   file_offset, result))
                return 1;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!is_candidate_viable(cands[i].value)) continue;
        for (uint32_t j = 0; j < count; j++) {
            if (i == j) continue;
            if (sosemanuk_verify(cands[i].value, 16, cands[j].value,
                                 pt, ct, sample_size, file_offset)) {
                uint8_t k32[32];
                memcpy(k32, cands[i].value, 16);
                memcpy(k32 + 16, cands[j].value, 16);
                fill_result(result, k32, 32, ORACLE_ALG_SOSEMANUK, ORACLE_MODE_STREAM,
                            cands[i].thread_id, cands[i].register_index);
                return 1;
            }
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!is_candidate_viable(cands[i].value)) continue;
        for (uint32_t j = 0; j < count; j++) {
            if (i == j) continue;
            if (hc128_verify(cands[i].value, cands[j].value,
                             pt, ct, sample_size, file_offset)) {
                uint8_t k32[32];
                memcpy(k32, cands[i].value, 16);
                memcpy(k32 + 16, cands[j].value, 16);
                fill_result(result, k32, 32, ORACLE_ALG_HC128, ORACLE_MODE_STREAM,
                            cands[i].thread_id, cands[i].register_index);
                return 1;
            }
        }
    }

    return 0;
}

static int try_rc4_at(const uint8_t *key, uint32_t key_len,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t sample_size, uint64_t file_offset,
                      oracle_result_t *result) {
    if (rc4_verify(key, key_len, pt, ct, sample_size, file_offset)) {
        fill_result(result, key, key_len, ORACLE_ALG_RC4, ORACLE_MODE_STREAM, 0, 0xFFFFFFFF);
        return 1;
    }
    return 0;
}

int oracle_try_buffer_keys(
    const uint8_t *buf, SIZE_T buf_len,
    const uint8_t *pt, const uint8_t *ct,
    uint32_t sample_size, uint64_t file_offset,
    oracle_result_t *result) {

    for (SIZE_T off = 0; off + 16 <= buf_len; off += 16) {
        if (!is_candidate_viable(buf + off)) continue;

        if (try_block_single(buf + off, pt, ct, sample_size, file_offset,
                             result, 0, 0xFFFFFFFF))
            return 1;

        if (off + 24 <= buf_len) {
            uint32_t mode;
            if (aes_modes_for_key(buf + off, 24, pt, ct, sample_size, &mode)) {
                fill_result(result, buf + off, 24, ORACLE_ALG_AES_192, mode, 0, 0xFFFFFFFF);
                return 1;
            }
            if (des3_modes_for_key(buf + off, pt, ct, sample_size, &mode)) {
                fill_result(result, buf + off, 24, ORACLE_ALG_3DES, mode, 0, 0xFFFFFFFF);
                return 1;
            }
            if (camellia_modes(buf + off, 24, pt, ct, sample_size, &mode)) {
                fill_result(result, buf + off, 24, ORACLE_ALG_CAMELLIA, mode, 0, 0xFFFFFFFF);
                return 1;
            }
            if (aria_modes(buf + off, 24, pt, ct, sample_size, &mode)) {
                fill_result(result, buf + off, 24, ORACLE_ALG_ARIA, mode, 0, 0xFFFFFFFF);
                return 1;
            }
        }

        if (off + 32 <= buf_len && is_candidate_viable(buf + off + 16)) {
            if (try_block_pair(buf + off, buf + off + 16, pt, ct, sample_size,
                              file_offset, result, 0, 0xFFFFFFFF))
                return 1;
        }

        if (off + 64 <= buf_len) {
            int viable = 1;
            for (int b = 1; b < 4 && viable; b++)
                viable = is_candidate_viable(buf + off + b * 16);
            if (viable) {
                if (aes_xts_for_keys(buf + off, 32, buf + off + 32, 32, pt, ct,
                                     sample_size, file_offset)) {
                    fill_result(result, buf + off, 64, ORACLE_ALG_AES_256,
                                ORACLE_MODE_XTS, 0, 0xFFFFFFFF);
                    return 1;
                }
                if (camellia_xts(buf + off, buf + off + 32, 32, pt, ct,
                                 sample_size, file_offset)) {
                    fill_result(result, buf + off, 64, ORACLE_ALG_CAMELLIA,
                                ORACLE_MODE_XTS, 0, 0xFFFFFFFF);
                    return 1;
                }
                if (aria_xts(buf + off, buf + off + 32, 32, pt, ct,
                             sample_size, file_offset)) {
                    fill_result(result, buf + off, 64, ORACLE_ALG_ARIA,
                                ORACLE_MODE_XTS, 0, 0xFFFFFFFF);
                    return 1;
                }
            }
        }

        if (is_rc4_sbox(buf + off)) {
            if (off + 16 <= buf_len &&
                try_rc4_at(buf + off, 16, pt, ct, sample_size, file_offset, result))
                return 1;
        } else {
            if (try_rc4_at(buf + off, 16, pt, ct, sample_size, file_offset, result))
                return 1;
        }
    }

    return 0;
}