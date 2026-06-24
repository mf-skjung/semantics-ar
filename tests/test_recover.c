#include "test_util.h"
#include "sar_recover.h"
#include "sar_recover_file.h"
#include "aes.h"
#include "des.h"
#include "sm4.h"
#include "camellia.h"
#include "aria.h"
#include "seed.h"
#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#define sar_getpid _getpid
#else
#include <unistd.h>
#define sar_getpid getpid
#endif

static void hex(const char *s, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void enc_whole(uint32_t mode, const block_cipher_ctx_t *bc, const uint8_t *iv,
                      const uint8_t *pt, uint8_t *ct, uint32_t len) {
    switch (mode) {
        case SAR_MODE_ECB: ref_ecb(bc, pt, ct, len); break;
        case SAR_MODE_CBC: ref_cbc(bc, iv, pt, ct, len); break;
        case SAR_MODE_CFB: ref_cfb(bc, iv, pt, ct, len); break;
        case SAR_MODE_OFB: ref_ofb(bc, iv, pt, ct, len); break;
        case SAR_MODE_CTR: ref_ctr(bc, iv, pt, ct, len); break;
        default: break;
    }
}

static void roundtrip_block(const char *name, const block_cipher_ctx_t *bc, uint32_t alg,
                            const uint8_t (*cands)[16], size_t ncand,
                            const uint8_t *key, uint32_t klen) {
    enum { L = 256 };
    uint8_t pt[L], ct[L], iv[16], rec[L];
    fill_pattern(pt, L, 0x19);
    fill_pattern(iv, 16, 0x3b);
    static const uint32_t modes[5] = {
        SAR_MODE_ECB, SAR_MODE_CBC, SAR_MODE_CFB, SAR_MODE_OFB, SAR_MODE_CTR
    };
    static const char *mn[5] = { "ECB", "CBC", "CFB", "OFB", "CTR" };
    for (int m = 0; m < 5; m++) {
        enc_whole(modes[m], bc, iv, pt, ct, L);
        sar_engine_input_t in;
        memset(&in, 0, sizeof in);
        in.candidates = cands; in.candidate_count = ncand;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v;
        int r = sar_convict(&in, &v);
        sar_recovery_key_t rk;
        sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        char nm[96];
        snprintf(nm, sizeof nm, "%s-%s convict->persist-IV->recover round-trip", name, mn[m]);
        CHECK(r && v.algorithm == alg && v.mode == modes[m] &&
              v.key_length == klen && memcmp(v.key, key, klen) == 0 &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0, nm);
    }
}

static void block_roundtrips(void) {
    printf("[block-mode A1 round-trips: convict emits IV, recover inverts]\n");
    uint8_t k16[16], k24[24], k32[32];
    fill_pattern(k16, 16, 0xA1);
    fill_pattern(k24, 24, 0xC3);
    fill_pattern(k32, 32, 0xB2);

    { aes_ctx_t c; aes_setkey(&c, k16, 16);
      block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
      uint8_t cd[1][16]; memcpy(cd[0], k16, 16);
      roundtrip_block("AES-128", &bc, SAR_ALG_AES_128, cd, 1, k16, 16); }
    { aes_ctx_t c; aes_setkey(&c, k24, 24);
      block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
      uint8_t cd[2][16]; memcpy(cd[0], k24, 16); memcpy(cd[1], k24 + 16, 8);
      fill_pattern(cd[1] + 8, 8, 0xE5);
      roundtrip_block("AES-192", &bc, SAR_ALG_AES_192, cd, 2, k24, 24); }
    { aes_ctx_t c; aes_setkey(&c, k32, 32);
      block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
      uint8_t cd[2][16]; memcpy(cd[0], k32, 16); memcpy(cd[1], k32 + 16, 16);
      roundtrip_block("AES-256", &bc, SAR_ALG_AES_256, cd, 2, k32, 32); }
    { des3_ctx_t c; des3_setkey(&c, k24);
      block_cipher_ctx_t bc = { des3_encrypt_block, des3_decrypt_block, &c, 8 };
      uint8_t cd[2][16]; memcpy(cd[0], k24, 16); memcpy(cd[1], k24 + 16, 8);
      fill_pattern(cd[1] + 8, 8, 0xD4);
      roundtrip_block("3DES", &bc, SAR_ALG_3DES, cd, 2, k24, 24); }
    { sm4_ctx_t c; sm4_key_schedule(&c, k16);
      block_cipher_ctx_t bc = { sm4_encrypt_block, sm4_decrypt_block, &c, 16 };
      uint8_t cd[1][16]; memcpy(cd[0], k16, 16);
      roundtrip_block("SM4", &bc, SAR_ALG_SM4, cd, 1, k16, 16); }
    { camellia_ctx_t c; camellia_key_schedule(&c, k16, 16);
      block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
      uint8_t cd[1][16]; memcpy(cd[0], k16, 16);
      roundtrip_block("Camellia-128", &bc, SAR_ALG_CAMELLIA, cd, 1, k16, 16); }
    { camellia_ctx_t c; camellia_key_schedule(&c, k32, 32);
      block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
      uint8_t cd[2][16]; memcpy(cd[0], k32, 16); memcpy(cd[1], k32 + 16, 16);
      roundtrip_block("Camellia-256", &bc, SAR_ALG_CAMELLIA, cd, 2, k32, 32); }
    { aria_ctx_t c; aria_key_schedule(&c, k16, 16);
      block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &c, 16 };
      uint8_t cd[1][16]; memcpy(cd[0], k16, 16);
      roundtrip_block("ARIA-128", &bc, SAR_ALG_ARIA, cd, 1, k16, 16); }
    { aria_ctx_t c; aria_key_schedule(&c, k32, 32);
      block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &c, 16 };
      uint8_t cd[2][16]; memcpy(cd[0], k32, 16); memcpy(cd[1], k32 + 16, 16);
      roundtrip_block("ARIA-256", &bc, SAR_ALG_ARIA, cd, 2, k32, 32); }
    { seed_ctx_t c; seed_key_schedule(&c, k16);
      block_cipher_ctx_t bc = { seed_encrypt_block, seed_decrypt_block, &c, 16 };
      uint8_t cd[1][16]; memcpy(cd[0], k16, 16);
      roundtrip_block("SEED", &bc, SAR_ALG_SEED, cd, 1, k16, 16); }
}

