#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

static const unsigned char IV0[16] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};
static const unsigned char REUSE_KEY[32] = {
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7
};

static int encrypt_reuse_wipe(const char *dir, int i, int keyAliveMs)
{
    char path[300];
    HANDLE h;
    DWORD sz, blk, got = 0, put = 0;
    unsigned char *buf, *out, key[32], iv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG ol = 0;
    BOOL ok;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\rf%d.dat", dir, i);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    sz = GetFileSize(h, NULL);
    blk = sz & ~15u;
    buf = (unsigned char *)malloc(blk);
    out = (unsigned char *)malloc(blk);
    if (!buf || !out) { CloseHandle(h); free(buf); free(out); return 0; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, buf, blk, &got, NULL);

    memcpy(key, REUSE_KEY, 32);
    memcpy(iv, IV0, 16);
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(alg, &k, NULL, 0, key, 32, 0);
    BCryptEncrypt(k, buf, blk, NULL, iv, 16, out, blk, &ol, 0);

    if (keyAliveMs)
        Sleep(keyAliveMs);

    BCryptDestroyKey(k);
    SecureZeroMemory(key, sizeof(key));
    BCryptCloseAlgorithmProvider(alg, 0);

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, out, ol, &put, NULL);
    CloseHandle(h);
    free(buf);
    free(out);

    if (!ok || put == 0)
        return -1;
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir;
    int count, alive, i, dmg = 0, prev = 0, skip = 0;

    if (argc < 4) {
        printf("usage: wipe_reuse <dir> <count> <keyAliveMs>\n");
        return 1;
    }
    dir = argv[1];
    count = atoi(argv[2]);
    alive = atoi(argv[3]);

    for (i = 0; i < count; i++) {
        int r = encrypt_reuse_wipe(dir, i, alive);
        if (r == 1) dmg++;
        else if (r == -1) prev++;
        else skip++;
    }

    printf("wipe_reuse: %d files keyAlive=%dms => damaged(written)=%d prevented(denied)=%d skip=%d\n",
           count, alive, dmg, prev, skip);
    return 0;
}
