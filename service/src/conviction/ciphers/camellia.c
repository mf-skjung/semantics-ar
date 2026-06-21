#include "camellia.h"
#include "cipher_common.h"
#include <string.h>

static const uint32_t SP1[256] = {
    0x70707000u,0x82828200u,0x2C2C2C00u,0xECECEC00u,0xB3B3B300u,0x27272700u,0xC0C0C000u,0xE5E5E500u,
    0xE4E4E400u,0x85858500u,0x57575700u,0x35353500u,0xEAEAEA00u,0x0C0C0C00u,0xAEAEAE00u,0x41414100u,
    0x23232300u,0xEFEFEF00u,0x6B6B6B00u,0x93939300u,0x45454500u,0x19191900u,0xA5A5A500u,0x21212100u,
    0xEDEDED00u,0x0E0E0E00u,0x4F4F4F00u,0x4E4E4E00u,0x1D1D1D00u,0x65656500u,0x92929200u,0xBDBDBD00u,
    0x86868600u,0xB8B8B800u,0xAFAFAF00u,0x8F8F8F00u,0x7C7C7C00u,0xEBEBEB00u,0x1F1F1F00u,0xCECECE00u,
    0x3E3E3E00u,0x30303000u,0xDCDCDC00u,0x5F5F5F00u,0x5E5E5E00u,0xC5C5C500u,0x0B0B0B00u,0x1A1A1A00u,
    0xA6A6A600u,0xE1E1E100u,0x39393900u,0xCACACA00u,0xD5D5D500u,0x47474700u,0x5D5D5D00u,0x3D3D3D00u,
    0xD9D9D900u,0x01010100u,0x5A5A5A00u,0xD6D6D600u,0x51515100u,0x56565600u,0x6C6C6C00u,0x4D4D4D00u,
    0x8B8B8B00u,0x0D0D0D00u,0x9A9A9A00u,0x66666600u,0xFBFBFB00u,0xCCCCCC00u,0xB0B0B000u,0x2D2D2D00u,
    0x74747400u,0x12121200u,0x2B2B2B00u,0x20202000u,0xF0F0F000u,0xB1B1B100u,0x84848400u,0x99999900u,
    0xDFDFDF00u,0x4C4C4C00u,0xCBCBCB00u,0xC2C2C200u,0x34343400u,0x7E7E7E00u,0x76767600u,0x05050500u,
    0x6D6D6D00u,0xB7B7B700u,0xA9A9A900u,0x31313100u,0xD1D1D100u,0x17171700u,0x04040400u,0xD7D7D700u,
    0x14141400u,0x58585800u,0x3A3A3A00u,0x61616100u,0xDEDEDE00u,0x1B1B1B00u,0x11111100u,0x1C1C1C00u,
    0x32323200u,0x0F0F0F00u,0x9C9C9C00u,0x16161600u,0x53535300u,0x18181800u,0xF2F2F200u,0x22222200u,
    0xFEFEFE00u,0x44444400u,0xCFCFCF00u,0xB2B2B200u,0xC3C3C300u,0xB5B5B500u,0x7A7A7A00u,0x91919100u,
    0x24242400u,0x08080800u,0xE8E8E800u,0xA8A8A800u,0x60606000u,0xFCFCFC00u,0x69696900u,0x50505000u,
    0xAAAAAA00u,0xD0D0D000u,0xA0A0A000u,0x7D7D7D00u,0xA1A1A100u,0x89898900u,0x62626200u,0x97979700u,
    0x54545400u,0x5B5B5B00u,0x1E1E1E00u,0x95959500u,0xE0E0E000u,0xFFFFFF00u,0x64646400u,0xD2D2D200u,
    0x10101000u,0xC4C4C400u,0x00000000u,0x48484800u,0xA3A3A300u,0xF7F7F700u,0x75757500u,0xDBDBDB00u,
    0x8A8A8A00u,0x03030300u,0xE6E6E600u,0xDADADA00u,0x09090900u,0x3F3F3F00u,0xDDDDDD00u,0x94949400u,
    0x87878700u,0x5C5C5C00u,0x83838300u,0x02020200u,0xCDCDCD00u,0x4A4A4A00u,0x90909000u,0x33333300u,
    0x73737300u,0x67676700u,0xF6F6F600u,0xF3F3F300u,0x9D9D9D00u,0x7F7F7F00u,0xBFBFBF00u,0xE2E2E200u,
    0x52525200u,0x9B9B9B00u,0xD8D8D800u,0x26262600u,0xC8C8C800u,0x37373700u,0xC6C6C600u,0x3B3B3B00u,
    0x81818100u,0x96969600u,0x6F6F6F00u,0x4B4B4B00u,0x13131300u,0xBEBEBE00u,0x63636300u,0x2E2E2E00u,
    0xE9E9E900u,0x79797900u,0xA7A7A700u,0x8C8C8C00u,0x9F9F9F00u,0x6E6E6E00u,0xBCBCBC00u,0x8E8E8E00u,
    0x29292900u,0xF5F5F500u,0xF9F9F900u,0xB6B6B600u,0x2F2F2F00u,0xFDFDFD00u,0xB4B4B400u,0x59595900u,
    0x78787800u,0x98989800u,0x06060600u,0x6A6A6A00u,0xE7E7E700u,0x46464600u,0x71717100u,0xBABABA00u,
    0xD4D4D400u,0x25252500u,0xABABAB00u,0x42424200u,0x88888800u,0xA2A2A200u,0x8D8D8D00u,0xFAFAFA00u,
    0x72727200u,0x07070700u,0xB9B9B900u,0x55555500u,0xF8F8F800u,0xEEEEEE00u,0xACACAC00u,0x0A0A0A00u,
    0x36363600u,0x49494900u,0x2A2A2A00u,0x68686800u,0x3C3C3C00u,0x38383800u,0xF1F1F100u,0xA4A4A400u,
    0x40404000u,0x28282800u,0xD3D3D300u,0x7B7B7B00u,0xBBBBBB00u,0xC9C9C900u,0x43434300u,0xC1C1C100u,
    0x15151500u,0xE3E3E300u,0xADADAD00u,0xF4F4F400u,0x77777700u,0xC7C7C700u,0x80808000u,0x9E9E9E00u
};

