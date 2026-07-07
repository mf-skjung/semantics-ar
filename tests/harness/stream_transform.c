#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../engine/ciphers/stream.c"

static const uint8_t SIGMA[16] = {
    0x65,0x78,0x70,0x61,0x6e,0x64,0x20,0x33,
    0x32,0x2d,0x62,0x79,0x74,0x65,0x20,0x6b
};

static void build_chacha_state(uint8_t *st, const uint8_t *key, const uint8_t *nonce12) {
    memcpy(st, SIGMA, 16);
    memcpy(st + 16, key, 32);
    memset(st + 48, 0, 4);
    memcpy(st + 52, nonce12, 12);
}

static void build_salsa_state(uint8_t *st, const uint8_t *key, const uint8_t *nonce8) {
    memcpy(st, SIGMA, 4);
    memcpy(st + 4, key, 16);
    memcpy(st + 20, SIGMA + 4, 4);
    memcpy(st + 24, nonce8, 8);
    memset(st + 32, 0, 8);
    memcpy(st + 40, SIGMA + 8, 4);
    memcpy(st + 44, key + 16, 16);
    memcpy(st + 60, SIGMA + 12, 4);
}

static void keystream(int is_chacha, const uint8_t *key, const uint8_t *nonce,
                      int rounds, uint64_t block_index, uint8_t out[64]) {
    if (is_chacha) chacha_block(key, nonce, (uint32_t)block_index, rounds, out);
    else salsa_block(key, nonce, block_index, rounds, out);
}

static int transform_file(const char *path, int is_chacha, const uint8_t *key,
                          const uint8_t *nonce, int rounds) {
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 1;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1 << 26)) { CloseHandle(h); return 2; }
    DWORD n = (DWORD)sz.QuadPart;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) { CloseHandle(h); return 3; }
    DWORD got = 0;
    if (!ReadFile(h, buf, n, &got, NULL) || got != n) { free(buf); CloseHandle(h); return 4; }
    uint8_t ks[64];
    for (DWORD off = 0; off < n; off += 64) {
        keystream(is_chacha, key, nonce, rounds, off / 64, ks);
        DWORD span = (n - off < 64) ? (n - off) : 64;
        for (DWORD i = 0; i < span; i++) buf[off + i] ^= ks[i];
    }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    DWORD put = 0;
    BOOL ok = WriteFile(h, buf, n, &put, NULL) && put == n;
    FlushFileBuffers(h);
    SecureZeroMemory(buf, n);
    free(buf);
    CloseHandle(h);
    return ok ? 0 : 5;
}

int main(int argc, char **argv) {
    if (argc != 7 && argc != 8) {
        fprintf(stderr, "usage: stream_transform <chacha|salsa> <rounds> <resident|oneshot> <dir> <count> <holdSeconds> [preHoldSeconds]\n");
        return 2;
    }
    int is_chacha = strcmp(argv[1], "chacha") == 0;
    int is_salsa = strcmp(argv[1], "salsa") == 0;
    if (!is_chacha && !is_salsa) { fprintf(stderr, "algo must be chacha or salsa\n"); return 2; }
    int rounds = atoi(argv[2]);
    if (rounds != 8 && rounds != 12 && rounds != 20) { fprintf(stderr, "rounds must be 8, 12, or 20\n"); return 2; }
    int resident = strcmp(argv[3], "resident") == 0;
    if (!resident && strcmp(argv[3], "oneshot") != 0) { fprintf(stderr, "mode must be resident or oneshot\n"); return 2; }
    const char *dir = argv[4];
    int count = atoi(argv[5]);
    int hold = atoi(argv[6]);

    uint8_t key[32];
    uint8_t nonce[12];
    DWORD nlen = is_chacha ? 12 : 8;
    if (BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
        BCryptGenRandom(NULL, nonce, nlen, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fprintf(stderr, "rng failed\n"); return 3;
    }

    uint8_t *state = NULL;
    if (resident) {
        state = (uint8_t *)malloc(64);
        if (!state) { fprintf(stderr, "alloc failed\n"); return 3; }
        if (is_chacha) build_chacha_state(state, key, nonce);
        else build_salsa_state(state, key, nonce);
    }

    if (argc == 8) {
        int prehold = atoi(argv[7]);
        printf("READY pid=%lu\n", (unsigned long)GetCurrentProcessId());
        fflush(stdout);
        if (prehold > 0)
            Sleep((DWORD)prehold * 1000);
    }

    int done = 0;
    for (int i = 0; i < count; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\sv_%05d.dat", dir, i);
        int r = transform_file(path, is_chacha, key, nonce, rounds);
        if (r == 0) { done++; printf("OK %s\n", path); }
        else printf("FAIL %d %s\n", r, path);
    }
    printf("DONE algo=%s rounds=%d mode=%s files=%d\n", argv[1], rounds, argv[3], done);
    fflush(stdout);

    if (resident) {
        Sleep((DWORD)hold * 1000);
        SecureZeroMemory(state, 64);
        free(state);
    }
    SecureZeroMemory(key, 32);
    SecureZeroMemory(nonce, sizeof nonce);
    return 0;
}
