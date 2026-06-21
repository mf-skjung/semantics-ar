#include "sosemanuk.h"
#include "cipher_common.h"
#include <string.h>

static void serpent_sbox0(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    t=a^d; a&=d; d=c^t; a^=b&t; b^=c; c^=a|b; b^=d; a^=b;
    t=a; a=c; c=b; b=d; d=t;
    *r0=b^(d|(c)); *r1=c^d; *r2=a^(b&c); *r3=d^(a|*r1);
    *r1^=*r0; *r0^=*r3; *r2=~(*r2);
}

static void serpent_sbox1(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    a=~a; c=~c; t=a&b; a^=c; c|=d; d^=t; b^=c; c^=a; a|=d; c^=d; b^=a; a^=b;
    b|=d; a^=b; c=~c; b^=c;
    *r0=d; *r1=a; *r2=b; *r3=c^(d|a);
}

static void serpent_sbox2(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    t=a; a&=c; a^=d; c^=b; c^=a; d|=t; d^=b; t^=c; b=d; d|=t; d^=a; a&=b; t^=a;
    b^=d; b^=t; t=~t;
    *r0=c; *r1=b; *r2=d; *r3=t;
}

static void serpent_sbox3(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    t=a; a|=d; d&=b; t^=c; d^=a; a|=t; a^=d; c^=t; d^=b; b|=a; b^=t; c=~c;
    c^=b; b^=d;
    *r0=d^(a&b); *r1=a; *r2=c; *r3=b;
}

static void serpent_sbox4(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    b^=d; d=~d; c^=d; d^=a; t=b&d; b^=c; t^=a; a&=b; a^=c;
    c&=d; d^=b; c^=d; b|=t; b^=d; t^=c;
    *r0=b; *r1=t^b; *r2=a; *r3=c;
}

static void serpent_sbox5(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    a^=b; b^=d; d=~d; t=b; b&=a; c^=d; b^=c; c|=t; t^=d; d&=b; d^=a; t^=b; t^=c;
    c^=a; a&=d; c=~c; a^=t;
    *r0=b; *r1=d; *r2=c^(a|t); *r3=t;
}

static void serpent_sbox6(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    c=~c; t=d; d&=a; a^=t; d^=c; c|=t; b^=d; c^=a; a|=b; c^=b; t^=a; a|=d;
    a^=c; t^=d; t^=a; d=~d; c&=t;
    *r0=d^c; *r1=a; *r2=b; *r3=t;
}

static void serpent_sbox7(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    uint32_t a=*r0,b=*r1,c=*r2,d=*r3,t;
    t=b; b|=c; b^=d; t^=c; c^=b; d|=t; d&=a; t^=c; d^=b; b|=t; b^=a; a|=d;
    a^=c; b^=t; c=b^a;
    *r0=d; *r1=t^(b&c); *r2=~b; *r3=c;
}

static void serpent_lt(uint32_t *x0, uint32_t *x1, uint32_t *x2, uint32_t *x3) {
    *x0 = SAR_ROTL32(*x0, 13);
    *x2 = SAR_ROTL32(*x2, 3);
    *x1 ^= *x0 ^ *x2;
    *x3 ^= *x2 ^ (*x0 << 3);
    *x1 = SAR_ROTL32(*x1, 1);
    *x3 = SAR_ROTL32(*x3, 7);
    *x0 ^= *x1 ^ *x3;
    *x2 ^= *x3 ^ (*x1 << 7);
    *x0 = SAR_ROTL32(*x0, 5);
    *x2 = SAR_ROTL32(*x2, 22);
}

typedef void (*sbox_fn)(uint32_t*, uint32_t*, uint32_t*, uint32_t*);
static const sbox_fn SBOXES[8] = {
    serpent_sbox0, serpent_sbox1, serpent_sbox2, serpent_sbox3,
    serpent_sbox4, serpent_sbox5, serpent_sbox6, serpent_sbox7
};