static void xts_roundtrip(void) {
    printf("[XTS A1 round-trip]\n");
    enum { L = 512 };
    uint8_t pt[L], ct[L], rec[L];
    fill_pattern(pt, L, 0x24);
    {
        uint8_t k1[16], k2[16];
        fill_pattern(k1, 16, 0x21); fill_pattern(k2, 16, 0x77);
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 16); aes_setkey(&a2, k2, 16);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 0, pt, ct, L);
        uint8_t cd[2][16]; memcpy(cd[0], k1, 16); memcpy(cd[1], k2, 16);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 2;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(r && v.mode == SAR_MODE_XTS && v.mode_params == 512 &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0,
              "AES-128-XTS convict->recover round-trip (data unit 512)");
    }
    {
        uint8_t k1[32], k2[32];
        fill_pattern(k1, 32, 0x31); fill_pattern(k2, 32, 0x88);
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 32); aes_setkey(&a2, k2, 32);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 0, pt, ct, L);
        uint8_t scan[64]; memcpy(scan, k1, 32); memcpy(scan + 32, k2, 32);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.scan_buffer = scan; in.scan_length = 64;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(r && v.mode == SAR_MODE_XTS && v.key_length == 64 &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0,
              "AES-256-XTS convict->recover round-trip (scan buffer)");
    }
}

