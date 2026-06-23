#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "conviction_engine.h"
#include "sar_keystore.h"

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
    v->mode_params = 0;
    for (int i = 0; i < 16; i++)
        v->key[i] = (uint8_t)(seed + 3u * (uint8_t)i);
}

int main(void) {
    uint8_t K[SEMANTICS_AR_MAC_SIZE];
    for (int i = 0; i < SEMANTICS_AR_MAC_SIZE; i++) K[i] = (uint8_t)(0x40 + i);

    semantics_ar_keystore_record_t records[8];
    uint64_t count = 0;

    printf("[append + dedup]\n");
    sar_verdict_t va, vb, vc;
    make_verdict(&va, SAR_ALG_AES_128, 0x10);
    make_verdict(&vb, SAR_ALG_AES_256, 0x20);
    make_verdict(&vc, SAR_ALG_SM4, 0x30);

    semantics_ar_keystore_record_t ra, rb, rc;
    sar_keystore_record_init(&ra, K, &va, NULL, 100);
    sar_keystore_record_init(&rb, K, &vb, NULL, 200);
    sar_keystore_record_init(&rc, K, &vc, NULL, 300);

    int r1 = sar_keystore_append(records, &count, 8, &ra);
    int r2 = sar_keystore_append(records, &count, 8, &ra);
    int r3 = sar_keystore_append(records, &count, 8, &rb);
    int r4 = sar_keystore_append(records, &count, 8, &rc);
    CHECK(r1 == 1 && r2 == 0 && r3 == 1 && r4 == 1 && count == 3,
          "append new=1 dup=0; dedup by key_id; count == 3");

    printf("[serialize + verify]\n");
    size_t need = sar_keystore_serialized_size(count);
    uint8_t *buf = (uint8_t *)malloc(need);
    size_t out_len = 0;
    int sret = sar_keystore_serialize(K, records, count, 7, buf, need, &out_len);
    CHECK(sret == SAR_KS_OK && out_len == need, "serialize OK, size matches");

    semantics_ar_keystore_header_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    sar_keystore_anchor_t anchor;
    anchor.present = 1;
    anchor.generation = 7;
    memcpy(anchor.head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);

    uint64_t vcount = 0;
    int vret = sar_keystore_verify(buf, out_len, K, &anchor, &vcount);
    CHECK(vret == SAR_KS_OK && vcount == 3, "verify clean keystore against anchor");

    size_t hdr_sz = sizeof(semantics_ar_keystore_header_t);
    size_t rec_sz = sizeof(semantics_ar_keystore_disk_record_t);

    printf("[tamper detection]\n");
    {
        uint8_t *t = (uint8_t *)malloc(need);
        memcpy(t, buf, need);
        t[hdr_sz + 4] ^= 0x01;
        int rv = sar_keystore_verify(t, need, K, &anchor, NULL);
        CHECK(rv == SAR_KS_RECORD_MAC, "(i) single record field edit detected");
        free(t);
    }
    {
        uint8_t *t = (uint8_t *)malloc(need);
        memcpy(t, buf, need);
        uint8_t tmp[sizeof(semantics_ar_keystore_disk_record_t)];
        memcpy(tmp, t + hdr_sz, rec_sz);
        memcpy(t + hdr_sz, t + hdr_sz + rec_sz, rec_sz);
        memcpy(t + hdr_sz + rec_sz, tmp, rec_sz);
        int rv = sar_keystore_verify(t, need, K, &anchor, NULL);
        CHECK(rv == SAR_KS_RECORD_MAC, "(ii) record reordering detected");
        free(t);
    }
    {
        size_t shortened = need - rec_sz;
        uint8_t *t = (uint8_t *)malloc(shortened);
        memcpy(t, buf, shortened);
        int rv = sar_keystore_verify(t, shortened, K, &anchor, NULL);
        CHECK(rv == SAR_KS_TRUNCATED, "(iii) tail truncation detected");
        free(t);
    }
    {
        semantics_ar_keystore_record_t older[8];
        uint64_t ocount = 0;
        sar_keystore_append(older, &ocount, 8, &ra);
        size_t oneed = sar_keystore_serialized_size(ocount);
        uint8_t *obuf = (uint8_t *)malloc(oneed);
        sar_keystore_serialize(K, older, ocount, 6, obuf, oneed, NULL);
        int rv = sar_keystore_verify(obuf, oneed, K, &anchor, NULL);
        CHECK(rv == SAR_KS_ROLLBACK,
              "(iv) whole-file rollback to older valid chain detected (generation mismatch)");
        uint64_t selfc = 0;
        int self_ok = sar_keystore_verify(obuf, oneed, K, NULL, &selfc);
        CHECK(self_ok == SAR_KS_OK && selfc == 1,
              "older chain is internally valid (rollback only caught via anchor)");
        free(obuf);
    }

    free(buf);
    printf("\nkeystore: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