void sosemanuk_key_schedule(sosemanuk_key_ctx_t *kc, const uint8_t *key, uint32_t key_len) {
    uint32_t w[140];
    uint32_t i;
    uint8_t padded[32];

    memset(padded, 0, 32);
    if (key_len > 32) key_len = 32;
    memcpy(padded, key, key_len);
    if (key_len < 32) padded[key_len] = 0x01;

    for (i = 0; i < 8; i++) w[i] = sar_le32(padded + i*4);
    for (i = 8; i < 140; i++)
        w[i] = SAR_ROTL32(w[i-8]^w[i-5]^w[i-3]^w[i-1]^0x9E3779B9^(i-8), 11);

    for (i = 0; i < 25; i++) {
        uint32_t r0 = w[4*i+8], r1 = w[4*i+9], r2 = w[4*i+10], r3 = w[4*i+11];
        SBOXES[(35-i)%8](&r0, &r1, &r2, &r3);
        kc->sk[4*i] = r0; kc->sk[4*i+1] = r1; kc->sk[4*i+2] = r2; kc->sk[4*i+3] = r3;
    }
}

static uint32_t mul_alpha(uint32_t w) {
    return (w << 8) ^ ((w & 0x80000000u) ? 0x00000100u : 0)
                    ^ ((w & 0x40000000u) ? 0x00000080u : 0)
                    ^ ((w & 0x20000000u) ? 0x00000040u : 0)
                    ^ ((w & 0x10000000u) ? 0x00000020u : 0)
                    ^ ((w & 0x08000000u) ? 0x00000010u : 0)
                    ^ ((w & 0x04000000u) ? 0x00000008u : 0)
                    ^ ((w & 0x02000000u) ? 0x00000004u : 0)
                    ^ ((w & 0x01000000u) ? 0x00000002u : 0)
                    ^ ((w >> 24) * 0xA9u);
}

static uint32_t mul_alpha_inv(uint32_t w) {
    return (w >> 8) ^ ((w & 0x00000080u) ? 0x01000000u : 0)
                    ^ ((w & 0x00000040u) ? 0x00800000u : 0)
                    ^ ((w & 0x00000020u) ? 0x00400000u : 0)
                    ^ ((w & 0x00000010u) ? 0x00200000u : 0)
                    ^ ((w & 0x00000008u) ? 0x00100000u : 0)
                    ^ ((w & 0x00000004u) ? 0x00080000u : 0)
                    ^ ((w & 0x00000002u) ? 0x00040000u : 0)
                    ^ ((w & 0x00000001u) ? 0x00020000u : 0)
                    ^ ((w & 0xFF) * 0xFFC10000u);
}