static uint32_t SP2(uint8_t b) { uint32_t v=SP1[b]; return (v<<1)|(v>>31); }
static uint32_t SP3(uint8_t b) { uint32_t v=SP1[b]; return (v<<7)|(v>>25); }
static uint32_t SP4(uint8_t b) { uint32_t v=SP1[b]; return (v>>1)|(v<<31); }

static uint64_t camellia_f(uint64_t x, uint64_t k) {
    x ^= k;
    uint8_t t1=(uint8_t)(x>>56), t2=(uint8_t)(x>>48), t3=(uint8_t)(x>>40), t4=(uint8_t)(x>>32);
    uint8_t t5=(uint8_t)(x>>24), t6=(uint8_t)(x>>16), t7=(uint8_t)(x>>8),  t8=(uint8_t)x;
    uint32_t d1 = SP1[t1] ^ SP2(t2) ^ SP3(t3) ^ SP4(t4);
    uint32_t d2 = SP2(t5) ^ SP3(t6) ^ SP4(t7) ^ SP1[t8];
    d1 ^= d2;
    d2 ^= (d1 >> 8) | (d1 << 24);
    d1 ^= (d2 >> 16) | (d2 << 16);
    return ((uint64_t)d1 << 32) | (uint64_t)d2;
}

static uint64_t camellia_fl(uint64_t x, uint64_t k) {
    uint32_t x1=(uint32_t)(x>>32), x2=(uint32_t)x;
    uint32_t k1=(uint32_t)(k>>32), k2=(uint32_t)k;
    x2 ^= (x1 & k1) << 1 | (x1 & k1) >> 31;
    x1 ^= x2 | k2;
    return ((uint64_t)x1 << 32) | x2;
}

static uint64_t camellia_flinv(uint64_t y, uint64_t k) {
    uint32_t y1=(uint32_t)(y>>32), y2=(uint32_t)y;
    uint32_t k1=(uint32_t)(k>>32), k2=(uint32_t)k;
    y1 ^= y2 | k2;
    y2 ^= (y1 & k1) << 1 | (y1 & k1) >> 31;
    return ((uint64_t)y1 << 32) | y2;
}

static const uint64_t SIGMA1 = 0xA09E667F3BCC908Bull;
static const uint64_t SIGMA2 = 0xB67AE8584CAA73B2ull;
static const uint64_t SIGMA3 = 0xC6EF372FE94F82BEull;
static const uint64_t SIGMA4 = 0x54FF53A5F1D36F1Cull;
static const uint64_t SIGMA5 = 0x10E527FADE682D1Dull;
static const uint64_t SIGMA6 = 0xB05688C2B3E6C1FDull;

#define ROT128(out,in,n) do { \
    int ws=(n)/64, bs=(n)%64; \
    if(bs==0){(out)[0]=(in)[ws%2];(out)[1]=(in)[(ws+1)%2];} \
    else{(out)[0]=((in)[ws%2]<<bs)|((in)[(ws+1)%2]>>(64-bs)); \
         (out)[1]=((in)[(ws+1)%2]<<bs)|((in)[ws%2]>>(64-bs));} \
} while(0)

