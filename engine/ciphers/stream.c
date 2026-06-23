#include "stream.h"
#include "cipher_common.h"
#include <string.h>

const int STREAM_ROUNDS[STREAM_ROUNDS_COUNT] = {8, 12, 20};

static void chacha_qr(uint32_t *s, int a, int b, int c, int d) {
    s[a] += s[b]; s[d] ^= s[a]; s[d] = SAR_ROTL32(s[d], 16);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = SAR_ROTL32(s[b], 12);
    s[a] += s[b]; s[d] ^= s[a]; s[d] = SAR_ROTL32(s[d], 8);
    s[c] += s[d]; s[b] ^= s[c]; s[b] = SAR_ROTL32(s[b], 7);
}

void chacha_block(const uint8_t *key, const uint8_t *nonce,
                  uint32_t counter, int rounds, uint8_t *out) {
    uint32_t s[16], w[16];
    s[0]=0x61707865; s[1]=0x3320646e; s[2]=0x79622d32; s[3]=0x6b206574;
    for (int i = 0; i < 8; i++) s[4+i] = sar_le32(key + i*4);
    s[12] = counter;
    s[13] = sar_le32(nonce); s[14] = sar_le32(nonce+4); s[15] = sar_le32(nonce+8);
    memcpy(w, s, 64);
    int dr = rounds / 2;
    for (int i = 0; i < dr; i++) {
        chacha_qr(w,0,4,8,12); chacha_qr(w,1,5,9,13);
        chacha_qr(w,2,6,10,14); chacha_qr(w,3,7,11,15);
        chacha_qr(w,0,5,10,15); chacha_qr(w,1,6,11,12);
        chacha_qr(w,2,7,8,13); chacha_qr(w,3,4,9,14);
    }
    for (int i = 0; i < 16; i++) {
        w[i] += s[i];
        sar_st_le32(out + i*4, w[i]);
    }
}

void hchacha20(const uint8_t *key, const uint8_t *nonce, uint8_t *subkey) {
    uint32_t s[16];
    s[0]=0x61707865; s[1]=0x3320646e; s[2]=0x79622d32; s[3]=0x6b206574;
    for (int i = 0; i < 8; i++) s[4+i] = sar_le32(key + i*4);
    s[12]=sar_le32(nonce); s[13]=sar_le32(nonce+4); s[14]=sar_le32(nonce+8); s[15]=sar_le32(nonce+12);
    for (int i = 0; i < 10; i++) {
        chacha_qr(s,0,4,8,12); chacha_qr(s,1,5,9,13);
        chacha_qr(s,2,6,10,14); chacha_qr(s,3,7,11,15);
        chacha_qr(s,0,5,10,15); chacha_qr(s,1,6,11,12);
        chacha_qr(s,2,7,8,13); chacha_qr(s,3,4,9,14);
    }
    for (int i = 0; i < 4; i++) sar_st_le32(subkey + i*4, s[i]);
    for (int i = 0; i < 4; i++) sar_st_le32(subkey + 16 + i*4, s[12+i]);
}

static void salsa_qr(uint32_t *s, int a, int b, int c, int d) {
    s[b] ^= SAR_ROTL32(s[a] + s[d],  7);
    s[c] ^= SAR_ROTL32(s[b] + s[a],  9);
    s[d] ^= SAR_ROTL32(s[c] + s[b], 13);
    s[a] ^= SAR_ROTL32(s[d] + s[c], 18);
}

