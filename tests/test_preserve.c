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
        sar_preserve_record_init(&r0, pa, 0, 256, 1000, 0, 256, 1, iv, 16, orig, 256);
        uint8_t ct1[256];
        fill(ct1, sizeof ct1, 0x33);
        sar_preserve_record_init(&r1, pa, 0, 256, 1001, 256, 256, 1, iv, 16, ct1, 256);

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
        sar_preserve_record_init(&r2, pa, 256, 256, 1002, 512, 256, 1, iv, 16, orig, 256);
        sar_preserve_record_init(&r3, pb, 0, 256, 1003, 768, 256, 2, iv, 16, orig, 256);
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
        sar_preserve_record_init(&r4, pa, 0, 256, 2000, 0, 256, 1, iv, 16, orig, 256);
        sar_preserve_append(s2, &c2, 4, &r4);
        uint64_t removed = sar_preserve_reconcile(s2, &c2, pa, 0, 128);
        CHECK(removed == 0 && c2 == 1,
              "key covering only [0,128) does not discard the held [0,256) (containment, not overlap)");
    }

    printf("[budget: time eviction; probation slide leaves protected untouched]\n");
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
                                     (uint64_t)(i * 100), 100, (uint64_t)(10 + i),
                                     iv, 16, orig, 100);
            sar_preserve_append(s3, &c3, 8, &r);
        }
        CHECK(c3 == 5 && sar_preserve_total_bytes(s3, c3) == 500, "five holds, 500 bytes");

        uint64_t aged = sar_preserve_evict_aged(s3, &c3, 1450, 300);
        CHECK(aged == 2 && c3 == 3,
              "retention=300 at now=1450 reclaims the two oldest (t=1000,1100)");

        uint64_t promoted = sar_preserve_promote(s3, c3, 12);
        CHECK(promoted == 1 && sar_preserve_protected_bytes(s3, c3) == 100 &&
              sar_preserve_probation_bytes(s3, c3) == 200,
              "promote actor 12 pins c (t=1200) to protected; 100 protected / 200 probation");

        uint64_t slid = sar_preserve_evict_probation_oldest(s3, &c3, 100);
        CHECK(slid == 1 && c3 == 2 &&
              sar_preserve_protected_bytes(s3, c3) == 100 &&
              sar_preserve_probation_bytes(s3, c3) == 100,
              "probation slide to 100B drops oldest probation (d, t=1300); protected c survives");

        int over = sar_preserve_would_exceed(s3, c3, 200, 1);
        int under = sar_preserve_would_exceed(s3, c3, 200, 0);
        CHECK(over == 1 && under == 0,
              "ENFORCE block keys on total store bytes (200 held + 1 incoming > 200)");
    }

    printf("[promote-by-actor: conviction pins all of an attacker's holds to protected]\n");
    {
        sar_preserve_record_t s4[8];
        uint64_t c4 = 0;
        sar_preserve_record_t q0, q1, q2;
        uint16_t pq[SEMANTICS_AR_PROVENANCE_PATH_MAX];
        path_of(pq, "q.txt");
        sar_preserve_record_init(&q0, pa, 0, 100, 100, 0, 100, 7, iv, 16, orig, 100);
        sar_preserve_record_init(&q1, pb, 0, 100, 101, 100, 100, 7, iv, 16, orig, 100);
        sar_preserve_record_init(&q2, pq, 0, 100, 102, 200, 100, 9, iv, 16, orig, 100);
        sar_preserve_append(s4, &c4, 8, &q0);
        sar_preserve_append(s4, &c4, 8, &q1);
        sar_preserve_append(s4, &c4, 8, &q2);
        uint64_t promoted = sar_preserve_promote(s4, c4, 7);
        CHECK(promoted == 2 && sar_preserve_protected_bytes(s4, c4) == 200 &&
              sar_preserve_probation_bytes(s4, c4) == 100,
              "convicting actor 7 protects both its holds; actor 9 stays in probation");
    }

    printf("[captured key discards even a promoted (protected) hold]\n");
    {
        sar_preserve_record_t s5[4];
        uint64_t c5 = 0;
        sar_preserve_record_t z0;
        sar_preserve_record_init(&z0, pa, 0, 256, 100, 0, 256, 5, iv, 16, orig, 256);
        sar_preserve_append(s5, &c5, 4, &z0);
        sar_preserve_promote(s5, c5, 5);
        CHECK(sar_preserve_protected_bytes(s5, c5) == 256 && c5 == 1,
              "hold promoted to protected");
        uint64_t removed = sar_preserve_reconcile(s5, &c5, pa, 0, 256);
        CHECK(removed == 1 && c5 == 0,
              "a captured key reconciles a hold regardless of pool (protected is not exempt from key)");
    }

    printf("[verify-before-restore: tag + binding + sub-region extract]\n");
    {
        sar_preserve_record_t r;
        sar_preserve_record_init(&r, pa, 4096, 256, 3000, 0, 256, 1, iv, 16, orig, 256);
        uint64_t inner = 0xdead;
        int ok = sar_preserve_verify_extract(&r, pa, 4096, 256, orig, 256, &inner);
        int whole_inner0 = (inner == 0);
        uint8_t bad[256];
        fill(bad, sizeof bad, 0x80);
        bad[10] ^= 0x01;
        int corrupt = sar_preserve_verify_extract(&r, pa, 4096, 256, bad, 256, &inner);
        int wrong_off = sar_preserve_verify_extract(&r, pa, 0, 256, orig, 256, &inner);
        int wrong_path = sar_preserve_verify_extract(&r, pb, 4096, 256, orig, 256, &inner);
        CHECK(ok == SAR_PRES_OK && whole_inner0 &&
              corrupt == SAR_PRES_RESTORE_MISMATCH &&
              wrong_off == SAR_PRES_RESTORE_MISMATCH &&
              wrong_path == SAR_PRES_RESTORE_MISMATCH,
              "restore verifies content tag AND (path, offset, length) binding");

        uint64_t sub = 0;
        int subok = sar_preserve_verify_extract(&r, pa, 4096 + 64, 128, orig, 256, &sub);
        int oob = sar_preserve_verify_extract(&r, pa, 4096 + 200, 128, orig, 256, &sub);
        int zerolen = sar_preserve_verify_extract(&r, pa, 4096, 0, orig, 256, &sub);
        CHECK(subok == SAR_PRES_OK && sub == 64 &&
              oob == SAR_PRES_RESTORE_MISMATCH &&
              zerolen == SAR_PRES_INVALID_ARG,
              "sub-region of a held region extracts at the right inner offset; OOB + zero-length decline");
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

    printf("[containment-aware staging: only the uncovered remainder is a gap]\n");
    {
        sar_preserve_record_t g[8];
        uint64_t gc = 0;
        uint64_t goff = 0, glen = 0;

        int empty = sar_preserve_first_gap(g, gc, pa, 0, 1000, &goff, &glen);
        CHECK(empty == 1 && goff == 0 && glen == 1000, "empty store: entire region is one gap");

        sar_preserve_record_t p0;
        sar_preserve_record_init(&p0, pa, 0, 16, 100, 0, 16, 1, iv, 16, orig, 16);
        sar_preserve_append(g, &gc, 8, &p0);

        int pfx = sar_preserve_first_gap(g, gc, pa, 0, 1u << 20, &goff, &glen);
        CHECK(pfx == 1 && goff == 16 && glen == (1u << 20) - 16,
              "prefix hold [0,16) + overwrite [0,1MB): uncovered gap is [16,1MB) (close-reopen defect closed)");

        int cov = sar_preserve_first_gap(g, gc, pa, 0, 16, &goff, &glen);
        CHECK(cov == 0, "region fully within a held record reports no gap (ALREADY_COVERED)");

        int other = sar_preserve_first_gap(g, gc, pb, 0, 16, &goff, &glen);
        CHECK(other == 1 && goff == 0 && glen == 16, "a hold on another path never covers this path");

        sar_preserve_record_t p1;
        sar_preserve_record_init(&p1, pa, 100, 100, 101, 16, 100, 1, iv, 16, orig, 100);
        sar_preserve_append(g, &gc, 8, &p1);

        int mid = sar_preserve_first_gap(g, gc, pa, 0, 300, &goff, &glen);
        CHECK(mid == 1 && goff == 16 && glen == 84,
              "first gap skips the prefix hold and stops at the next hold's start");
        int resume = sar_preserve_first_gap(g, gc, pa, 200, 100, &goff, &glen);
        CHECK(resume == 1 && goff == 200 && glen == 100,
              "gap resumes after an interior hold ends");
    }

    printf("\npreserve: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