void camellia_key_schedule(camellia_ctx_t *ctx, const uint8_t *key, uint32_t key_len) {
    uint64_t KL[2]={0}, KR[2]={0}, KA[2], KB[2];
    uint64_t D1, D2;

    KL[0] = sar_load64be(key); KL[1] = sar_load64be(key+8);
    if (key_len >= 24) KR[0] = sar_load64be(key+16);
    if (key_len >= 32) KR[1] = sar_load64be(key+24);
    if (key_len == 24) KR[1] = ~KR[0];

    D1 = KL[0] ^ KR[0]; D2 = KL[1] ^ KR[1];
    D2 ^= camellia_f(D1, SIGMA1);
    D1 ^= camellia_f(D2, SIGMA2);
    D1 ^= KL[0]; D2 ^= KL[1];
    D2 ^= camellia_f(D1, SIGMA3);
    D1 ^= camellia_f(D2, SIGMA4);
    KA[0] = D1; KA[1] = D2;

    D1 = KA[0] ^ KR[0]; D2 = KA[1] ^ KR[1];
    D2 ^= camellia_f(D1, SIGMA5);
    D1 ^= camellia_f(D2, SIGMA6);
    KB[0] = D1; KB[1] = D2;

    memset(ctx, 0, sizeof(*ctx));
    uint64_t t[2];

    if (key_len <= 16) {
        ctx->rounds = 18;
        ctx->kw[0]=KL[0]; ctx->kw[1]=KL[1];
        ctx->k[0]=KA[0]; ctx->k[1]=KA[1];
        ROT128(t,KL,15); ctx->k[2]=t[0]; ctx->k[3]=t[1];
        ROT128(t,KA,15); ctx->k[4]=t[0]; ctx->k[5]=t[1];
        ROT128(t,KA,30); ctx->ke[0]=t[0]; ctx->ke[1]=t[1];
        ROT128(t,KL,45); ctx->k[6]=t[0]; ctx->k[7]=t[1];
        ROT128(t,KA,45); ctx->k[8]=t[0];
        ROT128(t,KL,60); ctx->k[9]=t[1];
        ROT128(t,KA,60); ctx->k[10]=t[0]; ctx->k[11]=t[1];
        ROT128(t,KL,77); ctx->ke[2]=t[0]; ctx->ke[3]=t[1];
        ROT128(t,KL,94); ctx->k[12]=t[0]; ctx->k[13]=t[1];
        ROT128(t,KA,94); ctx->k[14]=t[0]; ctx->k[15]=t[1];
        ROT128(t,KL,111); ctx->k[16]=t[0]; ctx->k[17]=t[1];
        ROT128(t,KA,111); ctx->kw[2]=t[0]; ctx->kw[3]=t[1];
    } else {
        ctx->rounds = 24;
        ctx->kw[0]=KL[0]; ctx->kw[1]=KL[1];
        ctx->k[0]=KB[0]; ctx->k[1]=KB[1];
        ROT128(t,KR,15); ctx->k[2]=t[0]; ctx->k[3]=t[1];
        ROT128(t,KA,15); ctx->k[4]=t[0]; ctx->k[5]=t[1];
        ROT128(t,KR,30); ctx->ke[0]=t[0]; ctx->ke[1]=t[1];
        ROT128(t,KB,30); ctx->k[6]=t[0]; ctx->k[7]=t[1];
        ROT128(t,KL,45); ctx->k[8]=t[0]; ctx->k[9]=t[1];
        ROT128(t,KA,45); ctx->k[10]=t[0]; ctx->k[11]=t[1];
        ROT128(t,KL,60); ctx->ke[2]=t[0]; ctx->ke[3]=t[1];
        ROT128(t,KR,60); ctx->k[12]=t[0]; ctx->k[13]=t[1];
        ROT128(t,KB,60); ctx->k[14]=t[0]; ctx->k[15]=t[1];
        ROT128(t,KL,77); ctx->k[16]=t[0]; ctx->k[17]=t[1];
        ROT128(t,KA,77); ctx->ke[4]=t[0]; ctx->ke[5]=t[1];
        ROT128(t,KR,94); ctx->k[18]=t[0]; ctx->k[19]=t[1];
        ROT128(t,KA,94); ctx->k[20]=t[0]; ctx->k[21]=t[1];
        ROT128(t,KL,111); ctx->k[22]=t[0]; ctx->k[23]=t[1];
        ROT128(t,KB,111); ctx->kw[2]=t[0]; ctx->kw[3]=t[1];
    }
}

