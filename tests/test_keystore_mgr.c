#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "conviction_engine.h"
#include "sar_keystore.h"
#include "sar_keystore_mgr.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }       \
    else { g_fail++; printf("  FAIL %s\n", (name)); }            \
} while (0)

static void make_verdict(sar_verdict_t *v, uint32_t alg, uint8_t seed) {
    memset(v, 0, sizeof(*v));
    v->convicted = 1;
    v->algorithm = alg;
    v->mode = SAR_MODE_CBC;
    v->key_length = 16;
    for (int i = 0; i < 16; i++)
        v->key[i] = (uint8_t)(seed + 3u * (uint8_t)i);
    v->iv_length = 16;
    for (int i = 0; i < 16; i++)
        v->iv[i] = (uint8_t)(seed ^ (uint8_t)(i * 5u));
}

static uint8_t *persist_set(const uint8_t *K,
                            const semantics_ar_keystore_record_t *records,
                            uint64_t count, uint64_t prev_gen,
                            size_t *out_len, sar_keystore_anchor_t *anchor) {
    size_t cap = sar_keystore_serialized_size(count);
    uint8_t *buf = (uint8_t *)malloc(cap);
    sar_ksm_status_t s = sar_ksm_persist(K, records, count, prev_gen,
                                         buf, cap, out_len, anchor);
    if (s != SAR_KSM_OK) {
        free(buf);
        return NULL;
    }
    return buf;
}

int main(void) {
    uint8_t K[SEMANTICS_AR_MAC_SIZE];
    for (int i = 0; i < SEMANTICS_AR_MAC_SIZE; i++) K[i] = (uint8_t)(0x40 + i);
    uint8_t Kwrong[SEMANTICS_AR_MAC_SIZE];
    for (int i = 0; i < SEMANTICS_AR_MAC_SIZE; i++) Kwrong[i] = (uint8_t)(0x99 ^ i);

    semantics_ar_keystore_record_t recs[8];
    uint64_t count = 0;
    sar_verdict_t va, vb, vc, vd;
    make_verdict(&va, SAR_ALG_AES_128, 0x10);
    make_verdict(&vb, SAR_ALG_AES_256, 0x20);
    make_verdict(&vc, SAR_ALG_SM4, 0x30);
    make_verdict(&vd, SAR_ALG_ARIA, 0x40);
    semantics_ar_keystore_record_t ra, rb, rc, rd;
    sar_keystore_record_init(&ra, K, &va, NULL, 100, 0, NULL, 0, 0, 0, 0);
    sar_keystore_record_init(&rb, K, &vb, NULL, 200, 0, NULL, 0, 0, 0, 0);
    sar_keystore_record_init(&rc, K, &vc, NULL, 300, 0, NULL, 0, 0, 0, 0);
    sar_keystore_record_init(&rd, K, &vd, NULL, 400, 0, NULL, 0, 0, 0, 0);
    sar_keystore_append(recs, &count, 8, &ra);
    sar_keystore_append(recs, &count, 8, &rb);
    sar_keystore_append(recs, &count, 8, &rc);

    printf("[persist gen1 (3 records)]\n");
    size_t len1 = 0;
    sar_keystore_anchor_t anc1;
    uint8_t *buf1 = persist_set(K, recs, count, 0, &len1, &anc1);
    CHECK(buf1 != NULL && anc1.present && anc1.generation == 1,
          "persist bumps generation 0 -> 1, anchor present");

    printf("[append 4th, persist gen2 (4 records)]\n");
    sar_keystore_append(recs, &count, 8, &rd);
    size_t len2 = 0;
    sar_keystore_anchor_t anc2;
    uint8_t *buf2 = persist_set(K, recs, count, 1, &len2, &anc2);
    CHECK(buf2 != NULL && anc2.generation == 2 && count == 4,
          "persist bumps generation 1 -> 2 at 4 records");

    printf("[round-trip load gen2 with matching anchor]\n");
    semantics_ar_keystore_record_t out[8];
    uint64_t ocount = 0;
    sar_keystore_anchor_t aout;
    sar_ksm_load_result_t res;
    sar_ksm_status_t s = sar_ksm_load(buf2, len2, K, &anc2, out, 8,
                                      &ocount, &aout, &res);
    CHECK(s == SAR_KSM_OK && ocount == 4 && res.anchor_advanced == 0,
          "load OK, count restored, anchor not advanced (steady state)");
    CHECK(memcmp(out, recs, 4 * sizeof(recs[0])) == 0,
          "restored records byte-identical to originals");
    CHECK(aout.generation == 2 &&
          memcmp(aout.head_mac, anc2.head_mac, SEMANTICS_AR_MAC_SIZE) == 0,
          "returned anchor equals stored anchor");

    printf("[crash window: file gen2 ahead of anchor gen1]\n");
    s = sar_ksm_load(buf2, len2, K, &anc1, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_OK && ocount == 4 && res.anchor_advanced == 1 &&
          aout.generation == 2,
          "file one generation ahead of anchor accepted, anchor advances");

    printf("[rollback: older file gen1 against anchor gen2]\n");
    s = sar_ksm_load(buf1, len1, K, &anc2, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_ROLLBACK, "older-generation file rejected as rollback");

    printf("[same-generation tamper]\n");
    sar_keystore_anchor_t forged = anc2;
    forged.head_mac[0] ^= 0xFF;
    s = sar_ksm_load(buf2, len2, K, &forged, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_ROLLBACK,
          "same generation but head_mac mismatch rejected");

    printf("[erasure: empty file against non-zero anchor]\n");
    s = sar_ksm_load(NULL, 0, K, &anc2, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_ROLLBACK, "missing store with live anchor = erasure");

    printf("[fresh start: empty file, no anchor]\n");
    s = sar_ksm_load(NULL, 0, K, NULL, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_EMPTY && ocount == 0, "no file + no anchor = empty");

    printf("[first establish: valid file, no anchor]\n");
    s = sar_ksm_load(buf2, len2, K, NULL, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_OK && res.anchor_advanced == 1 && aout.generation == 2,
          "valid file with absent anchor accepted and anchor established");

    printf("[corrupt: flipped record byte]\n");
    buf2[sizeof(semantics_ar_keystore_header_t) + 4] ^= 0x01;
    s = sar_ksm_load(buf2, len2, K, &anc2, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_CORRUPT, "internal MAC failure surfaces as corrupt");
    buf2[sizeof(semantics_ar_keystore_header_t) + 4] ^= 0x01;

    printf("[wrong mac key]\n");
    s = sar_ksm_load(buf2, len2, Kwrong, &anc2, out, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_CORRUPT, "wrong mac key fails verification");

    printf("[overflow: capacity below record count]\n");
    s = sar_ksm_load(buf2, len2, K, &anc2, out, 3, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_OVERFLOW, "record count above capacity rejected");

    printf("[invalid args]\n");
    s = sar_ksm_load(buf2, len2, K, &anc2, NULL, 8, &ocount, &aout, &res);
    CHECK(s == SAR_KSM_INVALID_ARG, "null records_out rejected");

    free(buf1);
    free(buf2);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