void sosemanuk_iv_setup(sosemanuk_ctx_t *ctx, const sosemanuk_key_ctx_t *kc, const uint8_t *iv) {
    uint32_t r0, r1, r2, r3;
    uint32_t a[12];

    r0 = sar_le32(iv) ^ kc->sk[0];
    r1 = sar_le32(iv+4) ^ kc->sk[1];
    r2 = sar_le32(iv+8) ^ kc->sk[2];
    r3 = sar_le32(iv+12) ^ kc->sk[3];

    serpent_sbox0(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[4]; r1^=kc->sk[5]; r2^=kc->sk[6]; r3^=kc->sk[7];
    serpent_sbox1(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[8]; r1^=kc->sk[9]; r2^=kc->sk[10]; r3^=kc->sk[11];
    serpent_sbox2(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[12]; r1^=kc->sk[13]; r2^=kc->sk[14]; r3^=kc->sk[15];
    serpent_sbox3(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    a[0]=r0; a[1]=r1; a[2]=r2; a[3]=r3;

    r0^=kc->sk[16]; r1^=kc->sk[17]; r2^=kc->sk[18]; r3^=kc->sk[19];
    serpent_sbox4(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[20]; r1^=kc->sk[21]; r2^=kc->sk[22]; r3^=kc->sk[23];
    serpent_sbox5(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[24]; r1^=kc->sk[25]; r2^=kc->sk[26]; r3^=kc->sk[27];
    serpent_sbox6(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[28]; r1^=kc->sk[29]; r2^=kc->sk[30]; r3^=kc->sk[31];
    serpent_sbox7(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    a[4]=r0; a[5]=r1; a[6]=r2; a[7]=r3;

    r0^=kc->sk[32]; r1^=kc->sk[33]; r2^=kc->sk[34]; r3^=kc->sk[35];
    serpent_sbox0(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[36]; r1^=kc->sk[37]; r2^=kc->sk[38]; r3^=kc->sk[39];
    serpent_sbox1(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[40]; r1^=kc->sk[41]; r2^=kc->sk[42]; r3^=kc->sk[43];
    serpent_sbox2(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[44]; r1^=kc->sk[45]; r2^=kc->sk[46]; r3^=kc->sk[47];
    serpent_sbox3(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    a[8]=r0; a[9]=r1; a[10]=r2; a[11]=r3;

    r0^=kc->sk[48]; r1^=kc->sk[49]; r2^=kc->sk[50]; r3^=kc->sk[51];
    serpent_sbox4(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[52]; r1^=kc->sk[53]; r2^=kc->sk[54]; r3^=kc->sk[55];
    serpent_sbox5(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[56]; r1^=kc->sk[57]; r2^=kc->sk[58]; r3^=kc->sk[59];
    serpent_sbox6(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[60]; r1^=kc->sk[61]; r2^=kc->sk[62]; r3^=kc->sk[63];
    serpent_sbox7(&r0,&r1,&r2,&r3);
    r0^=kc->sk[64]; r1^=kc->sk[65]; r2^=kc->sk[66]; r3^=kc->sk[67];

    r0^=kc->sk[68]; r1^=kc->sk[69]; r2^=kc->sk[70]; r3^=kc->sk[71];
    serpent_sbox0(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[72]; r1^=kc->sk[73]; r2^=kc->sk[74]; r3^=kc->sk[75];
    serpent_sbox1(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[76]; r1^=kc->sk[77]; r2^=kc->sk[78]; r3^=kc->sk[79];
    serpent_sbox2(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[80]; r1^=kc->sk[81]; r2^=kc->sk[82]; r3^=kc->sk[83];
    serpent_sbox3(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[84]; r1^=kc->sk[85]; r2^=kc->sk[86]; r3^=kc->sk[87];
    serpent_sbox4(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[88]; r1^=kc->sk[89]; r2^=kc->sk[90]; r3^=kc->sk[91];
    serpent_sbox5(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[92]; r1^=kc->sk[93]; r2^=kc->sk[94]; r3^=kc->sk[95];
    serpent_sbox6(&r0,&r1,&r2,&r3); serpent_lt(&r0,&r1,&r2,&r3);
    r0^=kc->sk[96]; r1^=kc->sk[97]; r2^=kc->sk[98]; r3^=kc->sk[99];
    serpent_sbox7(&r0,&r1,&r2,&r3);

    ctx->s[9] = a[0] ^ r0;
    ctx->s[8] = a[1] ^ r1;
    ctx->s[7] = a[2] ^ r2;
    ctx->s[6] = a[3] ^ r3;
    ctx->s[5] = a[4] ^ r0;
    ctx->s[4] = a[5] ^ r1;
    ctx->s[3] = a[6] ^ r2;
    ctx->s[2] = a[7] ^ r3;
    ctx->s[1] = a[8] ^ r0;
    ctx->s[0] = a[9] ^ r1;
    ctx->r1   = a[10] ^ r2;
    ctx->r2   = a[11] ^ r3;
}

static void sosemanuk_step(sosemanuk_ctx_t *ctx, uint32_t out[4]) {
    uint32_t s0=ctx->s[0], s1=ctx->s[1], s2=ctx->s[2], s3=ctx->s[3];
    uint32_t s4=ctx->s[4], s5=ctx->s[5], s6=ctx->s[6];
    uint32_t s7=ctx->s[7], s8=ctx->s[8], s9=ctx->s[9];
    uint32_t f0, f1, f2, f3, v0, v1, v2, v3;
    uint32_t ns0, ns1, ns2, ns3;
    uint32_t tr1, tr2;

    ns0 = mul_alpha(s0) ^ s3 ^ mul_alpha_inv(s6);
    ns1 = mul_alpha(s1) ^ s4 ^ mul_alpha_inv(s7);
    ns2 = mul_alpha(s2) ^ s5 ^ mul_alpha_inv(s8);
    ns3 = mul_alpha(s3) ^ s6 ^ mul_alpha_inv(s9);

    tr1 = ctx->r1; tr2 = ctx->r2;

    f0 = (s9 + tr1) ^ tr2;
    v0 = s0;
    tr2 = SAR_ROTL32(tr2 * SOSEMANUK_FSM_CONST, 7);
    f1 = (ns0 + tr2) ^ tr1;
    v1 = s1;

    tr1 = (tr1 + ns0);
    tr1 ^= SAR_ROTL32(tr1, 13);
    f2 = (ns1 + tr1) ^ tr2;
    v2 = s2;

    tr2 = (tr2 + ns1);
    tr2 ^= SAR_ROTL32(tr2, 13);
    f3 = (ns2 + tr2) ^ tr1;
    v3 = s3;

    ctx->r1 = (tr1 + ns2);
    ctx->r1 ^= SAR_ROTL32(ctx->r1, 13);
    ctx->r2 = (tr2 + ns3);
    ctx->r2 ^= SAR_ROTL32(ctx->r2, 13);

    {
        uint32_t a=f0,b=f1,c=f2,d=f3,t;
        t=a; a&=c; a^=d; c^=b; c^=a; d|=t; d^=b; t^=c; b=d; d|=t; d^=a; a&=b; t^=a;
        b^=d; b^=t; t=~t;
        out[0] = t ^ v0;
        out[1] = b ^ v1;
        out[2] = d ^ v2;
        out[3] = c ^ v3;
    }

    ctx->s[0] = s4; ctx->s[1] = s5; ctx->s[2] = s6; ctx->s[3] = s7;
    ctx->s[4] = s8; ctx->s[5] = s9; ctx->s[6] = ns0; ctx->s[7] = ns1;
    ctx->s[8] = ns2; ctx->s[9] = ns3;
}

int sosemanuk_verify(const uint8_t *key, uint32_t key_len, const uint8_t *iv,
                     const uint8_t *pt, const uint8_t *ct,
                     uint32_t sample_size, uint64_t file_offset) {
    if (sample_size < 16) return 0;
    sosemanuk_key_ctx_t kc;
    sosemanuk_ctx_t ctx;
    sosemanuk_key_schedule(&kc, key, key_len);
    sosemanuk_iv_setup(&ctx, &kc, iv);

    uint64_t skip_blocks = file_offset / 16;
    uint32_t byte_off = (uint32_t)(file_offset % 16);

    uint32_t dummy[4];
    for (uint64_t s = 0; s < skip_blocks; s++) sosemanuk_step(&ctx, dummy);

    uint8_t ks[16];
    uint32_t ks_pos = 16;
    uint32_t block[4];

    if (byte_off > 0) {
        sosemanuk_step(&ctx, block);
        sar_st_le32(ks, block[0]); sar_st_le32(ks+4, block[1]);
        sar_st_le32(ks+8, block[2]); sar_st_le32(ks+12, block[3]);
        ks_pos = byte_off;
    }

    for (uint32_t i = 0; i < sample_size; i++) {
        if (ks_pos >= 16) {
            sosemanuk_step(&ctx, block);
            sar_st_le32(ks, block[0]); sar_st_le32(ks+4, block[1]);
            sar_st_le32(ks+8, block[2]); sar_st_le32(ks+12, block[3]);
            ks_pos = 0;
        }
        if ((pt[i] ^ ks[ks_pos]) != ct[i]) return 0;
        ks_pos++;
    }
    return 1;
}