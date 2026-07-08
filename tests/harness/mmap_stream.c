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

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: mmap_stream <chacha|salsa> <rounds> <dir> <count> <holdSeconds>\n");
        return 2;
    }
    int is_chacha = strcmp(argv[1], "chacha") == 0;
    int is_salsa = strcmp(argv[1], "salsa") == 0;
    if (!is_chacha && !is_salsa) { fprintf(stderr, "algo must be chacha or salsa\n"); return 2; }
    int rounds = atoi(argv[2]);
    if (rounds != 8 && rounds != 12 && rounds != 20) { fprintf(stderr, "rounds must be 8, 12, or 20\n"); return 2; }
    const char *dir = argv[3];
    int count = atoi(argv[4]);
    int hold = atoi(argv[5]);
    if (count <= 0) { fprintf(stderr, "count must be > 0\n"); return 2; }

    uint8_t key[32];
    uint8_t nonce[12];
    DWORD nlen = is_chacha ? 12 : 8;
    if (BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
        BCryptGenRandom(NULL, nonce, nlen, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fprintf(stderr, "rng failed\n"); return 3;
    }
    uint8_t state[64];
    if (is_chacha) build_chacha_state(state, key, nonce);
    else build_salsa_state(state, key, nonce);

    HANDLE *hf = (HANDLE *)calloc(count, sizeof(HANDLE));
    HANDLE *hm = (HANDLE *)calloc(count, sizeof(HANDLE));
    unsigned char **views = (unsigned char **)calloc(count, sizeof(unsigned char *));
    LARGE_INTEGER *sizes = (LARGE_INTEGER *)calloc(count, sizeof(LARGE_INTEGER));
    if (!hf || !hm || !views || !sizes) { fprintf(stderr, "alloc failed\n"); return 3; }

    int done = 0;
    for (int i = 0; i < count; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\sv_%05d.dat", dir, i);

        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) { printf("FAIL 1 %s\n", path); continue; }

        LARGE_INTEGER sz;
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1 << 26)) {
            printf("FAIL 2 %s\n", path); CloseHandle(h); continue;
        }

        HANDLE m = CreateFileMappingA(h, NULL, PAGE_READWRITE, sz.HighPart, sz.LowPart, NULL);
        if (m == NULL) { printf("FAIL 3 %s\n", path); CloseHandle(h); continue; }

        unsigned char *view = (unsigned char *)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0);
        if (view == NULL) { printf("FAIL 4 %s\n", path); CloseHandle(m); CloseHandle(h); continue; }

        DWORD n = (DWORD)sz.QuadPart;
        uint8_t ks[64];
        for (DWORD off = 0; off < n; off += 64) {
            keystream(is_chacha, key, nonce, rounds, off / 64, ks);
            DWORD span = (n - off < 64) ? (n - off) : 64;
            for (DWORD k = 0; k < span; k++) view[off + k] ^= ks[k];
        }
        FlushViewOfFile(view, 0);

        hf[i] = h; hm[i] = m; views[i] = view; sizes[i] = sz;
        done++;
        printf("OK %s\n", path);
    }
    printf("DONE algo=%s rounds=%d mode=resident files=%d\n", argv[1], rounds, done);
    printf("READY pid=%lu\n", (unsigned long)GetCurrentProcessId());
    fflush(stdout);

    if (hold > 0) Sleep((DWORD)hold * 1000);

    for (int i = 0; i < count; i++) {
        if (views[i] == NULL) continue;
        UnmapViewOfFile(views[i]);
        CloseHandle(hm[i]);
        FlushFileBuffers(hf[i]);
        CloseHandle(hf[i]);
    }

    SecureZeroMemory(state, sizeof state);
    SecureZeroMemory(key, sizeof key);
    SecureZeroMemory(nonce, sizeof nonce);
    free(hf); free(hm); free(views); free(sizes);
    return 0;
}