int camellia_encrypt_block(void *vctx, const uint8_t *in, uint8_t *out) {
    camellia_ctx_t *ctx = (camellia_ctx_t *)vctx;
    uint64_t D1 = sar_load64be(in) ^ ctx->kw[0];
    uint64_t D2 = sar_load64be(in+8) ^ ctx->kw[1];

    D2 ^= camellia_f(D1, ctx->k[0]); D1 ^= camellia_f(D2, ctx->k[1]);
    D2 ^= camellia_f(D1, ctx->k[2]); D1 ^= camellia_f(D2, ctx->k[3]);
    D2 ^= camellia_f(D1, ctx->k[4]); D1 ^= camellia_f(D2, ctx->k[5]);
    D1 = camellia_fl(D1, ctx->ke[0]); D2 = camellia_flinv(D2, ctx->ke[1]);

    D2 ^= camellia_f(D1, ctx->k[6]); D1 ^= camellia_f(D2, ctx->k[7]);
    D2 ^= camellia_f(D1, ctx->k[8]); D1 ^= camellia_f(D2, ctx->k[9]);
    D2 ^= camellia_f(D1, ctx->k[10]); D1 ^= camellia_f(D2, ctx->k[11]);
    D1 = camellia_fl(D1, ctx->ke[2]); D2 = camellia_flinv(D2, ctx->ke[3]);

    D2 ^= camellia_f(D1, ctx->k[12]); D1 ^= camellia_f(D2, ctx->k[13]);
    D2 ^= camellia_f(D1, ctx->k[14]); D1 ^= camellia_f(D2, ctx->k[15]);
    D2 ^= camellia_f(D1, ctx->k[16]); D1 ^= camellia_f(D2, ctx->k[17]);

    if (ctx->rounds == 24) {
        D1 = camellia_fl(D1, ctx->ke[4]); D2 = camellia_flinv(D2, ctx->ke[5]);
        D2 ^= camellia_f(D1, ctx->k[18]); D1 ^= camellia_f(D2, ctx->k[19]);
        D2 ^= camellia_f(D1, ctx->k[20]); D1 ^= camellia_f(D2, ctx->k[21]);
        D2 ^= camellia_f(D1, ctx->k[22]); D1 ^= camellia_f(D2, ctx->k[23]);
    }

    D2 ^= ctx->kw[2]; D1 ^= ctx->kw[3];
    sar_store64be(out, D2);
    sar_store64be(out+8, D1);
    return 1;
}

int camellia_decrypt_block(void *vctx, const uint8_t *in, uint8_t *out) {
    camellia_ctx_t *ctx = (camellia_ctx_t *)vctx;
    uint64_t D1 = sar_load64be(in) ^ ctx->kw[2];
    uint64_t D2 = sar_load64be(in+8) ^ ctx->kw[3];

    if (ctx->rounds == 24) {
        D2 ^= camellia_f(D1, ctx->k[23]); D1 ^= camellia_f(D2, ctx->k[22]);
        D2 ^= camellia_f(D1, ctx->k[21]); D1 ^= camellia_f(D2, ctx->k[20]);
        D2 ^= camellia_f(D1, ctx->k[19]); D1 ^= camellia_f(D2, ctx->k[18]);
        D1 = camellia_fl(D1, ctx->ke[5]); D2 = camellia_flinv(D2, ctx->ke[4]);
    }

    D2 ^= camellia_f(D1, ctx->k[17]); D1 ^= camellia_f(D2, ctx->k[16]);
    D2 ^= camellia_f(D1, ctx->k[15]); D1 ^= camellia_f(D2, ctx->k[14]);
    D2 ^= camellia_f(D1, ctx->k[13]); D1 ^= camellia_f(D2, ctx->k[12]);
    D1 = camellia_fl(D1, ctx->ke[3]); D2 = camellia_flinv(D2, ctx->ke[2]);

    D2 ^= camellia_f(D1, ctx->k[11]); D1 ^= camellia_f(D2, ctx->k[10]);
    D2 ^= camellia_f(D1, ctx->k[9]); D1 ^= camellia_f(D2, ctx->k[8]);
    D2 ^= camellia_f(D1, ctx->k[7]); D1 ^= camellia_f(D2, ctx->k[6]);
    D1 = camellia_fl(D1, ctx->ke[1]); D2 = camellia_flinv(D2, ctx->ke[0]);

    D2 ^= camellia_f(D1, ctx->k[5]); D1 ^= camellia_f(D2, ctx->k[4]);
    D2 ^= camellia_f(D1, ctx->k[3]); D1 ^= camellia_f(D2, ctx->k[2]);
    D2 ^= camellia_f(D1, ctx->k[1]); D1 ^= camellia_f(D2, ctx->k[0]);

    D2 ^= ctx->kw[0]; D1 ^= ctx->kw[1];
    sar_store64be(out, D2);
    sar_store64be(out+8, D1);
    return 1;
}