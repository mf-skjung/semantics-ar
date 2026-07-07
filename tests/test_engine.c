#include "test_util.h"
#include "aes.h"
#include "des.h"
#include "sm4.h"
#include "camellia.h"
#include "aria.h"
#include "seed.h"
#include "stream.h"

static void hex(const char *s, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static int convict_check(sar_engine_input_t *in, uint32_t alg, uint32_t mode,
                         const uint8_t *key, uint32_t klen, sar_verdict_t *v) {
    int r = sar_convict(in, v);
    return r && v->algorithm == alg && v->mode == mode &&
           v->key_length == klen && memcmp(v->key, key, klen) == 0;
}

static void primitive_kats(void) {
    printf("[primitive known-answer vectors]\n");
    uint8_t key[32], pt[16], ct[16], out[16];

    hex("000102030405060708090a0b0c0d0e0f", key, 16);
    hex("00112233445566778899aabbccddeeff", pt, 16);
    hex("69c4e0d86a7b0430d8cdb78070b4c55a", ct, 16);
    aes_ctx_t a; aes_setkey(&a, key, 16); aes_encrypt_block(&a, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "AES-128 FIPS-197 encrypt");
    aes_decrypt_block(&a, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "AES-128 FIPS-197 decrypt");

    hex("000102030405060708090a0b0c0d0e0f1011121314151617", key, 24);
    hex("dda97ca4864cdfe06eaf70a0ec0d7191", ct, 16);
    aes_setkey(&a, key, 24); aes_encrypt_block(&a, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "AES-192 FIPS-197 encrypt");

    hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    hex("8ea2b7ca516745bfeafc49904b496089", ct, 16);
    aes_setkey(&a, key, 32); aes_encrypt_block(&a, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "AES-256 FIPS-197 encrypt");
    aes_decrypt_block(&a, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "AES-256 FIPS-197 decrypt");

    uint8_t k24[24], pt8[8], ct8[8], out8[8];
    hex("0123456789abcdef0123456789abcdef0123456789abcdef", k24, 24);
    hex("4e6f772069732074", pt8, 8);
    hex("3fa40e8a984d4815", ct8, 8);
    des3_ctx_t d; des3_setkey(&d, k24);
    des3_encrypt_block(&d, pt8, out8);
    CHECK(memcmp(out8, ct8, 8) == 0, "3DES(K,K,K) == DES known vector");
    des3_decrypt_block(&d, ct8, out8);
    CHECK(memcmp(out8, pt8, 8) == 0, "3DES decrypt round-trip");

    hex("0123456789abcdeffedcba9876543210", key, 16);
    hex("0123456789abcdeffedcba9876543210", pt, 16);
    hex("681edf34d206965e86b3e94f536e4246", ct, 16);
    sm4_ctx_t sm; sm4_key_schedule(&sm, key);
    sm4_encrypt_block(&sm, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "SM4 GB/T 32907 encrypt");
    sm4_decrypt_block(&sm, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "SM4 decrypt round-trip");

    hex("67673138549669730857065648eabe43", ct, 16);
    camellia_ctx_t cam; camellia_key_schedule(&cam, key, 16);
    camellia_encrypt_block(&cam, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "Camellia-128 RFC 3713 encrypt");
    camellia_decrypt_block(&cam, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "Camellia-128 decrypt round-trip");

    hex("000102030405060708090a0b0c0d0e0f", key, 16);
    hex("00112233445566778899aabbccddeeff", pt, 16);
    hex("d718fbd6ab644c739da95f3be6451778", ct, 16);
    aria_ctx_t ar; aria_key_schedule(&ar, key, 16);
    aria_encrypt_block(&ar, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "ARIA-128 RFC 5794 encrypt");
    aria_decrypt_block(&ar, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "ARIA-128 decrypt round-trip");

    hex("00000000000000000000000000000000", key, 16);
    hex("000102030405060708090a0b0c0d0e0f", pt, 16);
    hex("5ebac6e0054e166819aff1cc6d346cdb", ct, 16);
    seed_ctx_t sd; seed_key_schedule(&sd, key);
    seed_encrypt_block(&sd, pt, out);
    CHECK(memcmp(out, ct, 16) == 0, "SEED RFC 4269 encrypt");
    seed_decrypt_block(&sd, ct, out);
    CHECK(memcmp(out, pt, 16) == 0, "SEED decrypt round-trip");
}

static void block_modes_suite(const char *name, block_cipher_ctx_t *bc, uint32_t alg,
                              const uint8_t (*cands)[16], size_t ncand,
                              const uint8_t *key, uint32_t klen) {
    uint8_t pt[64], ct[64], iv[16], nonce[16];
    fill_pattern(pt, 64, 0x11);
    fill_pattern(iv, 16, 0x33);
    fill_pattern(nonce, 16, 0x55);
    sar_engine_input_t in;
    sar_verdict_t v;
    char nm[80];
    memset(&in, 0, sizeof in);
    in.candidates = cands; in.candidate_count = ncand;
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;

    ref_ecb(bc, pt, ct, 64);
    snprintf(nm, sizeof nm, "%s ECB forward conviction", name);
    CHECK(convict_check(&in, alg, SAR_MODE_ECB, key, klen, &v), nm);

    ref_cbc(bc, iv, pt, ct, 64);
    snprintf(nm, sizeof nm, "%s CBC forward conviction", name);
    CHECK(convict_check(&in, alg, SAR_MODE_CBC, key, klen, &v), nm);

    ref_cfb(bc, iv, pt, ct, 64);
    snprintf(nm, sizeof nm, "%s CFB forward conviction", name);
    CHECK(convict_check(&in, alg, SAR_MODE_CFB, key, klen, &v), nm);

    ref_ofb(bc, iv, pt, ct, 64);
    snprintf(nm, sizeof nm, "%s OFB forward conviction", name);
    CHECK(convict_check(&in, alg, SAR_MODE_OFB, key, klen, &v), nm);

    ref_ctr(bc, nonce, pt, ct, 64);
    snprintf(nm, sizeof nm, "%s CTR forward conviction", name);
    CHECK(convict_check(&in, alg, SAR_MODE_CTR, key, klen, &v), nm);
}

static void block_ciphers(void) {
    printf("[block cipher / mode forward conviction]\n");
    uint8_t key16[16], key32[32], key24[24];
    fill_pattern(key16, 16, 0xA1);
    fill_pattern(key32, 32, 0xB2);
    fill_pattern(key24, 24, 0xC3);

    {
        aes_ctx_t c; aes_setkey(&c, key16, 16);
        block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
        uint8_t cands[1][16]; memcpy(cands[0], key16, 16);
        block_modes_suite("AES-128", &bc, SAR_ALG_AES_128, cands, 1, key16, 16);
    }
    {
        sm4_ctx_t c; sm4_key_schedule(&c, key16);
        block_cipher_ctx_t bc = { sm4_encrypt_block, sm4_decrypt_block, &c, 16 };
        uint8_t cands[1][16]; memcpy(cands[0], key16, 16);
        block_modes_suite("SM4", &bc, SAR_ALG_SM4, cands, 1, key16, 16);
    }
    {
        camellia_ctx_t c; camellia_key_schedule(&c, key16, 16);
        block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
        uint8_t cands[1][16]; memcpy(cands[0], key16, 16);
        block_modes_suite("Camellia-128", &bc, SAR_ALG_CAMELLIA, cands, 1, key16, 16);
    }
    {
        aria_ctx_t c; aria_key_schedule(&c, key16, 16);
        block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &c, 16 };
        uint8_t cands[1][16]; memcpy(cands[0], key16, 16);
        block_modes_suite("ARIA-128", &bc, SAR_ALG_ARIA, cands, 1, key16, 16);
    }
    {
        seed_ctx_t c; seed_key_schedule(&c, key16);
        block_cipher_ctx_t bc = { seed_encrypt_block, seed_decrypt_block, &c, 16 };
        uint8_t cands[1][16]; memcpy(cands[0], key16, 16);
        block_modes_suite("SEED", &bc, SAR_ALG_SEED, cands, 1, key16, 16);
    }
    {
        camellia_ctx_t c; camellia_key_schedule(&c, key32, 32);
        block_cipher_ctx_t bc = { camellia_encrypt_block, camellia_decrypt_block, &c, 16 };
        uint8_t cands[2][16]; memcpy(cands[0], key32, 16); memcpy(cands[1], key32 + 16, 16);
        block_modes_suite("Camellia-256", &bc, SAR_ALG_CAMELLIA, cands, 2, key32, 32);
    }
    {
        aria_ctx_t c; aria_key_schedule(&c, key32, 32);
        block_cipher_ctx_t bc = { aria_encrypt_block, aria_decrypt_block, &c, 16 };
        uint8_t cands[2][16]; memcpy(cands[0], key32, 16); memcpy(cands[1], key32 + 16, 16);
        block_modes_suite("ARIA-256", &bc, SAR_ALG_ARIA, cands, 2, key32, 32);
    }
    {
        des3_ctx_t c; des3_setkey(&c, key24);
        block_cipher_ctx_t bc = { des3_encrypt_block, des3_decrypt_block, &c, 8 };
        uint8_t cands[2][16];
        memcpy(cands[0], key24, 16);
        memcpy(cands[1], key24 + 16, 8);
        fill_pattern(cands[1] + 8, 8, 0xD4);
        block_modes_suite("3DES", &bc, SAR_ALG_3DES, cands, 2, key24, 24);
    }
    {
        aes_ctx_t c; aes_setkey(&c, key24, 24);
        block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
        uint8_t cands[2][16];
        memcpy(cands[0], key24, 16);
        memcpy(cands[1], key24 + 16, 8);
        fill_pattern(cands[1] + 8, 8, 0xE5);
        block_modes_suite("AES-192", &bc, SAR_ALG_AES_192, cands, 2, key24, 24);
    }
}