void salsa_block(const uint8_t *key, const uint8_t *nonce,
                 uint64_t counter, int rounds, uint8_t *out) {
    uint32_t s[16], w[16];
    s[0]  = 0x61707865;
    s[1]  = sar_le32(key);      s[2]  = sar_le32(key+4);
    s[3]  = sar_le32(key+8);    s[4]  = sar_le32(key+12);
    s[5]  = 0x3320646e;
    s[6]  = sar_le32(nonce);    s[7]  = sar_le32(nonce+4);
    s[8]  = (uint32_t)(counter & 0xFFFFFFFF);
    s[9]  = (uint32_t)(counter >> 32);
    s[10] = 0x79622d32;
    s[11] = sar_le32(key+16);   s[12] = sar_le32(key+20);
    s[13] = sar_le32(key+24);   s[14] = sar_le32(key+28);
    s[15] = 0x6b206574;
    memcpy(w, s, 64);
    int dr = rounds / 2;
    for (int i = 0; i < dr; i++) {
        salsa_qr(w, 0, 4, 8,12); salsa_qr(w, 5, 9,13, 1);
        salsa_qr(w,10,14, 2, 6); salsa_qr(w,15, 3, 7,11);
        salsa_qr(w, 0, 1, 2, 3); salsa_qr(w, 5, 6, 7, 4);
        salsa_qr(w,10,11, 8, 9); salsa_qr(w,15,12,13,14);
    }
    for (int i = 0; i < 16; i++) {
        w[i] += s[i];
        sar_st_le32(out + i*4, w[i]);
    }
}

void hsalsa20(const uint8_t *key, const uint8_t *nonce, uint8_t *subkey) {
    uint32_t s[16];
    s[0]  = 0x61707865;
    s[1]  = sar_le32(key);      s[2]  = sar_le32(key+4);
    s[3]  = sar_le32(key+8);    s[4]  = sar_le32(key+12);
    s[5]  = 0x3320646e;
    s[6]  = sar_le32(nonce);    s[7]  = sar_le32(nonce+4);
    s[8]  = sar_le32(nonce+8);  s[9]  = sar_le32(nonce+12);
    s[10] = 0x79622d32;
    s[11] = sar_le32(key+16);   s[12] = sar_le32(key+20);
    s[13] = sar_le32(key+24);   s[14] = sar_le32(key+28);
    s[15] = 0x6b206574;
    for (int i = 0; i < 10; i++) {
        salsa_qr(s, 0, 4, 8,12); salsa_qr(s, 5, 9,13, 1);
        salsa_qr(s,10,14, 2, 6); salsa_qr(s,15, 3, 7,11);
        salsa_qr(s, 0, 1, 2, 3); salsa_qr(s, 5, 6, 7, 4);
        salsa_qr(s,10,11, 8, 9); salsa_qr(s,15,12,13,14);
    }
    static const int out_idx[8] = {0,5,10,15,6,7,8,9};
    for (int i = 0; i < 8; i++) sar_st_le32(subkey + i*4, s[out_idx[i]]);
}

int rc4_verify(const uint8_t *key, uint32_t key_len,
               const uint8_t *pt, const uint8_t *ct,
               uint32_t sample_size, uint64_t file_offset) {
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + S[i] + key[i % key_len];
        uint8_t tmp = S[i]; S[i] = S[j]; S[j] = tmp;
    }
    uint8_t ri = 0, rj = 0;
    for (uint64_t n = 0; n < file_offset; n++) {
        ri++; rj += S[ri];
        uint8_t tmp = S[ri]; S[ri] = S[rj]; S[rj] = tmp;
    }
    for (uint32_t k = 0; k < sample_size; k++) {
        ri++; rj += S[ri];
        uint8_t tmp = S[ri]; S[ri] = S[rj]; S[rj] = tmp;
        if ((pt[k] ^ S[(S[ri] + S[rj]) & 0xFF]) != ct[k])
            return 0;
    }
    return 1;
}

int rc4_verify_state(const uint8_t S_in[256], uint8_t si, uint8_t sj,
                     const uint8_t *pt, const uint8_t *ct,
                     uint32_t sample_size) {
    uint8_t S[256];
    for (int i = 0; i < 256; i++) S[i] = S_in[i];
    uint8_t ri = si, rj = sj;
    for (uint32_t k = 0; k < sample_size; k++) {
        ri++; rj += S[ri];
        uint8_t tmp = S[ri]; S[ri] = S[rj]; S[rj] = tmp;
        if ((pt[k] ^ S[(S[ri] + S[rj]) & 0xFF]) != ct[k])
            return 0;
    }
    return 1;
}