static void enc_chacha(const uint8_t *key, const uint8_t *nonce12, int origin,
                       const uint8_t *pt, uint8_t *ct, uint32_t len) {
    for (uint32_t o = 0; o < len; o += 64) {
        uint8_t ks[64];
        chacha_block(key, nonce12, (uint32_t)(o / 64) + (uint32_t)origin, 20, ks);
        uint32_t n = len - o; if (n > 64) n = 64;
        for (uint32_t i = 0; i < n; i++) ct[o + i] = (uint8_t)(pt[o + i] ^ ks[i]);
    }
}

static void enc_salsa(const uint8_t *key, const uint8_t *nonce8, int origin,
                      const uint8_t *pt, uint8_t *ct, uint32_t len) {
    for (uint32_t o = 0; o < len; o += 64) {
        uint8_t ks[64];
        salsa_block(key, nonce8, (uint64_t)(o / 64) + (uint64_t)origin, 20, ks);
        uint32_t n = len - o; if (n > 64) n = 64;
        for (uint32_t i = 0; i < n; i++) ct[o + i] = (uint8_t)(pt[o + i] ^ ks[i]);
    }
}

static void stream_roundtrips(void) {
    printf("[stream A1 round-trips: convict persists nonce+origin+rounds]\n");
    enum { L = 256 };
    uint8_t key[32], nonce[24], pt[L], ct[L], rec[L];
    fill_pattern(key, 32, 0x41);
    fill_pattern(nonce, 24, 0x59);
    fill_pattern(pt, L, 0x13);

    for (int origin = 0; origin <= 1; origin++) {
        enc_chacha(key, nonce, origin, pt, ct, L);
        uint8_t cd[3][16];
        memcpy(cd[0], key, 16); memcpy(cd[1], key + 16, 16);
        memcpy(cd[2], nonce, 12); memset(cd[2] + 12, 0, 4);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 3;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        char nm[80];
        snprintf(nm, sizeof nm, "ChaCha20 round-trip (counter origin %d)", origin);
        CHECK(r && v.algorithm == SAR_ALG_CHACHA20 &&
              (uint32_t)(v.mode_params & 0xff) == (uint32_t)origin &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0, nm);
    }

    enc_salsa(key, nonce, 0, pt, ct, L);
    {
        uint8_t cd[3][16];
        memcpy(cd[0], key, 16); memcpy(cd[1], key + 16, 16);
        memcpy(cd[2], nonce, 8); memset(cd[2] + 8, 0, 8);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 3;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(r && v.algorithm == SAR_ALG_SALSA20 && st == SAR_RECOVER_OK &&
              memcmp(rec, pt, L) == 0, "Salsa20 round-trip");
    }
    {
        uint8_t subkey[32], dn[12];
        hchacha20(key, nonce, subkey);
        memset(dn, 0, 4); memcpy(dn + 4, nonce + 16, 8);
        enc_chacha(subkey, dn, 0, pt, ct, L);
        uint8_t cd[4][16];
        memcpy(cd[0], key, 16); memcpy(cd[1], key + 16, 16);
        memcpy(cd[2], nonce, 16); memcpy(cd[3], nonce + 16, 8); memset(cd[3] + 8, 0, 8);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 4;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(r && v.algorithm == SAR_ALG_XCHACHA20 && rk.iv_length == 24 &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0, "XChaCha20 round-trip");
    }
    {
        uint8_t subkey[32], dn[8];
        hsalsa20(key, nonce, subkey);
        memcpy(dn, nonce + 16, 8);
        enc_salsa(subkey, dn, 0, pt, ct, L);
        uint8_t cd[4][16];
        memcpy(cd[0], key, 16); memcpy(cd[1], key + 16, 16);
        memcpy(cd[2], nonce, 16); memcpy(cd[3], nonce + 16, 8); memset(cd[3] + 8, 0, 8);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 4;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; int r = sar_convict(&in, &v);
        sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
        memset(rec, 0, L);
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(r && v.algorithm == SAR_ALG_XSALSA20 && rk.iv_length == 24 &&
              st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0, "XSalsa20 round-trip");
    }
}