static void xts_suite(void) {
    printf("[XTS forward conviction]\n");
    uint8_t k1[32], k2[32], pt[64], ct[64];
    fill_pattern(pt, 64, 0x12);
    sar_engine_input_t in; sar_verdict_t v;
    memset(&in, 0, sizeof in);
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 512;

    fill_pattern(k1, 16, 0x21); fill_pattern(k2, 16, 0x77);
    {
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 16); aes_setkey(&a2, k2, 16);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 1, pt, ct, 64);
        uint8_t cands[2][16]; memcpy(cands[0], k1, 16); memcpy(cands[1], k2, 16);
        uint8_t expk[32]; memcpy(expk, k1, 16); memcpy(expk + 16, k2, 16);
        in.candidates = cands; in.candidate_count = 2; in.scan_buffer = NULL; in.scan_length = 0;
        int ok = convict_check(&in, SAR_ALG_AES_128, SAR_MODE_XTS, expk, 32, &v);
        CHECK(ok && v.mode_params == 512, "AES-128-XTS forward conviction (data unit 512)");
    }
    fill_pattern(k1, 32, 0x31); fill_pattern(k2, 32, 0x88);
    {
        aes_ctx_t a1, a2; aes_setkey(&a1, k1, 32); aes_setkey(&a2, k2, 32);
        block_cipher_ctx_t bc1 = { aes_encrypt_block, aes_decrypt_block, &a1, 16 };
        block_cipher_ctx_t bc2 = { aes_encrypt_block, aes_decrypt_block, &a2, 16 };
        ref_xts(&bc1, &bc2, 1, pt, ct, 64);
        uint8_t scan[64]; memcpy(scan, k1, 32); memcpy(scan + 32, k2, 32);
        uint8_t expk[64]; memcpy(expk, k1, 32); memcpy(expk + 32, k2, 32);
        in.candidates = NULL; in.candidate_count = 0;
        in.scan_buffer = scan; in.scan_length = 64;
        int ok = convict_check(&in, SAR_ALG_AES_256, SAR_MODE_XTS, expk, 64, &v);
        CHECK(ok && v.mode_params == 512, "AES-256-XTS forward conviction (scan buffer)");
    }
}

