#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#pragma comment(lib, "bcrypt.lib")

static char g_dir[260];
static int g_count, g_perfile, g_hold;
static volatile LONG g_next, g_dmg, g_prev, g_skip, g_blocked;
static BCRYPT_ALG_HANDLE g_alg;

static const unsigned char IV0[16] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};
static const unsigned char RK[32] = {
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7,
    0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7, 0xA7
};

static void one(int i)
{
    char path[300];
    HANDLE h;
    DWORD sz, blk, got = 0, put = 0;
    unsigned char *buf, *out, key[32], iv[16];
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG ol = 0;
    BOOL ok;

    if (g_blocked) { InterlockedIncrement(&g_prev); return; }

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\rf%d.dat", g_dir, i);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { InterlockedIncrement(&g_skip); return; }
    sz = GetFileSize(h, NULL);
    blk = sz & ~15u;
    buf = (unsigned char *)malloc(blk);
    out = (unsigned char *)malloc(blk);
    if (!buf || !out) { CloseHandle(h); free(buf); free(out); InterlockedIncrement(&g_skip); return; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, buf, blk, &got, NULL);

    if (g_perfile)
        BCryptGenRandom(NULL, key, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    else
        memcpy(key, RK, 32);
    memcpy(iv, IV0, 16);

    BCryptGenerateSymmetricKey(g_alg, &k, NULL, 0, key, 32, 0);
    BCryptEncrypt(k, buf, blk, NULL, iv, 16, out, blk, &ol, 0);

    if (g_hold)
        Sleep(g_hold);

    BCryptDestroyKey(k);
    SecureZeroMemory(key, sizeof(key));

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, out, ol, &put, NULL);
    CloseHandle(h);
    free(buf);
    free(out);

    if (!ok || put == 0) {
        InterlockedExchange(&g_blocked, 1);
        InterlockedIncrement(&g_prev);
    } else {
        InterlockedIncrement(&g_dmg);
    }
}

static unsigned __stdcall worker(void *a)
{
    (void)a;
    for (;;) {
        LONG i = InterlockedIncrement(&g_next) - 1;
        if (i >= g_count)
            break;
        one((int)i);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int threads, i;
    HANDLE *th;

    if (argc < 6) {
        printf("usage: wipe_test <dir> <count> <reuse|perfile> <keyAliveMs> <threads>\n");
        return 1;
    }
    strncpy_s(g_dir, sizeof(g_dir), argv[1], _TRUNCATE);
    g_count = atoi(argv[2]);
    g_perfile = (strcmp(argv[3], "perfile") == 0);
    g_hold = atoi(argv[4]);
    threads = atoi(argv[5]);
    if (threads < 1) threads = 1;
    g_next = 0; g_dmg = 0; g_prev = 0; g_skip = 0; g_blocked = 0;

    BCryptOpenAlgorithmProvider(&g_alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(g_alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);

    th = (HANDLE *)malloc(threads * sizeof(HANDLE));
    for (i = 0; i < threads; i++)
        th[i] = (HANDLE)_beginthreadex(NULL, 0, worker, NULL, 0, NULL);
    WaitForMultipleObjects(threads, th, TRUE, INFINITE);
    for (i = 0; i < threads; i++)
        CloseHandle(th[i]);

    printf("wipe_test mode=%s hold=%dms threads=%d count=%d => damaged=%d prevented=%d skip=%d\n",
           g_perfile ? "perfile" : "reuse", g_hold, threads, g_count, g_dmg, g_prev, g_skip);

    BCryptCloseAlgorithmProvider(g_alg, 0);
    free(th);
    return 0;
}