static void rc4_roundtrip(void) {
    printf("[RC4 short-key round-trip and state-only decline]\n");
    enum { L = 256 };
    uint8_t key[16], pt[L], ct[L], rec[L];
    fill_pattern(key, 16, 0x61);
    fill_pattern(pt, L, 0x14);
    {
        uint8_t S[256]; for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
        uint8_t j = 0;
        for (int i = 0; i < 256; i++) { j = (uint8_t)(j + S[i] + key[i % 16]);
            uint8_t t = S[i]; S[i] = S[j]; S[j] = t; }
        uint8_t ri = 0, rj = 0;
        for (uint32_t k = 0; k < L; k++) { ri++; rj = (uint8_t)(rj + S[ri]);
            uint8_t t = S[ri]; S[ri] = S[rj]; S[rj] = t;
            ct[k] = (uint8_t)(pt[k] ^ S[(uint8_t)(S[ri] + S[rj])]); }
    }
    uint8_t cd[1][16]; memcpy(cd[0], key, 16);
    sar_engine_input_t in; memset(&in, 0, sizeof in);
    in.candidates = cd; in.candidate_count = 1;
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
    sar_verdict_t v; int r = sar_convict(&in, &v);
    sar_recovery_key_t rk; sar_recovery_key_from_verdict(&v, &rk);
    memset(rec, 0, L);
    sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
    CHECK(r && v.algorithm == SAR_ALG_RC4 && v.key_length == 16 &&
          st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0, "RC4 short-key round-trip");

    sar_recovery_key_t rks; memset(&rks, 0, sizeof rks);
    rks.algorithm = SAR_ALG_RC4; rks.mode = SAR_MODE_STREAM; rks.key_length = 0;
    sar_recover_status_t sst = sar_recover_buffer(&rks, NULL, ct, rec, L);
    CHECK(sst == SAR_RECOVER_DECLINED_STATE_ONLY,
          "RC4 S-box-state-only declines (not whole-file recoverable)");
}

static void iv_recovery(void) {
    printf("[IV recovery: A1 persisted value, A2 located-on-disk]\n");
    enum { L = 256 };
    uint8_t key[16], iv[16], pt[L], ct[L];
    fill_pattern(key, 16, 0x71);
    fill_pattern(iv, 16, 0x4d);
    fill_pattern(pt, L, 0x22);
    aes_ctx_t c; aes_setkey(&c, key, 16);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
    ref_cbc(&bc, iv, pt, ct, L);

    {
        uint8_t cd[1][16]; memcpy(cd[0], key, 16);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        in.candidates = cd; in.candidate_count = 1;
        in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; sar_convict(&in, &v);
        CHECK(v.mode == SAR_MODE_CBC && v.iv_length == 16 &&
              memcmp(v.iv, iv, 16) == 0, "A1: CBC IV recovered at conviction equals true IV");
    }
    {
        uint8_t file[16 + L];
        memcpy(file, iv, 16);
        memcpy(file + 16, ct, L);
        sar_recovery_key_t rk; memset(&rk, 0, sizeof rk);
        rk.algorithm = SAR_ALG_AES_128; rk.mode = SAR_MODE_CBC;
        rk.key_length = 16; memcpy(rk.key, key, 16);
        sar_recovery_sample_t s = { 0, pt, ct, 64 };
        int found = sar_recover_locate_iv(&rk, file, sizeof file, &s);
        CHECK(found && rk.iv_length == 16 && memcmp(rk.iv, iv, 16) == 0,
              "A2: CBC IV located in file head equals true IV (relation-verified)");
    }
    {
        uint8_t nonce[16];
        fill_pattern(nonce, 16, 0x00);
        memset(nonce + 8, 0, 8);
        ref_ctr(&bc, nonce, pt, ct, L);
        uint8_t file[16 + L];
        memcpy(file, nonce, 16);
        memcpy(file + 16, ct, L);
        sar_recovery_key_t rk; memset(&rk, 0, sizeof rk);
        rk.algorithm = SAR_ALG_AES_128; rk.mode = SAR_MODE_CTR;
        rk.key_length = 16; memcpy(rk.key, key, 16);
        sar_recovery_sample_t s = { 0, pt, ct, 64 };
        int found = sar_recover_locate_iv(&rk, file, sizeof file, &s);
        uint8_t rec[L];
        sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
        CHECK(found && rk.ctr_layout_tag != 0 && st == SAR_RECOVER_OK &&
              memcmp(rec, pt, L) == 0,
              "A2: CTR origin+layout located on disk, recovers file");
    }
}

