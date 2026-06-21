#include "mode_verify.h"
#include "ciphers/cipher_common.h"
#include <string.h>

void gf128_mul_alpha(uint8_t *tweak) {
    uint8_t carry = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t next_carry = (tweak[i] >> 7) & 1;
        tweak[i] = (tweak[i] << 1) | carry;
        carry = next_carry;
    }
    if (carry)
        tweak[0] ^= 0x87;
}

int is_candidate_viable(const uint8_t *data) {
    uint8_t flags[256];
    memset(flags, 0, 256);
    uint32_t unique = 0;
    for (int i = 0; i < 16; i++)
        if (!flags[data[i]]) { flags[data[i]] = 1; unique++; }
    if (unique < 4) return 0;
    uint8_t first = data[0];
    for (int i = 1; i < 16; i++)
        if (data[i] != first) return 1;
    return 0;
}

int is_rc4_sbox(const uint8_t *data) {
    uint8_t seen[256];
    memset(seen, 0, 256);
    for (int i = 0; i < 256; i++) {
        if (seen[data[i]]) return 0;
        seen[data[i]] = 1;
    }
    return 1;
}

static int verify_ecb_h(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint8_t out[16];
    ULONG cb;
    for (uint32_t s = 0; s < 16 && s + 16 <= len; s++)
        if (BCryptEncrypt(hk,(PUCHAR)(pt+s),16,NULL,NULL,0,out,16,&cb,0)==0)
            if (memcmp(out, ct+s, 16) == 0) return 1;
    return 0;
}

