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

static void encrypt_wipe(const char *dir, int i)
{
    char path[300];
    HANDLE h;
    DWORD sz, blk, got = 0, put = 0;
    unsigned char *buf, *out, *keyobj, key[32], iv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG objlen = 0, cb = 0, ol = 0;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\rf%d.dat", dir, i);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    sz = GetFileSize(h, NULL);
    blk = sz & ~15u;
    buf = (unsigned char *)malloc(blk);
    out = (unsigned char *)malloc(blk);
    if (!buf || !out) { CloseHandle(h); free(buf); free(out); return; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, buf, blk, &got, NULL);

    BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    memcpy(iv, IV0, 16);
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objlen, sizeof(objlen), &cb, 0);
    keyobj = (unsigned char *)malloc(objlen);
    if (!keyobj) { BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(h); free(buf); free(out); return; }

    BCryptGenerateSymmetricKey(alg, &k, keyobj, objlen, key, 32, 0);
    BCryptEncrypt(k, buf, blk, NULL, iv, 16, out, blk, &ol, 0);

    /* WIPE all key material BEFORE the ciphertext ever touches disk */
    BCryptDestroyKey(k);
    SecureZeroMemory(keyobj, objlen);
    SecureZeroMemory(key, sizeof(key));
    BCryptCloseAlgorithmProvider(alg, 0);
    free(keyobj);

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, out, ol, &put, NULL);
    FlushFileBuffers(h);

    CloseHandle(h);
    free(buf);
    free(out);
}

int main(int argc, char **argv)
{
    const char *dir;
    int count, holdms, i;

    if (argc < 4) {
        printf("usage: wipe_encryptor <dir> <count> <postHoldMs>\n");
        return 1;
    }
    dir = argv[1];
    count = atoi(argv[2]);
    holdms = atoi(argv[3]);

    for (i = 0; i < count; i++)
        encrypt_wipe(dir, i);

    /* stay alive (keys already wiped) so the deferred worker attaches to a LIVE
       process and snapshots its heap -- isolating "key wiped" from "process exited" */
    if (holdms > 0)
        Sleep(holdms);

    printf("wipe-encrypted %d files in %s (keys zeroed pre-write)\n", count, dir);
    return 0;
}
