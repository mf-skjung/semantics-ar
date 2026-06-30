#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const UCHAR g_key[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};
static const UCHAR g_iv[16] = {
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f
};

static int aes_cbc(const UCHAR *in, ULONG in_len, UCHAR *out, ULONG out_cap, ULONG *out_len)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    UCHAR iv[16];
    NTSTATUS s;
    int ok = 0;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return 0;
    if (!BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0)))
        goto done;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &key, NULL, 0, (PUCHAR)g_key,
                                                   sizeof(g_key), 0)))
        goto done;
    memcpy(iv, g_iv, sizeof(iv));
    s = BCryptEncrypt(key, (PUCHAR)in, in_len, NULL, iv, sizeof(iv), out, out_cap, out_len,
                      BCRYPT_BLOCK_PADDING);
    ok = BCRYPT_SUCCESS(s);
done:
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static int make_victim(const wchar_t *path, ULONG size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    UCHAR *buf;
    DWORD wrote;
    ULONG i;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    buf = (UCHAR *)malloc(size);
    if (buf == NULL) { CloseHandle(h); return 0; }
    for (i = 0; i < size; i++)
        buf[i] = (UCHAR)('A' + (i % 26));
    ok = WriteFile(h, buf, size, &wrote, NULL) && wrote == size;
    FlushFileBuffers(h);
    free(buf);
    CloseHandle(h);
    return ok;
}

static int read_all(const wchar_t *path, UCHAR **out, ULONG *out_len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER sz;
    UCHAR *buf;
    DWORD got;

    *out = NULL;
    *out_len = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0 || sz.QuadPart > (1 << 26)) {
        CloseHandle(h);
        return 0;
    }
    buf = (UCHAR *)malloc((size_t)sz.QuadPart);
    if (buf == NULL) { CloseHandle(h); return 0; }
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != sz.QuadPart) {
        free(buf);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *out = buf;
    *out_len = got;
    return 1;
}

static int write_new(const wchar_t *path, const UCHAR *buf, ULONG len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, buf, len, &wrote, NULL) && wrote == len;
    CloseHandle(h);
    return ok;
}

static int do_newdelete(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    wchar_t locked[MAX_PATH];
    swprintf(locked, MAX_PATH, L"%s.locked", victim);
    if (!write_new(locked, ct, ct_len))
        return 0;
    return DeleteFileW(victim) ? 1 : 0;
}

static int do_renameover(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    wchar_t tmp[MAX_PATH];
    swprintf(tmp, MAX_PATH, L"%s.tmp", victim);
    if (!write_new(tmp, ct, ct_len))
        return 0;
    return MoveFileExW(tmp, victim, MOVEFILE_REPLACE_EXISTING) ? 1 : 0;
}

static int do_truncate(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER sz;
    ULONG head;
    int ok;

    (void)ct;
    (void)ct_len;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 32) { CloseHandle(h); return 0; }
    head = (ULONG)(sz.QuadPart / 2);
    SetFilePointer(h, (LONG)head, NULL, FILE_BEGIN);
    ok = SetEndOfFile(h);
    CloseHandle(h);
    return ok;
}

static int do_mmap(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE map;
    UCHAR *view;
    LARGE_INTEGER sz;
    ULONG n;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return 0; }
    map = CreateFileMappingW(h, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (map == NULL) { CloseHandle(h); return 0; }
    view = (UCHAR *)MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0);
    if (view == NULL) { CloseHandle(map); CloseHandle(h); return 0; }
    n = ct_len < (ULONG)sz.QuadPart ? ct_len : (ULONG)sz.QuadPart;
    memcpy(view, ct, n);
    FlushViewOfFile(view, n);
    UnmapViewOfFile(view);
    CloseHandle(map);
    CloseHandle(h);
    return 1;
}

static int do_setzero(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    FILE_ZERO_DATA_INFORMATION z;
    LARGE_INTEGER sz;
    DWORD ret;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return 0; }
    z.FileOffset.QuadPart = 0;
    z.BeyondFinalZero.QuadPart = sz.QuadPart;
    ok = DeviceIoControl(h, FSCTL_SET_ZERO_DATA, &z, sizeof(z), NULL, 0, &ret, NULL) ? 1 : 0;
    CloseHandle(h);
    return ok;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *mode;
    const wchar_t *dir;
    ULONG count;
    ULONG size;
    ULONG i;
    ULONG ok = 0;

    if (argc < 4) {
        wprintf(L"usage: %s <newdelete|renameover|truncate|mmap|setzero> <dir> <count> [sizeKB]\n",
                argv[0]);
        return 2;
    }
    mode = argv[1];
    dir = argv[2];
    count = (ULONG)_wtoi(argv[3]);
    size = (argc > 4 ? (ULONG)_wtoi(argv[4]) : 64) * 1024u;
    if (size == 0) size = 64u * 1024u;

    for (i = 0; i < count; i++) {
        wchar_t victim[MAX_PATH];
        UCHAR *pt = NULL;
        ULONG pt_len = 0;
        UCHAR *ct;
        ULONG ct_cap;
        ULONG ct_len = 0;
        int done = 0;

        swprintf(victim, MAX_PATH, L"%s\\victim_%05u.dat", dir, i);
        if (!make_victim(victim, size))
            continue;
        if (!read_all(victim, &pt, &pt_len))
            continue;

        ct_cap = pt_len + 32;
        ct = (UCHAR *)malloc(ct_cap);
        if (ct == NULL) { free(pt); continue; }
        if (!aes_cbc(pt, pt_len, ct, ct_cap, &ct_len)) {
            free(pt);
            free(ct);
            continue;
        }

        if (wcscmp(mode, L"newdelete") == 0)
            done = do_newdelete(victim, ct, ct_len);
        else if (wcscmp(mode, L"renameover") == 0)
            done = do_renameover(victim, ct, ct_len);
        else if (wcscmp(mode, L"truncate") == 0)
            done = do_truncate(victim, ct, ct_len);
        else if (wcscmp(mode, L"mmap") == 0)
            done = do_mmap(victim, ct, ct_len);
        else if (wcscmp(mode, L"setzero") == 0)
            done = do_setzero(victim);

        if (done)
            ok++;
        free(pt);
        free(ct);
    }

    wprintf(L"%s: %u/%u performed\n", mode, ok, count);
    return 0;
}