static void block0_limit(void) {
    printf("[CBC block-0 confirmed limit: no IV -> recover blocks >=1]\n");
    enum { L = 256 };
    uint8_t key[16], iv[16], pt[L], ct[L], rec[L];
    fill_pattern(key, 16, 0x71);
    fill_pattern(iv, 16, 0x4d);
    fill_pattern(pt, L, 0x22);
    aes_ctx_t c; aes_setkey(&c, key, 16);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
    ref_cbc(&bc, iv, pt, ct, L);

    sar_recovery_key_t rk; memset(&rk, 0, sizeof rk);
    rk.algorithm = SAR_ALG_AES_128; rk.mode = SAR_MODE_CBC;
    rk.key_length = 16; memcpy(rk.key, key, 16);
    rk.iv_length = 0;
    memset(rec, 0, L);
    sar_recover_status_t st = sar_recover_buffer(&rk, NULL, ct, rec, L);
    CHECK(st == SAR_RECOVER_OK && memcmp(rec, ct, 16) == 0 &&
          memcmp(rec + 16, pt + 16, L - 16) == 0,
          "block 0 left as on-disk ciphertext, blocks >=1 recovered");
}

static void slice_aware(void) {
    printf("[slice-aware recovery: decrypt only encrypted ranges]\n");
    enum { L = 1024 };
    uint8_t key[16], nonce[16], pt[L], ct[L], rec[L];
    fill_pattern(key, 16, 0x91);
    fill_pattern(nonce, 16, 0x00); memset(nonce + 8, 0, 8);
    fill_pattern(pt, L, 0x55);
    aes_ctx_t c; aes_setkey(&c, key, 16);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };

    uint8_t ks_full[L];
    {
        uint8_t zero[L]; memset(zero, 0, L);
        ref_ctr(&bc, nonce, zero, ks_full, L);
    }
    sar_recovery_key_t rk;
    {
        uint8_t s_ct[64];
        for (int i = 0; i < 64; i++) s_ct[i] = (uint8_t)(pt[i] ^ ks_full[i]);
        sar_engine_input_t in; memset(&in, 0, sizeof in);
        uint8_t cd[1][16]; memcpy(cd[0], key, 16);
        in.candidates = cd; in.candidate_count = 1;
        in.plaintext = pt; in.ciphertext = s_ct; in.sample_size = 64; in.file_offset = 0;
        sar_verdict_t v; sar_convict(&in, &v);
        sar_recovery_key_from_verdict(&v, &rk);
    }

    sar_geometry_t geom; memset(&geom, 0, sizeof geom);
    geom.mode = SAR_GEOM_STRIDE; geom.chunk_bytes = 64; geom.stride_bytes = 256;
    geom.counter_policy = SAR_CTR_CONTINUOUS;
    sar_range_t ranges[SAR_MAX_RANGES]; uint32_t nr = 0;
    sar_geometry_expand(&geom, L, ranges, SAR_MAX_RANGES, &nr);

    memcpy(ct, pt, L);
    for (uint32_t i = 0; i < nr; i++)
        for (uint64_t o = ranges[i].offset; o < ranges[i].offset + ranges[i].length; o++)
            ct[o] = (uint8_t)(pt[o] ^ ks_full[o]);

    memset(rec, 0, L);
    sar_recover_status_t st = sar_recover_buffer(&rk, &geom, ct, rec, L);
    CHECK(st == SAR_RECOVER_OK && memcmp(rec, pt, L) == 0,
          "STRIDE/continuous: encrypted ranges recovered, skipped ranges intact");

    uint8_t recfull[L];
    sar_recover_status_t stf = sar_recover_buffer(&rk, NULL, ct, recfull, L);
    int skipped_corrupted = (stf == SAR_RECOVER_OK) && (memcmp(recfull + 128, pt + 128, 64) != 0);
    CHECK(skipped_corrupted,
          "whole-file decrypt corrupts a skipped region (shows geometry is load-bearing)");

    {
        uint8_t hpt[L], hct[L], hrec[L];
        fill_pattern(hpt, L, 0x66);
        uint8_t ckey[32], cnonce[24];
        fill_pattern(ckey, 32, 0x33); fill_pattern(cnonce, 12, 0x44);
        uint64_t head = 384;
        memcpy(hct, hpt, L);
        for (uint32_t o = 0; o < head; o += 64) {
            uint8_t ksb[64];
            chacha_block(ckey, cnonce, o / 64, 20, ksb);
            for (uint32_t i = 0; i < 64 && o + i < head; i++)
                hct[o + i] = (uint8_t)(hpt[o + i] ^ ksb[i]);
        }
        sar_recovery_key_t crk; memset(&crk, 0, sizeof crk);
        crk.algorithm = SAR_ALG_CHACHA20; crk.mode = SAR_MODE_STREAM;
        crk.key_length = 32; memcpy(crk.key, ckey, 32);
        memcpy(crk.iv, cnonce, 12); crk.iv_length = 12;
        crk.mode_params = ((uint64_t)20 << 8);
        sar_geometry_t hg; memset(&hg, 0, sizeof hg);
        hg.mode = SAR_GEOM_HEAD; hg.head_bytes = head; hg.counter_policy = SAR_CTR_CONTINUOUS;
        memset(hrec, 0, L);
        sar_recover_status_t hst = sar_recover_buffer(&crk, &hg, hct, hrec, L);
        CHECK(hst == SAR_RECOVER_OK && memcmp(hrec, hpt, L) == 0,
              "HEAD/ChaCha: head-encrypted file recovered, tail intact");
    }

    {
        uint8_t rpt[L], rct[L], rrec[L];
        fill_pattern(rpt, L, 0x77);
        memcpy(rct, rpt, L);
        sar_geometry_t rg; memset(&rg, 0, sizeof rg);
        rg.mode = SAR_GEOM_STRIDE; rg.chunk_bytes = 128; rg.stride_bytes = 512;
        rg.counter_policy = SAR_CTR_RESET_PER_CHUNK;
        sar_range_t rr[SAR_MAX_RANGES]; uint32_t rnr = 0;
        sar_geometry_expand(&rg, L, rr, SAR_MAX_RANGES, &rnr);
        for (uint32_t i = 0; i < rnr; i++) {
            uint8_t ksc[128];
            uint8_t zero[128]; memset(zero, 0, 128);
            ref_ctr(&bc, nonce, zero, ksc, (uint32_t)rr[i].length);
            for (uint64_t o = 0; o < rr[i].length; o++)
                rct[rr[i].offset + o] = (uint8_t)(rpt[rr[i].offset + o] ^ ksc[o]);
        }
        sar_recover_status_t rst = sar_recover_buffer(&rk, &rg, rct, rrec, L);
        CHECK(rst == SAR_RECOVER_OK && memcmp(rrec, rpt, L) == 0,
              "STRIDE/reset-per-chunk: counter restarts each chunk, recovered");
    }
}

