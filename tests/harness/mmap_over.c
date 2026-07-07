#include <windows.h>
#include <stdio.h>

/* mmap_over <file> <holdSec>
 * Maps an existing (known-plaintext) file PAGE_READWRITE, overwrites every byte in the view,
 * flushes the view (arming the section-sync capture), then holds the mapping+handle open for
 * holdSec so a concurrent instance's section-create observes the outstanding reservation.
 * On a refused section-create (STATUS_INSUFFICIENT_RESOURCES surfaced as a NULL mapping) it
 * prints REFUSED and leaves the file untouched. */
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) { wprintf(L"usage: mmap_over <file> <holdSec>\n"); return 2; }
    const wchar_t *path = argv[1];
    int hold = _wtoi(argv[2]);

    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { wprintf(L"FAIL open %lu %ls\n", GetLastError(), path); return 3; }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart == 0) { wprintf(L"FAIL size %ls\n", path); CloseHandle(f); return 3; }

    { char tmp[4096]; DWORD got = 0; ReadFile(f, tmp, sizeof tmp, &got, NULL); SetFilePointer(f, 0, NULL, FILE_BEGIN); }

    HANDLE m = CreateFileMappingW(f, NULL, PAGE_READWRITE, sz.HighPart, sz.LowPart, NULL);
    if (m == NULL) { wprintf(L"REFUSED map %lu %ls\n", GetLastError(), path); CloseHandle(f); return 0; }

    unsigned char *view = (unsigned char *)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0);
    if (view == NULL) { wprintf(L"REFUSED view %lu %ls\n", GetLastError(), path); CloseHandle(m); CloseHandle(f); return 0; }

    for (LONGLONG i = 0; i < sz.QuadPart; i++) view[i] = (unsigned char)(view[i] ^ 0xFF);
    FlushViewOfFile(view, 0);
    wprintf(L"OK %ls\n", path);
    fflush(stdout);

    if (hold > 0) Sleep((DWORD)hold * 1000);

    UnmapViewOfFile(view);
    FlushFileBuffers(f);
    CloseHandle(m);
    CloseHandle(f);
    return 0;
}
