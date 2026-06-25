#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "sar_capture.h"
#include "aes.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static const uint8_t g_mac_key[SEMANTICS_AR_MAC_SIZE] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};

static sar_gate_map_t g_scratch;

static void make_plaintext(uint8_t *p, uint32_t len)
{
    static const char pat[16] = {
        'D', 'o', 'c', 'u', 'm', 'e', 'n', 't',
        ' ', 't', 'e', 'x', 't', '.', '.', '\n'
    };
    for (uint32_t i = 0; i < len; i++)
        p[i] = (uint8_t)pat[i % 16];
}

static void ecb_encrypt(const uint8_t *key, uint32_t klen,
                        const uint8_t *pt, uint8_t *ct, uint32_t len)
{
    aes_ctx_t c;
    aes_setkey(&c, key, klen);
    for (uint32_t i = 0; i + 16 <= len; i += 16)
        aes_encrypt_block(&c, pt + i, ct + i);
}

static void cbc_encrypt(const uint8_t *key, uint32_t klen, const uint8_t *iv,
                        const uint8_t *pt, uint8_t *ct, uint32_t len)
{
    aes_ctx_t c;
    uint8_t prev[16];
    aes_setkey(&c, key, klen);
    memcpy(prev, iv, 16);
    for (uint32_t i = 0; i + 16 <= len; i += 16) {
        uint8_t blk[16];
        for (uint32_t j = 0; j < 16; j++)
            blk[j] = (uint8_t)(pt[i + j] ^ prev[j]);
        aes_encrypt_block(&c, blk, ct + i);
        memcpy(prev, ct + i, 16);
    }
}

static int key_in_buffer(const void *buf, size_t buf_len, const uint8_t *key, size_t key_len)
{
    const uint8_t *b = (const uint8_t *)buf;
    if (buf_len < key_len)
        return 0;
    for (size_t off = 0; off + key_len <= buf_len; off++)
        if (memcmp(b + off, key, key_len) == 0)
            return 1;
    return 0;
}

static void fill_request(sar_capture_request_t *req,
                         const uint8_t *p, const uint8_t *c, uint32_t len)
{
    req->plaintext = p;
    req->ciphertext = c;
    req->sample_size = len;
    req->file_offset = 0;
    req->candidates = NULL;
    req->candidate_count = 0;
    req->scan_buffer = NULL;
    req->scan_length = 0;
    req->provenance_path = NULL;
    req->provenance_offset = 0;
}

static const uint8_t g_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

static void test_ecb_convict_and_projection(void)
{
    uint8_t p[256], c[256];
    uint8_t cand[1][16];
    sar_capture_request_t req;
    sar_capture_result_t res;
    int nonzero_id = 0;

    make_plaintext(p, 256);
    ecb_encrypt(g_key, 16, p, c, 256);
    memcpy(cand[0], g_key, 16);

    fill_request(&req, p, c, 256);
    req.candidates = (const uint8_t (*)[16])cand;
    req.candidate_count = 1;

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_CONVICTED);
    CHECK(res.outcome == SAR_CAPTURE_CONVICTED);
    CHECK(res.gate.candidate != 0);
    CHECK(res.verdict.convicted == 1);
    CHECK(res.verdict.algorithm == SAR_ALG_AES_128);
    CHECK(res.verdict.mode == SAR_MODE_ECB);
    CHECK(res.verdict.key_length == 16);
    CHECK(memcmp(res.verdict.key, g_key, 16) == 0);

    CHECK(res.notify.header.protocol_version == SEMANTICS_AR_PROTOCOL_VERSION);
    CHECK(res.notify.header.message_type == SEMANTICS_AR_MSG_VERDICT_NOTIFY);
    CHECK(res.notify.header.message_length == (uint32_t)sizeof(res.notify));
    CHECK(res.notify.algorithm == SAR_ALG_AES_128);
    CHECK(res.notify.mode == SAR_MODE_ECB);
    CHECK(memcmp(res.notify.key_id, res.record.key_id, SEMANTICS_AR_KEY_ID_SIZE) == 0);

    for (int i = 0; i < SEMANTICS_AR_KEY_ID_SIZE; i++)
        if (res.record.key_id[i])
            nonzero_id = 1;
    CHECK(nonzero_id);

    {
        uint8_t expect[SEMANTICS_AR_MAC_SIZE];
        sar_sample_tag(p, SEMANTICS_AR_SAMPLE_TAG_MAX, expect);
        CHECK(res.record.sample_length == SEMANTICS_AR_SAMPLE_TAG_MAX);
        CHECK(res.record.sample_offset == 0);
        CHECK(memcmp(res.record.sample_tag, expect, SEMANTICS_AR_MAC_SIZE) == 0);
    }

    CHECK(!key_in_buffer(&res.notify, sizeof(res.notify), g_key, 16));
    CHECK(!key_in_buffer(&res.notify, sizeof(res.notify), g_key, 8));
}

static void test_skip_gate_identity(void)
{
    uint8_t p[256], c[256];
    sar_capture_request_t req;
    sar_capture_result_t res;

    make_plaintext(p, 256);
    memcpy(c, p, 256);

    fill_request(&req, p, c, 256);

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_SKIP_GATE);
    CHECK(res.gate.candidate == 0);
    CHECK(res.verdict.convicted == 0);
    CHECK(res.notify.header.message_type == 0);
}