static void stream_suite(void) {
    printf("[stream cipher forward conviction]\n");
    uint8_t key[32], nonce[24], pt[64], ct[64], ks[64];
    fill_pattern(key, 32, 0x41);
    fill_pattern(nonce, 24, 0x59);
    fill_pattern(pt, 64, 0x13);
    sar_engine_input_t in; sar_verdict_t v;
    memset(&in, 0, sizeof in);
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;

    chacha_block(key, nonce, 0, 20, ks);
    for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
    {
        uint8_t cands[3][16];
        memcpy(cands[0], key, 16); memcpy(cands[1], key + 16, 16);
        memcpy(cands[2], nonce, 12); memset(cands[2] + 12, 0, 4);
        in.candidates = cands; in.candidate_count = 3;
        CHECK(convict_check(&in, SAR_ALG_CHACHA20, SAR_MODE_STREAM, key, 32, &v),
              "ChaCha20 forward conviction");
    }
    salsa_block(key, nonce, 0, 20, ks);
    for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
    {
        uint8_t cands[3][16];
        memcpy(cands[0], key, 16); memcpy(cands[1], key + 16, 16);
        memcpy(cands[2], nonce, 8); memset(cands[2] + 8, 0, 8);
        in.candidates = cands; in.candidate_count = 3;
        CHECK(convict_check(&in, SAR_ALG_SALSA20, SAR_MODE_STREAM, key, 32, &v),
              "Salsa20 forward conviction");
    }
    salsa_block(key, nonce, 0, 12, ks);
    for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
    {
        uint8_t cands[3][16];
        memcpy(cands[0], key, 16); memcpy(cands[1], key + 16, 16);
        memcpy(cands[2], nonce, 8); memset(cands[2] + 8, 0, 8);
        in.candidates = cands; in.candidate_count = 3;
        CHECK(convict_check(&in, SAR_ALG_SALSA20, SAR_MODE_STREAM, key, 32, &v),
              "Salsa20/12 reduced-round forward conviction");
    }
    {
        uint8_t subkey[32];
        hchacha20(key, nonce, subkey);
        uint8_t dn[12]; memset(dn, 0, 4); memcpy(dn + 4, nonce + 16, 8);
        chacha_block(subkey, dn, 0, 20, ks);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
        uint8_t cands[4][16];
        memcpy(cands[0], key, 16); memcpy(cands[1], key + 16, 16);
        memcpy(cands[2], nonce, 16); memcpy(cands[3], nonce + 16, 8);
        memset(cands[3] + 8, 0, 8);
        in.candidates = cands; in.candidate_count = 4;
        CHECK(convict_check(&in, SAR_ALG_XCHACHA20, SAR_MODE_STREAM, key, 32, &v),
              "XChaCha20 forward conviction");
    }
    {
        uint8_t subkey[32];
        hsalsa20(key, nonce, subkey);
        uint8_t dn[8]; memcpy(dn, nonce + 16, 8);
        salsa_block(subkey, dn, 0, 20, ks);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
        uint8_t cands[4][16];
        memcpy(cands[0], key, 16); memcpy(cands[1], key + 16, 16);
        memcpy(cands[2], nonce, 16); memcpy(cands[3], nonce + 16, 8);
        memset(cands[3] + 8, 0, 8);
        in.candidates = cands; in.candidate_count = 4;
        CHECK(convict_check(&in, SAR_ALG_XSALSA20, SAR_MODE_STREAM, key, 32, &v),
              "XSalsa20 forward conviction");
    }
    {
        static const uint8_t sigma[16] = {
            0x65,0x78,0x70,0x61,0x6e,0x64,0x20,0x33,
            0x32,0x2d,0x62,0x79,0x74,0x65,0x20,0x6b
        };
        uint8_t nonce12[12]; fill_pattern(nonce12, 12, 0x37);
        uint8_t nonce8[8]; fill_pattern(nonce8, 8, 0x2b);
        uint8_t scan[512];

        chacha_block(key, nonce12, 0, 20, ks);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
        memset(scan, 0xa7, sizeof scan);
        memcpy(scan + 128, sigma, 16);
        memcpy(scan + 128 + 16, key, 32);
        memset(scan + 128 + 48, 0, 4);
        memcpy(scan + 128 + 52, nonce12, 12);
        in.candidates = NULL; in.candidate_count = 0;
        in.scan_buffer = scan; in.scan_length = sizeof scan;
        CHECK(convict_check(&in, SAR_ALG_CHACHA20, SAR_MODE_STREAM, key, 32, &v),
              "ChaCha20 located via sigma-scan (candidates=NULL)");

        salsa_block(key, nonce8, 0, 20, ks);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
        memset(scan, 0xa7, sizeof scan);
        memcpy(scan + 200, sigma, 4);
        memcpy(scan + 200 + 4, key, 16);
        memcpy(scan + 200 + 20, sigma + 4, 4);
        memcpy(scan + 200 + 24, nonce8, 8);
        memset(scan + 200 + 32, 0, 8);
        memcpy(scan + 200 + 40, sigma + 8, 4);
        memcpy(scan + 200 + 44, key + 16, 16);
        memcpy(scan + 200 + 60, sigma + 12, 4);
        in.scan_buffer = scan; in.scan_length = sizeof scan;
        CHECK(convict_check(&in, SAR_ALG_SALSA20, SAR_MODE_STREAM, key, 32, &v),
              "Salsa20 located via sigma-scan (candidates=NULL)");

        uint8_t subkey[32];
        hchacha20(key, nonce, subkey);
        uint8_t dn[12]; memset(dn, 0, 4); memcpy(dn + 4, nonce + 16, 8);
        chacha_block(subkey, dn, 0, 20, ks);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ ks[i]);
        memset(scan, 0xa7, sizeof scan);
        memcpy(scan + 64, sigma, 16);
        memcpy(scan + 64 + 16, subkey, 32);
        memset(scan + 64 + 48, 0, 4);
        memcpy(scan + 64 + 52, dn, 12);
        in.scan_buffer = scan; in.scan_length = sizeof scan;
        CHECK(convict_check(&in, SAR_ALG_CHACHA20, SAR_MODE_STREAM, subkey, 32, &v),
              "XChaCha20 subkey-state located via sigma-scan as CHACHA20");
    }
}

