#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static double now_ms(LARGE_INTEGER freq)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *dir;
    ULONG count;
    ULONG size;
    ULONG i;
    UCHAR *buf;
    UCHAR *rd;
    LARGE_INTEGER freq;
    double t0, t_create, t_read, t_overwrite, t_delete;
    DWORD w, g;

    if (argc < 3) {
        wprintf(L"usage: %s <dir> <count> [sizeKB]\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    count = (ULONG)_wtoi(argv[2]);
    size = (argc > 3 ? (ULONG)_wtoi(argv[3]) : 64) * 1024u;
    if (size == 0) size = 64u * 1024u;

    buf = (UCHAR *)malloc(size);
    rd = (UCHAR *)malloc(size);
    if (buf == NULL || rd == NULL) return 1;
    for (i = 0; i < size; i++) buf[i] = (UCHAR)(i * 7 + 3);

    QueryPerformanceFrequency(&freq);

    t0 = now_ms(freq);
    for (i = 0; i < count; i++) {
        wchar_t p[MAX_PATH];
        HANDLE h;
        swprintf(p, MAX_PATH, L"%s\\pf_%06u.bin", dir, i);
        h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) { WriteFile(h, buf, size, &w, NULL); CloseHandle(h); }
    }
    t_create = now_ms(freq) - t0;

    t0 = now_ms(freq);
    for (i = 0; i < count; i++) {
        wchar_t p[MAX_PATH];
        HANDLE h;
        swprintf(p, MAX_PATH, L"%s\\pf_%06u.bin", dir, i);
        h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) { ReadFile(h, rd, size, &g, NULL); CloseHandle(h); }
    }
    t_read = now_ms(freq) - t0;

    t0 = now_ms(freq);
    for (i = 0; i < count; i++) {
        wchar_t p[MAX_PATH];
        HANDLE h;
        swprintf(p, MAX_PATH, L"%s\\pf_%06u.bin", dir, i);
        h = CreateFileW(p, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, rd, size, &g, NULL);
            SetFilePointer(h, 0, NULL, FILE_BEGIN);
            WriteFile(h, buf, size, &w, NULL);
            CloseHandle(h);
        }
    }
    t_overwrite = now_ms(freq) - t0;

    t0 = now_ms(freq);
    for (i = 0; i < count; i++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%s\\pf_%06u.bin", dir, i);
        DeleteFileW(p);
    }
    t_delete = now_ms(freq) - t0;

    free(buf);
    free(rd);

    wprintf(L"perf count=%u sizeKB=%u create_ms=%.1f read_ms=%.1f overwrite_ms=%.1f delete_ms=%.1f\n",
            count, size / 1024u, t_create, t_read, t_overwrite, t_delete);
    wprintf(L"PERF_TOTAL_MS=%.1f\n", t_create + t_read + t_overwrite + t_delete);
    return 0;
}