static void test_no_conviction(void)
{
    uint8_t p[256], c[256];
    uint32_t s = 0x12345678u;
    sar_capture_request_t req;
    sar_capture_result_t res;

    make_plaintext(p, 256);
    for (uint32_t i = 0; i < 256; i++) {
        s = s * 1664525u + 1013904223u;
        c[i] = (uint8_t)(s >> 24);
    }

    fill_request(&req, p, c, 256);

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_NO_CONVICTION);
    CHECK(res.gate.candidate != 0);
    CHECK(res.verdict.convicted == 0);
    CHECK(res.notify.header.message_type == 0);
}

static void test_invalid_inputs(void)
{
    uint8_t p[256], c[256];
    sar_capture_request_t req;
    sar_capture_result_t res;

    make_plaintext(p, 256);
    ecb_encrypt(g_key, 16, p, c, 256);

    fill_request(&req, p, c, 8);
    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_INVALID);

    fill_request(&req, NULL, c, 256);
    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_INVALID);

    fill_request(&req, p, c, 256);
    CHECK(sar_capture_run(&req, NULL, g_mac_key, &res) == SAR_CAPTURE_INVALID);
    CHECK(res.outcome == SAR_CAPTURE_INVALID);
}

static void test_cbc_convict_iv(void)
{
    uint8_t p[256], c[256];
    uint8_t cand[1][16];
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    sar_capture_request_t req;
    sar_capture_result_t res;

    make_plaintext(p, 256);
    cbc_encrypt(g_key, 16, iv, p, c, 256);
    memcpy(cand[0], g_key, 16);

    fill_request(&req, p, c, 256);
    req.candidates = (const uint8_t (*)[16])cand;
    req.candidate_count = 1;

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_CONVICTED);
    CHECK(res.verdict.algorithm == SAR_ALG_AES_128);
    CHECK(res.verdict.mode == SAR_MODE_CBC);
    CHECK(memcmp(res.verdict.key, g_key, 16) == 0);
    CHECK(res.verdict.iv_length == 16);
    CHECK(memcmp(res.verdict.iv, iv, 16) == 0);
    CHECK(memcmp(res.record.iv, iv, 16) == 0);
    CHECK(!key_in_buffer(&res.notify, sizeof(res.notify), g_key, 16));
}

static void test_scan_buffer_primary(void)
{
    uint8_t p[256], c[256];
    uint8_t scan[512];
    sar_capture_request_t req;
    sar_capture_result_t res;

    make_plaintext(p, 256);
    ecb_encrypt(g_key, 16, p, c, 256);
    memset(scan, 0, sizeof(scan));
    memcpy(scan + 64, g_key, 16);

    fill_request(&req, p, c, 256);
    req.scan_buffer = scan;
    req.scan_length = sizeof(scan);

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_CONVICTED);
    CHECK(res.verdict.algorithm == SAR_ALG_AES_128);
    CHECK(res.verdict.mode == SAR_MODE_ECB);
    CHECK(memcmp(res.verdict.key, g_key, 16) == 0);
    CHECK(!key_in_buffer(&res.notify, sizeof(res.notify), g_key, 16));
}

static void test_provenance_propagation(void)
{
    uint8_t p[256], c[256];
    uint8_t cand[1][16];
    static const uint16_t path[8] = { 'C', ':', '\\', 'a', '.', 't', 'x', 't' };
    sar_capture_request_t req;
    sar_capture_result_t res;
    int match = 1;

    make_plaintext(p, 256);
    ecb_encrypt(g_key, 16, p, c, 256);
    memcpy(cand[0], g_key, 16);

    fill_request(&req, p, c, 256);
    req.candidates = (const uint8_t (*)[16])cand;
    req.candidate_count = 1;
    req.provenance_path = path;
    req.provenance_offset = 0x4000;

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res) == SAR_CAPTURE_CONVICTED);
    CHECK(res.record.provenance_offset == 0x4000);
    CHECK(res.notify.provenance_offset == 0x4000);
    for (int i = 0; i < 8; i++) {
        if (res.record.provenance_path[i] != path[i])
            match = 0;
        if (res.notify.provenance_path[i] != path[i])
            match = 0;
    }
    CHECK(match);
}

static void test_key_id_determinism(void)
{
    uint8_t p[256], c[256], c2[256];
    uint8_t cand[1][16];
    uint8_t key2[16];
    sar_capture_request_t req;
    sar_capture_result_t res_a, res_b, res_c;

    make_plaintext(p, 256);
    ecb_encrypt(g_key, 16, p, c, 256);
    memcpy(cand[0], g_key, 16);

    fill_request(&req, p, c, 256);
    req.candidates = (const uint8_t (*)[16])cand;
    req.candidate_count = 1;

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res_a) == SAR_CAPTURE_CONVICTED);
    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res_b) == SAR_CAPTURE_CONVICTED);
    CHECK(memcmp(res_a.record.key_id, res_b.record.key_id, SEMANTICS_AR_KEY_ID_SIZE) == 0);

    memcpy(key2, g_key, 16);
    key2[0] = (uint8_t)(key2[0] ^ 0x01);
    ecb_encrypt(key2, 16, p, c2, 256);
    memcpy(cand[0], key2, 16);
    fill_request(&req, p, c2, 256);
    req.candidates = (const uint8_t (*)[16])cand;
    req.candidate_count = 1;

    CHECK(sar_capture_run(&req, &g_scratch, g_mac_key, &res_c) == SAR_CAPTURE_CONVICTED);
    CHECK(memcmp(res_a.record.key_id, res_c.record.key_id, SEMANTICS_AR_KEY_ID_SIZE) != 0);
}

int main(void)
{
    test_ecb_convict_and_projection();
    test_skip_gate_identity();
    test_no_conviction();
    test_invalid_inputs();
    test_cbc_convict_iv();
    test_scan_buffer_primary();
    test_provenance_propagation();
    test_key_id_determinism();

    printf("test_capture: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