static void rc4_ksa(const uint8_t *key, uint32_t kl, uint8_t S[256], uint8_t *pi, uint8_t *pj) {
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = (uint8_t)(j + S[i] + key[(uint32_t)i % kl]);
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
    *pi = 0; *pj = 0;
}

static uint8_t rc4_prga(uint8_t S[256], uint8_t *pi, uint8_t *pj) {
    *pi = (uint8_t)(*pi + 1);
    *pj = (uint8_t)(*pj + S[*pi]);
    uint8_t t = S[*pi]; S[*pi] = S[*pj]; S[*pj] = t;
    return S[(uint8_t)(S[*pi] + S[*pj])];
}

static void rc4_suite(void) {
    printf("[RC4 forward conviction]\n");
    uint8_t key[16], pt[64], ct[64];
    fill_pattern(key, 16, 0x61);
    fill_pattern(pt, 64, 0x14);
    sar_engine_input_t in; sar_verdict_t v;
    memset(&in, 0, sizeof in);
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;

    {
        uint8_t S[256], si, sj;
        rc4_ksa(key, 16, S, &si, &sj);
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ rc4_prga(S, &si, &sj));
        uint8_t cands[1][16]; memcpy(cands[0], key, 16);
        in.candidates = cands; in.candidate_count = 1; in.scan_buffer = NULL; in.scan_length = 0;
        int r = sar_convict(&in, &v);
        CHECK(r && v.algorithm == SAR_ALG_RC4 && v.key_length == 16 &&
              memcmp(v.key, key, 16) == 0, "RC4 short-key forward conviction");
    }
    {
        uint8_t S[256], si, sj;
        rc4_ksa(key, 16, S, &si, &sj);
        for (int n = 0; n < 333; n++) rc4_prga(S, &si, &sj);
        uint8_t Scap[256], cap_i, cap_j;
        memcpy(Scap, S, 256); cap_i = si; cap_j = sj;
        for (int i = 0; i < 64; i++) ct[i] = (uint8_t)(pt[i] ^ rc4_prga(S, &si, &sj));
        uint8_t scan[258];
        memcpy(scan, Scap, 256);
        scan[256] = cap_i; scan[257] = cap_j;
        in.candidates = NULL; in.candidate_count = 0;
        in.scan_buffer = scan; in.scan_length = 258;
        int r = sar_convict(&in, &v);
        CHECK(r && v.algorithm == SAR_ALG_RC4 && v.mode == SAR_MODE_STREAM,
              "RC4 from captured S-box state (S,i,j)");
    }
}