static int read_file_bytes(const char *path, uint8_t *buf, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

static void no_clobber(void) {
    printf("[no-clobber writeback: wrong key declines, correct key recovers]\n");
    enum { L = 256 };
    uint8_t keyA[16], keyB[16], iv[16], pt[L], ct[L], disk[L];
    fill_pattern(keyA, 16, 0x81);
    fill_pattern(keyB, 16, 0x37);
    fill_pattern(iv, 16, 0x5c);
    fill_pattern(pt, L, 0x16);
    aes_ctx_t c; aes_setkey(&c, keyA, 16);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
    ref_cbc(&bc, iv, pt, ct, L);

    char path[128];
    snprintf(path, sizeof path, "sar_noclobber_%d.bin", (int)sar_getpid());
    FILE *f = fopen(path, "wb");
    fwrite(ct, 1, L, f);
    fclose(f);

    sar_recovery_sample_t sample = { 0, pt, ct, 64 };

    sar_recovery_key_t rkB; memset(&rkB, 0, sizeof rkB);
    rkB.algorithm = SAR_ALG_AES_128; rkB.mode = SAR_MODE_CBC;
    rkB.key_length = 16; memcpy(rkB.key, keyB, 16);
    memcpy(rkB.iv, iv, 16); rkB.iv_length = 16;
    sar_recover_file_result_t res;
    sar_recover_status_t st = sar_recover_file(path, &rkB, NULL, &sample, &res);
    int unchanged = (read_file_bytes(path, disk, L) == 0) && (memcmp(disk, ct, L) == 0);
    CHECK(st == SAR_RECOVER_DECLINED_MISMATCH && unchanged,
          "wrong key: forward relation fails, file left byte-for-byte intact");

    sar_recovery_key_t rkA; memset(&rkA, 0, sizeof rkA);
    rkA.algorithm = SAR_ALG_AES_128; rkA.mode = SAR_MODE_CBC;
    rkA.key_length = 16; memcpy(rkA.key, keyA, 16);
    memcpy(rkA.iv, iv, 16); rkA.iv_length = 16;
    st = sar_recover_file(path, &rkA, NULL, &sample, &res);
    int recovered = (read_file_bytes(path, disk, L) == 0) && (memcmp(disk, pt, L) == 0);
    CHECK(st == SAR_RECOVER_OK && recovered,
          "correct key: forward relation holds, file recovered on disk");

    remove(path);
}

static void atomic_replace(void) {
    printf("[atomic replace: kernel-written temp swapped onto target by path only]\n");
    char target[128], repl[160];
    snprintf(target, sizeof target, "sar_repl_%d.bin", (int)sar_getpid());
    snprintf(repl, sizeof repl, "%s.sarrectmp", target);

    static const char oldb[32] = "OLD-CONTENT-0123456789ABCDEF012";
    static const char newb[32] = "NEW-RECOVERED-PLAINTEXT-9876543";

    FILE *f = fopen(target, "wb"); fwrite(oldb, 1, 32, f); fclose(f);
    f = fopen(repl, "wb"); fwrite(newb, 1, 32, f); fclose(f);

    int rc = sar_atomic_replace_file(target, repl);

    uint8_t disk[32];
    int swapped = (read_file_bytes(target, disk, 32) == 0) && (memcmp(disk, newb, 32) == 0);
    FILE *rf = fopen(repl, "rb");
    int repl_consumed = (rf == NULL);
    if (rf) fclose(rf);

    CHECK(rc == 0 && swapped && repl_consumed,
          "replacement swapped onto target name, replacement file consumed");

    remove(target);
    remove(repl);
}

static void absorbed_kats(void) {
    printf("[absorbed published decrypt-direction KATs (RFC / OpenSSL cross-checked)]\n");
    uint8_t key[32], pt[16], ct[16], out[16];

    hex("0123456789abcdeffedcba9876543210", pt, 16);
    hex("0123456789abcdeffedcba98765432100011223344556677", key, 24);
    hex("b4993401b3e996f84ee5cee7d79b09b9", ct, 16);
    { camellia_ctx_t cm; camellia_key_schedule(&cm, key, 24);
      camellia_encrypt_block(&cm, pt, out);
      CHECK(memcmp(out, ct, 16) == 0, "Camellia-192 RFC 3713 encrypt");
      camellia_decrypt_block(&cm, ct, out);
      CHECK(memcmp(out, pt, 16) == 0, "Camellia-192 decrypt round-trip"); }

    hex("0123456789abcdeffedcba987654321000112233445566778899aabbccddeeff", key, 32);
    hex("9acc237dff16d76c20ef7c919e3a7509", ct, 16);
    { camellia_ctx_t cm; camellia_key_schedule(&cm, key, 32);
      camellia_encrypt_block(&cm, pt, out);
      CHECK(memcmp(out, ct, 16) == 0, "Camellia-256 RFC 3713 encrypt");
      camellia_decrypt_block(&cm, ct, out);
      CHECK(memcmp(out, pt, 16) == 0, "Camellia-256 decrypt round-trip"); }

    hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    hex("00112233445566778899aabbccddeeff", pt, 16);
    hex("f92bd7c79fb72e2f2b8f80c1972d24fc", ct, 16);
    { aria_ctx_t ar; aria_key_schedule(&ar, key, 32);
      aria_encrypt_block(&ar, pt, out);
      CHECK(memcmp(out, ct, 16) == 0, "ARIA-256 RFC 5794 encrypt");
      aria_decrypt_block(&ar, ct, out);
      CHECK(memcmp(out, pt, 16) == 0, "ARIA-256 decrypt round-trip"); }

    {
        uint8_t ckey[32], nonce[12], ksref[64], ks[64];
        for (int i = 0; i < 32; i++) ckey[i] = (uint8_t)i;
        hex("000000090000004a00000000", nonce, 12);
        hex("10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
            "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e", ksref, 64);
        chacha_block(ckey, nonce, 1, 20, ks);
        CHECK(memcmp(ks, ksref, 64) == 0, "ChaCha20 RFC 8439 2.3.2 keystream block");
    }

    {
        uint8_t data[64], ctx[64];
        for (int i = 0; i < 64; i++) data[i] = (uint8_t)i;
        uint8_t k1[16], k2[16], ref[64];
        hex("2122232425262728292a2b2c2d2e2f30", k1, 16);
        hex("7778797a7b7c7d7e7f80818283848586", k2, 16);
        hex("4cf7f7d02d11b5727703e16f496a93ffb3afa3f41bfe395353ee6dcde3827f09"
            "996bd924fc9d301256f6f19e46adb32224e1fe67ef52c5f42641711f84307517", ref, 64);
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 16); aes_setkey(&a2, k2, 16);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 1, data, ctx, 64);
        CHECK(memcmp(ctx, ref, 64) == 0, "AES-128-XTS unit 1 matches OpenSSL");
    }
    {
        uint8_t data[64], ctx[64];
        for (int i = 0; i < 64; i++) data[i] = (uint8_t)i;
        uint8_t k1[32], k2[32], ref[64];
        hex("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40", k1, 32);
        hex("7778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f90919293949596", k2, 32);
        hex("ff1ed4ccf0b03a71b077d2ce6ccd1713f9a91710747f89db1802e1eed286b530"
            "1ac839fd0304f9750bb45761c0ed54defececd8dcc05d18c2f7a87eb3c1d206a", ref, 64);
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 32); aes_setkey(&a2, k2, 32);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 1, data, ctx, 64);
        CHECK(memcmp(ctx, ref, 64) == 0, "AES-256-XTS unit 1 matches OpenSSL");
    }
}

int main(void) {
    block_roundtrips();
    xts_roundtrip();
    stream_roundtrips();
    rc4_roundtrip();
    iv_recovery();
    block0_limit();
    slice_aware();
    no_clobber();
    atomic_replace();
    absorbed_kats();
    printf("\nrecover: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
