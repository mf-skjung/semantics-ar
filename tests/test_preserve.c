#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "sar_preserve.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }       \
    else { g_fail++; printf("  FAIL %s\n", (name)); }            \
} while (0)

static void path_of(uint16_t out[SEMANTICS_AR_PROVENANCE_PATH_MAX], const char *s) {
    memset(out, 0, SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(uint16_t));
    for (int i = 0; s[i] && i < SEMANTICS_AR_PROVENANCE_PATH_MAX - 1; i++)
        out[i] = (uint16_t)s[i];
}

static void fill(uint8_t *b, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + 7u * (uint8_t)i);
}

int main(void) {
    uint8_t K[SEMANTICS_AR_MAC_SIZE];
    for (int i = 0; i < SEMANTICS_AR_MAC_SIZE; i++) K[i] = (uint8_t)(0x40 + i);

    uint16_t pa[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    uint16_t pb[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    path_of(pa, "a.txt");
    path_of(pb, "b.txt");

    uint8_t iv[16];
    fill(iv, sizeof iv, 0x11);
    uint8_t orig[256];
    fill(orig, sizeof orig, 0x80);

    sar_preserve_record_t recs[16];
    uint64_t count = 0;

    printf("[first-write-wins; double-encryption never overwrites the held original]\n");
    {
        sar_preserve_record_t r0, r1;
        sar_preserve_record_init(&r0, pa, 0, 256, 1000, 0, 256, iv, 16, orig, 256);
        uint8_t ct1[256];
        fill(ct1, sizeof ct1, 0x33);
        sar_preserve_record_init(&r1, pa, 0, 256, 1001, 256, 256, iv, 16, ct1, 256);

        int a0 = sar_preserve_append(recs, &count, 16, &r0);
        int a1 = sar_preserve_append(recs, &count, 16, &r1);
        uint8_t expect[SEMANTICS_AR_MAC_SIZE];
        sar_preserve_content_tag(orig, 256, expect);
        CHECK(a0 == 1 && a1 == 0 && count == 1 &&
              memcmp(recs[0].content_tag, expect, SEMANTICS_AR_MAC_SIZE) == 0,
              "second destructive write to a held region is rejected; original tag preserved");
    }

    printf("[distinct region of same file is a new hold; distinct file too]\n");
    {
        sar_preserve_record_t r2, r3;
        sar_preserve_record_init(&r2, pa, 256, 256, 1002, 512, 256, iv, 16, orig, 256);
        sar_preserve_record_init(&r3, pb, 0, 256, 1003, 768, 256, iv, 16, orig, 256);
        int a2 = sar_preserve_append(recs, &count, 16, &r2);
        int a3 = sar_preserve_append(recs, &count, 16, &r3);
        CHECK(a2 == 1 && a3 == 1 && count == 3,
              "non-overlapping region + distinct file each add a hold");
    }

    printf("[reconcile: a captured key discards the held originals it now recovers]\n");
    {
        uint64_t removed = sar_preserve_reconcile(recs, &count, pa, 0, 512);
        CHECK(removed == 2 && count == 1,
              "key covering a.txt[0,512) discards both held a.txt regions; b.txt untouched");
        CHECK(recs[0].provenance_offset == 0 &&
              memcmp(recs[0].provenance_path, pb,
                     SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(uint16_t)) == 0,
              "the surviving hold is b.txt");
    }

    printf("[partial coverage does not discard a hold the key cannot fully recover]\n");
    {
        sar_preserve_record_t r4;
        uint64_t c2 = 0;
        sar_preserve_record_t s2[4];
        sar_preserve_record_init(&r4, pa, 0, 256, 2000, 0, 256, iv, 16, orig, 256);
        sar_preserve_append(s2, &c2, 4, &r4);
        uint64_t removed = sar_preserve_reconcile(s2, &c2, pa, 0, 128);
        CHECK(removed == 0 && c2 == 1,
              "key covering only [0,128) does not discard the held [0,256) (containment, not overlap)");
    }

    printf("[budget: time eviction and capacity slide]\n");
    {
        sar_preserve_record_t s3[8];
        uint64_t c3 = 0;
        for (int i = 0; i < 5; i++) {
            sar_preserve_record_t r;
            uint16_t p[SEMANTICS_AR_PROVENANCE_PATH_MAX];
            char nm[8];
            nm[0] = (char)('a' + i); nm[1] = 0;
            path_of(p, nm);
            sar_preserve_record_init(&r, p, 0, 100, (uint64_t)(1000 + i * 100),
                                     (uint64_t)(i * 100), 100, iv, 16, orig, 100);
            sar_preserve_append(s3, &c3, 8, &r);
        }
        CHECK(c3 == 5 && sar_preserve_total_bytes(s3, c3) == 500, "five holds, 500 bytes");

        uint64_t aged = sar_preserve_evict_aged(s3, &c3, 1450, 300);
        CHECK(aged == 2 && c3 == 3,
              "retention=300 at now=1450 reclaims the two oldest (t=1000,1100)");

        uint64_t slid = sar_preserve_evict_oldest(s3, &c3, 150);
        CHECK(slid == 2 && c3 == 1 && sar_preserve_total_bytes(s3, c3) == 100,
              "capacity slide to 150 bytes drops oldest-first until under cap");

        int over = sar_preserve_would_exceed(s3, c3, 150, 100);
        CHECK(over == 1, "would_exceed reports the ENFORCE block condition before adding");
    }

    printf("[verify-before-restore: tag + binding]\n");
    {
        sar_preserve_record_t r;
        sar_preserve_record_init(&r, pa, 4096, 256, 3000, 0, 256, iv, 16, orig, 256);
        int ok = sar_preserve_verify_restore(&r, pa, 4096, orig, 256);
        uint8_t bad[256];
        fill(bad, sizeof bad, 0x80);
        bad[10] ^= 0x01;
        int corrupt = sar_preserve_verify_restore(&r, pa, 4096, bad, 256);
        int wrong_off = sar_preserve_verify_restore(&r, pa, 0, orig, 256);
        int wrong_path = sar_preserve_verify_restore(&r, pb, 4096, orig, 256);
        CHECK(ok == SAR_PRES_OK &&
              corrupt == SAR_PRES_RESTORE_MISMATCH &&
              wrong_off == SAR_PRES_RESTORE_MISMATCH &&
              wrong_path == SAR_PRES_RESTORE_MISMATCH,
              "restore verifies content tag AND (path, offset, length) binding");
    }

    printf("[serialize + verify + anchor]\n");
    {
        size_t need = sar_preserve_serialized_size(count);
        uint8_t *buf = (uint8_t *)malloc(need);
        size_t out_len = 0;
        int sret = sar_preserve_serialize(K, recs, count, 9, buf, need, &out_len);
        CHECK(sret == SAR_PRES_OK && out_len == need, "serialize OK, size matches");

        sar_preserve_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        sar_keystore_anchor_t anchor;
        anchor.present = 1;
        anchor.generation = 9;
        memcpy(anchor.head_mac, hdr.head_mac, SEMANTICS_AR_MAC_SIZE);

        uint64_t vc = 0;
        int vret = sar_preserve_verify(buf, out_len, K, &anchor, &vc);
        CHECK(vret == SAR_PRES_OK && vc == count, "verify clean store against anchor");

        size_t hdr_sz = sizeof(sar_preserve_header_t);
        buf[hdr_sz + 4] ^= 0x01;
        int tv = sar_preserve_verify(buf, out_len, K, &anchor, NULL);
        CHECK(tv == SAR_PRES_RECORD_MAC, "record field edit detected");
        buf[hdr_sz + 4] ^= 0x01;

        sar_preserve_record_t older[4];
        uint64_t oc = 1;
        memcpy(&older[0], &recs[0], sizeof(recs[0]));
        size_t oneed = sar_preserve_serialized_size(oc);
        uint8_t *obuf = (uint8_t *)malloc(oneed);
        sar_preserve_serialize(K, older, oc, 8, obuf, oneed, NULL);
        int rb = sar_preserve_verify(obuf, oneed, K, &anchor, NULL);
        CHECK(rb == SAR_PRES_ROLLBACK, "whole-store rollback to older generation detected");
        free(obuf);
        free(buf);
    }

    printf("\npreserve: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