static void inversion_suite(void) {
    printf("[round-key inversion]\n");
    uint8_t master16[16], master32[32], pt[64], ct[64];
    fill_pattern(master16, 16, 0x71);
    fill_pattern(master32, 32, 0x83);
    fill_pattern(pt, 64, 0x15);
    sar_engine_input_t in; sar_verdict_t v;
    memset(&in, 0, sizeof in);
    in.plaintext = pt; in.ciphertext = ct; in.sample_size = 64; in.file_offset = 0;

    {
        aes_ctx_t c; aes_setkey(&c, master16, 16);
        block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
        ref_ecb(&bc, pt, ct, 64);
        uint8_t roundkey[16];
        memcpy(roundkey, c.rk + 10 * 16, 16);
        uint8_t cands[1][16]; memcpy(cands[0], roundkey, 16);
        in.candidates = cands; in.candidate_count = 1;
        int ok = convict_check(&in, SAR_ALG_AES_128, SAR_MODE_ECB, master16, 16, &v);
        CHECK(memcmp(roundkey, master16, 16) != 0 && ok,
              "AES-128 master recovered from round key 10");
    }
    {
        aes_ctx_t c; aes_setkey(&c, master32, 32);
        block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
        ref_ecb(&bc, pt, ct, 64);
        uint8_t window[32];
        memcpy(window, c.rk + 32, 32);
        uint8_t cands[2][16];
        memcpy(cands[0], window, 16); memcpy(cands[1], window + 16, 16);
        in.candidates = cands; in.candidate_count = 2;
        int ok = convict_check(&in, SAR_ALG_AES_256, SAR_MODE_ECB, master32, 32, &v);
        CHECK(memcmp(window, master32, 32) != 0 && ok,
              "AES-256 master recovered from adjacent round-key pair");
    }
}

