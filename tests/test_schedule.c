#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "conviction_engine.h"
#include "aes.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static const uint8_t key128[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

static const uint8_t key192[24] = {
    0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52,
    0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5,
    0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b
};

static const uint8_t key256[32] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
    0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
    0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};

static void fill_noise(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(i * 7u + 3u);
}

static void ecb_encrypt(const uint8_t *key, uint32_t klen,
                        const uint8_t *pt, uint8_t *ct, uint32_t len)
{
    aes_ctx_t c;
    aes_setkey(&c, key, klen);
    for (uint32_t i = 0; i + 16 <= len; i += 16)
        aes_encrypt_block(&c, pt + i, ct + i);
}

static int convict_schedule_at_offset(const uint8_t *key, uint32_t klen,
                                      uint32_t sched_len, size_t offset)
{
    uint8_t buf[512];
    aes_ctx_t expand;
    uint8_t pt[32], ct[32];
    sar_engine_input_t in;
    sar_verdict_t v;

    fill_noise(buf, sizeof(buf));
    aes_setkey(&expand, key, klen);
    memcpy(buf + offset, expand.rk, sched_len);

    for (uint32_t i = 0; i < sizeof(pt); i++)
        pt[i] = (uint8_t)(0x41 + (i % 23));
    ecb_encrypt(key, klen, pt, ct, sizeof(pt));

    memset(&in, 0, sizeof(in));
    in.candidates = NULL;
    in.candidate_count = 0;
    in.scan_buffer = buf;
    in.scan_length = sizeof(buf);
    in.plaintext = pt;
    in.ciphertext = ct;
    in.sample_size = sizeof(pt);
    in.file_offset = 0;

    if (!sar_convict(&in, &v))
        return 0;
    if (!v.convicted || v.key_length != klen)
        return 0;
    return memcmp(v.key, key, klen) == 0;
}

static void test_validator(void)
{
    aes_ctx_t c;

    aes_setkey(&c, key128, 16);
    CHECK(aes_schedule_is_valid(c.rk, 16) == 1);
    aes_setkey(&c, key192, 24);
    CHECK(aes_schedule_is_valid(c.rk, 24) == 1);
    aes_setkey(&c, key256, 32);
    CHECK(aes_schedule_is_valid(c.rk, 32) == 1);

    aes_setkey(&c, key256, 32);
    c.rk[100] ^= 0x01;
    CHECK(aes_schedule_is_valid(c.rk, 32) == 0);
    CHECK(aes_schedule_is_valid(c.rk, 0) == 0);
}

static void test_capture_unaligned(void)
{
    CHECK(convict_schedule_at_offset(key128, 16, 176, 7));
    CHECK(convict_schedule_at_offset(key192, 24, 208, 7));
    CHECK(convict_schedule_at_offset(key256, 32, 240, 7));
    CHECK(convict_schedule_at_offset(key128, 16, 176, 0));
    CHECK(convict_schedule_at_offset(key256, 32, 240, 64));
}

static void test_no_false_positive(void)
{
    uint8_t buf[512];
    uint8_t pt[32], ct[32];
    sar_engine_input_t in;
    sar_verdict_t v;

    fill_noise(buf, sizeof(buf));
    for (uint32_t i = 0; i < sizeof(pt); i++)
        pt[i] = (uint8_t)(0x41 + (i % 23));
    ecb_encrypt(key256, 32, pt, ct, sizeof(pt));

    memset(&in, 0, sizeof(in));
    in.scan_buffer = buf;
    in.scan_length = sizeof(buf);
    in.plaintext = pt;
    in.ciphertext = ct;
    in.sample_size = sizeof(pt);

    CHECK(sar_convict(&in, &v) == 0);
}

int main(void)
{
    test_validator();
    test_capture_unaligned();
    test_no_false_positive();

    printf("test_schedule: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