static int verify_cbc_h(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint8_t dec[16];
    ULONG cb;
    for (uint32_t s = 0; s < 16 && s + 32 <= len; s++) {
        if (BCryptDecrypt(hk,(PUCHAR)(ct+s+16),16,NULL,NULL,0,dec,16,&cb,0)==0) {
            int match = 1;
            for (int i = 0; i < 16; i++)
                if ((dec[i] ^ ct[s+i]) != pt[s+16+i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int verify_cfb_h(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint8_t enc[16];
    ULONG cb;
    for (uint32_t s = 0; s < 16 && s + 32 <= len; s++) {
        if (BCryptEncrypt(hk,(PUCHAR)(ct+s),16,NULL,NULL,0,enc,16,&cb,0)==0) {
            int match = 1;
            for (int i = 0; i < 16; i++)
                if ((enc[i] ^ ct[s+16+i]) != pt[s+16+i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int verify_ofb_h(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint8_t ks0[16], ks1[16], enc[16];
    ULONG cb;
    for (uint32_t s = 0; s < 16 && s + 32 <= len; s++) {
        for (int i = 0; i < 16; i++) {
            ks0[i] = pt[s+i] ^ ct[s+i];
            ks1[i] = pt[s+16+i] ^ ct[s+16+i];
        }
        if (BCryptEncrypt(hk,ks0,16,NULL,NULL,0,enc,16,&cb,0)==0)
            if (memcmp(enc, ks1, 16) == 0) return 1;
    }
    return 0;
}

static int verify_ctr_h(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    ULONG cb;
    for (uint32_t s = 0; s < 16 && s + 32 <= len; s++) {
        uint8_t ks0[16], ks1[16], c0[16], c1[16];
        for (int i = 0; i < 16; i++) {
            ks0[i] = pt[s+i] ^ ct[s+i];
            ks1[i] = pt[s+16+i] ^ ct[s+16+i];
        }
        if (BCryptDecrypt(hk,ks0,16,NULL,NULL,0,c0,16,&cb,0)!=0) continue;
        if (BCryptDecrypt(hk,ks1,16,NULL,NULL,0,c1,16,&cb,0)!=0) continue;
        if (memcmp(c0, c1, 12) == 0) {
            uint32_t v0 = sar_be32(c0+12), v1 = sar_be32(c1+12);
            if (v1 == v0 + 1) return 1;
        }
        if (memcmp(c0, c1, 8) == 0) {
            uint64_t v0 = sar_le64(c0+8), v1 = sar_le64(c1+8);
            if (v1 == v0 + 1) return 1;
            v0 = sar_be64(c0+8); v1 = sar_be64(c1+8);
            if (v1 == v0 + 1) return 1;
        }
        if (c0[0] == c1[0] && memcmp(c0+1, c1+1, 13) == 0) {
            uint16_t v0 = ((uint16_t)c0[14]<<8)|c0[15];
            uint16_t v1 = ((uint16_t)c1[14]<<8)|c1[15];
            if (v1 == (uint16_t)(v0 + 1)) return 1;
        }
        { uint64_t lo0=sar_le64(c0),lo1=sar_le64(c1),hi0=sar_le64(c0+8),hi1=sar_le64(c1+8);
          if (lo1==lo0+1 && hi0==hi1) return 1; }
        { uint64_t hi0=sar_be64(c0),hi1=sar_be64(c1),lo0=sar_be64(c0+8),lo1=sar_be64(c1+8);
          if (lo1==lo0+1 && hi0==hi1) return 1; }
    }
    return 0;
}

int try_bcrypt_modes(BCRYPT_KEY_HANDLE hk, const uint8_t *pt, const uint8_t *ct,
                     uint32_t len, uint32_t *out_mode) {
    if (verify_ecb_h(hk, pt, ct, len)) { *out_mode = ORACLE_MODE_ECB; return 1; }
    if (len >= 32) {
        if (verify_cbc_h(hk, pt, ct, len)) { *out_mode = ORACLE_MODE_CBC; return 1; }
        if (verify_cfb_h(hk, pt, ct, len)) { *out_mode = ORACLE_MODE_CFB; return 1; }
        if (verify_ofb_h(hk, pt, ct, len)) { *out_mode = ORACLE_MODE_OFB; return 1; }
        if (verify_ctr_h(hk, pt, ct, len)) { *out_mode = ORACLE_MODE_CTR; return 1; }
    }
    return 0;
}

static int verify_xts_tweak_h(BCRYPT_KEY_HANDLE hk1, BCRYPT_KEY_HANDLE hk2,
                              uint64_t sector, uint32_t block_in_sector,
                              const uint8_t *pt, const uint8_t *ct) {
    ULONG cb;
    uint8_t tweak_input[16];
    memset(tweak_input, 0, 16);
    memcpy(tweak_input, &sector, 8);
    uint8_t tweak[16];
    if (BCryptEncrypt(hk2,tweak_input,16,NULL,NULL,0,tweak,16,&cb,0)!=0) return 0;
    for (uint32_t b = 0; b < block_in_sector; b++) gf128_mul_alpha(tweak);
    uint8_t tmp[16];
    for (int i = 0; i < 16; i++) tmp[i] = pt[i] ^ tweak[i];
    uint8_t enc[16];
    if (BCryptEncrypt(hk1,tmp,16,NULL,NULL,0,enc,16,&cb,0)!=0) return 0;
    for (int i = 0; i < 16; i++) enc[i] ^= tweak[i];
    return memcmp(enc, ct, 16) == 0;
}

int verify_bcrypt_xts(BCRYPT_KEY_HANDLE hk1, BCRYPT_KEY_HANDLE hk2,
                      const uint8_t *pt, const uint8_t *ct,
                      uint32_t len, uint64_t file_offset) {
    uint32_t skip = (uint32_t)((16 - (file_offset & 15)) & 15);
    if (skip + 16 > len) return 0;
    uint64_t aligned = file_offset + skip;
    const uint8_t *apt = pt + skip, *act = ct + skip;
    struct { uint64_t s; uint32_t b; } st[4];
    st[0].s=aligned/512;  st[0].b=(uint32_t)((aligned%512)/16);
    st[1].s=aligned/4096; st[1].b=(uint32_t)((aligned%4096)/16);
    st[2].s=0;            st[2].b=0;
    st[3].s=aligned;      st[3].b=0;
    for (int i = 0; i < 4; i++) {
        int dup = 0;
        for (int d = 0; d < i; d++)
            if (st[d].s==st[i].s && st[d].b==st[i].b) { dup=1; break; }
        if (dup) continue;
        if (verify_xts_tweak_h(hk1,hk2,st[i].s,st[i].b,apt,act)) return 1;
    }
    return 0;
}

static int generic_ecb(const block_cipher_ctx_t *bc, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t out[16];
    for (uint32_t s = 0; s < bs && s + bs <= len; s++)
        if (bc->encrypt(bc->ctx, pt+s, out))
            if (memcmp(out, ct+s, bs) == 0) return 1;
    return 0;
}

static int generic_cbc(const block_cipher_ctx_t *bc, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t dec[16];
    for (uint32_t s = 0; s < bs && s + 2*bs <= len; s++) {
        if (bc->decrypt(bc->ctx, ct+s+bs, dec)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((dec[i] ^ ct[s+i]) != pt[s+bs+i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int generic_cfb(const block_cipher_ctx_t *bc, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t enc[16];
    for (uint32_t s = 0; s < bs && s + 2*bs <= len; s++) {
        if (bc->encrypt(bc->ctx, ct+s, enc)) {
            int match = 1;
            for (uint32_t i = 0; i < bs; i++)
                if ((enc[i] ^ ct[s+bs+i]) != pt[s+bs+i]) { match = 0; break; }
            if (match) return 1;
        }
    }
    return 0;
}

static int generic_ofb(const block_cipher_ctx_t *bc, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], enc[16];
    for (uint32_t s = 0; s < bs && s + 2*bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = pt[s+i] ^ ct[s+i];
            ks1[i] = pt[s+bs+i] ^ ct[s+bs+i];
        }
        if (bc->encrypt(bc->ctx, ks0, enc))
            if (memcmp(enc, ks1, bs) == 0) return 1;
    }
    return 0;
}

static int generic_ctr(const block_cipher_ctx_t *bc, const uint8_t *pt, const uint8_t *ct, uint32_t len) {
    uint32_t bs = bc->block_size;
    uint8_t ks0[16], ks1[16], c0[16], c1[16];
    for (uint32_t s = 0; s < bs && s + 2*bs <= len; s++) {
        for (uint32_t i = 0; i < bs; i++) {
            ks0[i] = pt[s+i] ^ ct[s+i];
            ks1[i] = pt[s+bs+i] ^ ct[s+bs+i];
        }
        if (!bc->decrypt(bc->ctx, ks0, c0)) continue;
        if (!bc->decrypt(bc->ctx, ks1, c1)) continue;
        if (bs == 16) {
            if (memcmp(c0,c1,12)==0) {
                uint32_t v0=sar_be32(c0+12), v1=sar_be32(c1+12);
                if (v1==v0+1) return 1;
            }
            if (memcmp(c0,c1,8)==0) {
                uint64_t v0=sar_le64(c0+8), v1=sar_le64(c1+8);
                if (v1==v0+1) return 1;
                v0=sar_be64(c0+8); v1=sar_be64(c1+8);
                if (v1==v0+1) return 1;
            }
            if (c0[0]==c1[0] && memcmp(c0+1,c1+1,13)==0) {
                uint16_t v0=((uint16_t)c0[14]<<8)|c0[15];
                uint16_t v1=((uint16_t)c1[14]<<8)|c1[15];
                if (v1==(uint16_t)(v0+1)) return 1;
            }
            { uint64_t lo0=sar_le64(c0),lo1=sar_le64(c1),hi0=sar_le64(c0+8),hi1=sar_le64(c1+8);
              if (lo1==lo0+1&&hi0==hi1) return 1; }
            { uint64_t hi0=sar_be64(c0),hi1=sar_be64(c1),lo0=sar_be64(c0+8),lo1=sar_be64(c1+8);
              if (lo1==lo0+1&&hi0==hi1) return 1; }
        } else if (bs == 8) {
            if (memcmp(c0,c1,4)==0) {
                uint32_t v0=sar_be32(c0+4), v1=sar_be32(c1+4);
                if (v1==v0+1) return 1;
            }
            { uint64_t v0=sar_le64(c0), v1=sar_le64(c1); if (v1==v0+1) return 1; }
            { uint64_t v0=sar_be64(c0), v1=sar_be64(c1); if (v1==v0+1) return 1; }
        }
    }
    return 0;
}

int generic_try_all_modes(const block_cipher_ctx_t *bc,
                          const uint8_t *pt, const uint8_t *ct,
                          uint32_t len, uint32_t *out_mode) {
    if (generic_ecb(bc, pt, ct, len)) { *out_mode = ORACLE_MODE_ECB; return 1; }
    if (len >= 2 * bc->block_size) {
        if (generic_cbc(bc, pt, ct, len)) { *out_mode = ORACLE_MODE_CBC; return 1; }
        if (generic_cfb(bc, pt, ct, len)) { *out_mode = ORACLE_MODE_CFB; return 1; }
        if (generic_ofb(bc, pt, ct, len)) { *out_mode = ORACLE_MODE_OFB; return 1; }
        if (generic_ctr(bc, pt, ct, len)) { *out_mode = ORACLE_MODE_CTR; return 1; }
    }
    return 0;
}

static int generic_xts_tweak(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                             uint64_t sector, uint32_t block_in_sector,
                             const uint8_t *pt, const uint8_t *ct) {
    uint8_t tweak_in[16], tweak[16];
    memset(tweak_in, 0, 16);
    memcpy(tweak_in, &sector, 8);
    if (!bc2->encrypt(bc2->ctx, tweak_in, tweak)) return 0;
    for (uint32_t b = 0; b < block_in_sector; b++) gf128_mul_alpha(tweak);
    uint8_t tmp[16], enc[16];
    for (int i = 0; i < 16; i++) tmp[i] = pt[i] ^ tweak[i];
    if (!bc1->encrypt(bc1->ctx, tmp, enc)) return 0;
    for (int i = 0; i < 16; i++) enc[i] ^= tweak[i];
    return memcmp(enc, ct, 16) == 0;
}

int generic_verify_xts(const block_cipher_ctx_t *bc1, const block_cipher_ctx_t *bc2,
                       const uint8_t *pt, const uint8_t *ct,
                       uint32_t len, uint64_t file_offset) {
    uint32_t skip = (uint32_t)((16 - (file_offset & 15)) & 15);
    if (skip + 16 > len) return 0;
    uint64_t aligned = file_offset + skip;
    const uint8_t *apt = pt + skip, *act = ct + skip;
    struct { uint64_t s; uint32_t b; } st[4];
    st[0].s=aligned/512;  st[0].b=(uint32_t)((aligned%512)/16);
    st[1].s=aligned/4096; st[1].b=(uint32_t)((aligned%4096)/16);
    st[2].s=0;            st[2].b=0;
    st[3].s=aligned;      st[3].b=0;
    for (int i = 0; i < 4; i++) {
        int dup = 0;
        for (int d = 0; d < i; d++)
            if (st[d].s==st[i].s && st[d].b==st[i].b) { dup=1; break; }
        if (dup) continue;
        if (generic_xts_tweak(bc1,bc2,st[i].s,st[i].b,apt,act)) return 1;
    }
    return 0;
}