static void negative_suite(void) {
    printf("[negative cases]\n");
    uint8_t key[16], pt[64], ct[64], wrong[16];
    fill_pattern(key, 16, 0x91);
    fill_pattern(wrong, 16, 0x37);
    fill_pattern(pt, 64, 0x16);
    sar_engine_input_t in; sar_verdict_t v;
    memset(&in, 0, sizeof in);
    in.sample_size = 64; in.file_offset = 0;

    aes_ctx_t c; aes_setkey(&c, key, 16);
    block_cipher_ctx_t bc = { aes_encrypt_block, aes_decrypt_block, &c, 16 };
    ref_ecb(&bc, pt, ct, 64);

    {
        uint8_t cands[1][16]; memcpy(cands[0], key, 16);
        in.candidates = cands; in.candidate_count = 1;
        in.plaintext = ct; in.ciphertext = pt;
        CHECK(sar_convict(&in, &v) == 0, "reverse-direction sample does NOT convict");
    }
    {
        uint8_t cands[1][16]; memcpy(cands[0], wrong, 16);
        in.candidates = cands; in.candidate_count = 1;
        in.plaintext = pt; in.ciphertext = ct;
        CHECK(sar_convict(&in, &v) == 0, "wrong candidate does NOT convict");
    }
    {
        uint8_t benign[64];
        fill_pattern(benign, 64, 0xCD);
        uint8_t cands[1][16]; memcpy(cands[0], key, 16);
        in.candidates = cands; in.candidate_count = 1;
        in.plaintext = pt; in.ciphertext = benign;
        CHECK(sar_convict(&in, &v) == 0, "benign write (key present, no forward match) does NOT convict");
    }
}

int main(void) {
    primitive_kats();
    block_ciphers();
    xts_suite();
    stream_suite();
    rc4_suite();
    inversion_suite();
    negative_suite();
    printf("\nengine: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
