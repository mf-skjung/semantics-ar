#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_file(const wchar_t *path, const UCHAR *buf, ULONG len, DWORD disp)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote;
    int ok;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, buf, len, &wrote, NULL) && wrote == len;
    CloseHandle(h);
    return ok;
}

static int read_file(const wchar_t *path, UCHAR *buf, ULONG cap, ULONG *got)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD g;
    int ok;
    *got = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = ReadFile(h, buf, cap, &g, NULL);
    if (ok) *got = g;
    CloseHandle(h);
    return ok;
}

static void fill_doc(UCHAR *p, ULONG n, ULONG seed)
{
    static const char *w = "the report covers quarterly results and the team review notes ";
    ULONG i;
    ULONG k = seed % 40u;
    for (i = 0; i < n; i++) {
        p[i] = (UCHAR)w[k];
        if (w[++k] == '\0') k = 0;
    }
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *dir;
    ULONG count;
    ULONG size;
    ULONG i;
    ULONG atomic_ok = 0, atomic_blocked = 0;
    ULONG copy_ok = 0, copy_blocked = 0;
    ULONG edit_ok = 0, edit_blocked = 0;
    ULONG del_ok = 0, del_blocked = 0;
    UCHAR *buf;
    UCHAR *rd;

    if (argc < 3) {
        wprintf(L"usage: %s <dir> <count> [sizeKB]\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    count = (ULONG)_wtoi(argv[2]);
    size = (argc > 3 ? (ULONG)_wtoi(argv[3]) : 32) * 1024u;
    if (size == 0) size = 32u * 1024u;

    buf = (UCHAR *)malloc(size);
    rd = (UCHAR *)malloc(size);
    if (buf == NULL || rd == NULL) return 1;

    for (i = 0; i < count; i++) {
        wchar_t doc[MAX_PATH], tmp[MAX_PATH], cpy[MAX_PATH];
        ULONG got = 0;

        swprintf(doc, MAX_PATH, L"%s\\doc_%05u.txt", dir, i);
        swprintf(tmp, MAX_PATH, L"%s\\doc_%05u.txt.tmp", dir, i);
        swprintf(cpy, MAX_PATH, L"%s\\copy_%05u.txt", dir, i);

        fill_doc(buf, size, i);
        if (!write_file(doc, buf, size, CREATE_ALWAYS))
            continue;

        read_file(doc, rd, size, &got);
        fill_doc(buf, size, i + 1);
        if (write_file(tmp, buf, size, CREATE_ALWAYS)) {
            if (MoveFileExW(tmp, doc, MOVEFILE_REPLACE_EXISTING))
                atomic_ok++;
            else {
                atomic_blocked++;
                DeleteFileW(tmp);
            }
        }

        read_file(doc, rd, size, &got);
        if (write_file(cpy, rd, got ? got : size, CREATE_ALWAYS))
            copy_ok++;
        else
            copy_blocked++;

        {
            HANDLE h = CreateFileW(doc, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD g2, w2;
                UCHAR small[64];
                ReadFile(h, small, sizeof(small), &g2, NULL);
                SetFilePointer(h, 32, NULL, FILE_BEGIN);
                small[0] = (UCHAR)('0' + (i % 10));
                if (WriteFile(h, small, 8, &w2, NULL))
                    edit_ok++;
                else
                    edit_blocked++;
                CloseHandle(h);
            }
        }

        if (DeleteFileW(cpy))
            del_ok++;
        else
            del_blocked++;
    }

    free(buf);
    free(rd);

    wprintf(L"benign: atomic_save ok=%u blocked=%u | copy ok=%u blocked=%u | "
            L"inplace_edit ok=%u blocked=%u | delete ok=%u blocked=%u\n",
            atomic_ok, atomic_blocked, copy_ok, copy_blocked,
            edit_ok, edit_blocked, del_ok, del_blocked);
    wprintf(L"BLOCKED_TOTAL=%u\n", atomic_blocked + copy_blocked + edit_blocked + del_blocked);
    return 0;
}
