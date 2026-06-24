#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "sar_gate.h"

static int g_pass;
static int g_fail;

#define CHECK(cond, name) do {                                  \
    if (cond) { g_pass++; printf("  ok   %s\n", (name)); }       \
    else { g_fail++; printf("  FAIL %s\n", (name)); }            \
} while (0)

static uint32_t lcg_state;

static void lcg_seed(uint32_t s) { lcg_state = s ? s : 0x1234567u; }

static uint8_t lcg_byte(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (uint8_t)(lcg_state >> 24);
}

static void fill_random(uint8_t *p, size_t n, uint32_t seed)
{
    lcg_seed(seed);
    for (size_t i = 0; i < n; i++)
        p[i] = lcg_byte();
}

static void fill_alphabet(uint8_t *p, size_t n, uint32_t seed, const uint8_t *alpha, uint32_t alpha_n)
{
    lcg_seed(seed);
    for (size_t i = 0; i < n; i++)
        p[i] = alpha[lcg_byte() % alpha_n];
}

static void fill_text(uint8_t *p, size_t n)
{
    static const char *words = "the quick brown fox jumps over a lazy dog while the sun sets ";
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)words[w];
        if (words[++w] == '\0')
            w = 0;
    }
}

static double novelty(uint32_t nq, uint32_t nc)
{
    return nq ? (double)(nq - nc) / (double)nq : 0.0;
}

int main(void)
{
    static sar_gate_map_t scratch;
    static uint8_t orig[4096];
    static uint8_t wrote[4096];
    uint32_t nq, nc;
    sar_gate_result_t r;

    printf("[identity write -> skip]\n");
    fill_text(orig, 256);
    for (int i = 0; i < 256; i++) wrote[i] = orig[i];
    sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
    CHECK(nc == nq, "identity: every written pair covered");
    CHECK(!sar_gate_fires(nq, nc), "identity: does not fire");
    sar_gate_classify(&scratch, orig, wrote, 256, &r);
    CHECK(r.candidate == 0, "identity: classify skip");

    printf("[uniform-random ciphertext over low-entropy original -> fires >= 0.97]\n");
    fill_text(orig, 256);
    fill_random(wrote, 256, 0xC1A5);
    sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
    CHECK((nq - nc) * 100u >= nq * 97u, "ciphertext: novelty >= 0.97");
    CHECK(sar_gate_fires(nq, nc), "ciphertext: fires");

    printf("[k-byte benign point change -> skip]\n");
    fill_random(orig, 256, 0xBEEF);
    for (int i = 0; i < 256; i++) wrote[i] = orig[i];
    for (int i = 100; i < 106; i++) wrote[i] = (uint8_t)(orig[i] ^ 0xA5);
    sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
    CHECK(novelty(nq, nc) < 0.10, "point-change: novelty < theta");
    sar_gate_classify(&scratch, orig, wrote, 256, &r);
    CHECK(r.candidate == 0, "point-change: classify skip");

    printf("[adversarial entropy: high-entropy original, unrelated high-entropy write -> fires]\n");
    fill_random(orig, 256, 0xAAAA1111);
    fill_random(wrote, 256, 0x5555EEEE);
    sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
    CHECK((nq - nc) * 100u >= nq * 95u, "adversarial: novelty >= 0.95 despite high->high entropy");
    CHECK(sar_gate_fires(nq, nc), "adversarial: fires (entropy gate would skip)");

    printf("[intermittent slice not diluted: one 256B cipher block inside a 4096B write]\n");
    fill_text(orig, 4096);
    for (int i = 0; i < 4096; i++) wrote[i] = orig[i];
    fill_random(wrote + 1024, 256, 0x515E);
    sar_gate_classify(&scratch, orig, wrote, 4096, &r);
    CHECK(r.candidate == 1, "intermittent: fires");
    CHECK(r.novelty_off == 1024 && r.novelty_len == 256, "intermittent: novelty slice located at the cipher block");

    printf("[Black Basta-style 64B run inside a 256B block -> fires]\n");
    fill_text(orig, 256);
    for (int i = 0; i < 256; i++) wrote[i] = orig[i];
    fill_random(wrote + 100, 64, 0x6464);
    sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
    CHECK(novelty(nq, nc) >= 0.20, "64B run: novelty ~0.25");
    CHECK(sar_gate_fires(nq, nc), "64B run: fires");

    printf("[base64-armored ciphertext over text -> still fires (hard floor ~0.938)]\n");
    {
        static const uint8_t b64[65] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        fill_text(orig, 256);
        fill_alphabet(wrote, 256, 0xB64B, b64, 64);
        sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
        CHECK((nq - nc) * 100u >= nq * 90u, "base64: novelty >= 0.90");
        CHECK(sar_gate_fires(nq, nc), "base64: fires");
    }

    printf("[realistic hex-armored ciphertext over text -> still fires (only de-Bruijn corner misses)]\n");
    {
        static const uint8_t hex[17] = "0123456789abcdef";
        fill_text(orig, 256);
        fill_alphabet(wrote, 256, 0x4EE7, hex, 16);
        sar_gate_block_counts(&scratch, orig, wrote, 256, &nq, &nc);
        CHECK(sar_gate_fires(nq, nc), "hex: realistic hex fires");
    }

    printf("[edge: short write fires; sub-2-byte write skips]\n");
    fill_text(orig, 100);
    fill_random(wrote, 100, 0x5071);
    sar_gate_classify(&scratch, orig, wrote, 100, &r);
    CHECK(r.candidate == 1, "short write (100B): fires");
    sar_gate_classify(&scratch, orig, wrote, 1, &r);
    CHECK(r.candidate == 0, "1-byte write: skip (no 2-gram)");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
