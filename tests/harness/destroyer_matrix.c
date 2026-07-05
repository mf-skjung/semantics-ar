#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG SAR_NTSTATUS;
typedef struct _SAR_IO_STATUS_BLOCK { union { SAR_NTSTATUS Status; PVOID Pointer; } u; ULONG_PTR Information; } SAR_IO_STATUS_BLOCK;
typedef SAR_NTSTATUS (NTAPI *PFN_NtSetInformationFile)(HANDLE, SAR_IO_STATUS_BLOCK *, PVOID, ULONG, ULONG);
typedef struct _SAR_FILE_LINK_INFORMATION {
    BOOLEAN ReplaceIfExists;
    HANDLE  RootDirectory;
    ULONG   FileNameLength;
    WCHAR   FileName[1];
} SAR_FILE_LINK_INFORMATION;
#define SAR_FileLinkInformation 11

static const UCHAR g_key[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};
static const UCHAR g_iv[16] = {
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f
};

#ifndef FILE_DISPOSITION_FLAG_DELETE
#define FILE_DISPOSITION_FLAG_DELETE 0x00000001
typedef struct _FILE_DISPOSITION_INFO_EX { DWORD Flags; } FILE_DISPOSITION_INFO_EX;
#endif

#define R_PERFORMED   2
#define R_UNAVAILABLE 1
#define R_ERROR       0

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
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

static int do_noncached(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    HANDLE h;
    SYSTEM_INFO si;
    DWORD sector = 512, wrote;
    ULONG aligned;
    UCHAR *buf;
    int ok;

    GetSystemInfo(&si);
    (void)si;
    h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    aligned = (ct_len + sector - 1) & ~(sector - 1);
    buf = (UCHAR *)VirtualAlloc(NULL, aligned, MEM_COMMIT, PAGE_READWRITE);
    if (buf == NULL) { CloseHandle(h); return R_ERROR; }
    memset(buf, 0, aligned);
    memcpy(buf, ct, ct_len);
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, buf, aligned, &wrote, NULL) && wrote == aligned;
    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_disposeex(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    FILE_DISPOSITION_INFO_EX di;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    di.Flags = FILE_DISPOSITION_FLAG_DELETE;
    ok = SetFileInformationByHandle(h, (FILE_INFO_BY_HANDLE_CLASS)21, &di, sizeof(di));
    if (!ok && (GetLastError() == ERROR_INVALID_PARAMETER || GetLastError() == ERROR_NOT_SUPPORTED)) {
        CloseHandle(h);
        return R_UNAVAILABLE;
    }
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_allocshrink(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    FILE_ALLOCATION_INFO ai;
    LARGE_INTEGER sz;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 32) { CloseHandle(h); return R_ERROR; }
    ai.AllocationSize.QuadPart = sz.QuadPart / 2;
    ok = SetFileInformationByHandle(h, FileAllocationInfo, &ai, sizeof(ai));
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_setsparse(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    FILE_ZERO_DATA_INFORMATION z;
    FILE_SET_SPARSE_BUFFER sp;
    LARGE_INTEGER sz;
    DWORD ret;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return R_ERROR; }
    sp.SetSparse = TRUE;
    if (!DeviceIoControl(h, FSCTL_SET_SPARSE, &sp, sizeof(sp), NULL, 0, &ret, NULL)) {
        if (GetLastError() == ERROR_INVALID_FUNCTION || GetLastError() == ERROR_NOT_SUPPORTED) {
            CloseHandle(h);
            return R_UNAVAILABLE;
        }
    }
    z.FileOffset.QuadPart = 0;
    z.BeyondFinalZero.QuadPart = sz.QuadPart;
    ok = DeviceIoControl(h, FSCTL_SET_ZERO_DATA, &z, sizeof(z), NULL, 0, &ret, NULL) ? 1 : 0;
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_trim(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    FILE_LEVEL_TRIM req;
    LARGE_INTEGER sz;
    DWORD ret, err;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return R_ERROR; }
    memset(&req, 0, sizeof(req));
    req.NumRanges = 1;
    req.Ranges[0].Offset = 0;
    req.Ranges[0].Length = (DWORDLONG)sz.QuadPart;
    ok = DeviceIoControl(h, FSCTL_FILE_LEVEL_TRIM, &req, sizeof(req), NULL, 0, &ret, NULL);
    err = GetLastError();
    CloseHandle(h);
    if (!ok && (err == ERROR_INVALID_FUNCTION || err == ERROR_NOT_SUPPORTED ||
                err == ERROR_INVALID_PARAMETER || err == ERROR_FILE_LEVEL_TRIM_NOT_SUPPORTED))
        return R_UNAVAILABLE;
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_blockclone(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    wchar_t src[MAX_PATH];
    HANDLE hs, hd;
    DUPLICATE_EXTENTS_DATA ded;
    LARGE_INTEGER sz;
    DWORD ret;
    int ok;
    ULONG clone_len = ct_len & ~(4096u - 1u);

    if (clone_len == 0)
        return R_UNAVAILABLE;
    swprintf(src, MAX_PATH, L"%s.clonesrc", victim);
    if (!write_new(src, ct, clone_len))
        return R_ERROR;

    hd = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    hs = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (hd == INVALID_HANDLE_VALUE || hs == INVALID_HANDLE_VALUE) {
        if (hd != INVALID_HANDLE_VALUE) CloseHandle(hd);
        if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
        DeleteFileW(src);
        return R_ERROR;
    }
    if (!GetFileSizeEx(hd, &sz)) { CloseHandle(hd); CloseHandle(hs); DeleteFileW(src); return R_ERROR; }
    memset(&ded, 0, sizeof(ded));
    ded.FileHandle = hs;
    ded.SourceFileOffset.QuadPart = 0;
    ded.TargetFileOffset.QuadPart = 0;
    ded.ByteCount.QuadPart = clone_len;
    ok = DeviceIoControl(hd, FSCTL_DUPLICATE_EXTENTS_TO_FILE, &ded, sizeof(ded), NULL, 0, &ret, NULL);
    CloseHandle(hd);
    CloseHandle(hs);
    DeleteFileW(src);
    if (!ok) {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED || e == ERROR_BLOCK_TOO_MANY_REFERENCES)
            return R_UNAVAILABLE;
        return R_ERROR;
    }
    return R_PERFORMED;
}

static int do_linkreplace(const wchar_t *victim)
{
    wchar_t src[MAX_PATH];
    wchar_t ntname[MAX_PATH + 8];
    HANDLE h;
    UCHAR blob[256];
    UCHAR buf[sizeof(SAR_FILE_LINK_INFORMATION) + (MAX_PATH + 8) * sizeof(wchar_t)];
    SAR_FILE_LINK_INFORMATION *li = (SAR_FILE_LINK_INFORMATION *)buf;
    SAR_IO_STATUS_BLOCK iosb;
    PFN_NtSetInformationFile setinfo;
    ULONG namelen;
    SAR_NTSTATUS st;

    setinfo = (PFN_NtSetInformationFile)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                       "NtSetInformationFile");
    if (setinfo == NULL)
        return R_UNAVAILABLE;

    swprintf(src, MAX_PATH, L"%s.linksrc", victim);
    memset(blob, 0x5a, sizeof(blob));
    if (!write_new(src, blob, sizeof(blob)))
        return R_ERROR;

    h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { DeleteFileW(src); return R_ERROR; }

    swprintf(ntname, MAX_PATH + 8, L"\\??\\%s", victim);
    namelen = (ULONG)(wcslen(ntname) * sizeof(wchar_t));
    memset(buf, 0, sizeof(buf));
    li->ReplaceIfExists = TRUE;
    li->RootDirectory = NULL;
    li->FileNameLength = namelen;
    memcpy(li->FileName, ntname, namelen);

    st = setinfo(h, &iosb, buf,
                 (ULONG)(FIELD_OFFSET(SAR_FILE_LINK_INFORMATION, FileName) + namelen),
                 SAR_FileLinkInformation);
    CloseHandle(h);
    DeleteFileW(src);
    if (st == (SAR_NTSTATUS)0xC00000BBL || st == (SAR_NTSTATUS)0xC0000002L)
        return R_UNAVAILABLE;
    return (st >= 0) ? R_PERFORMED : R_ERROR;
}

static int do_mmapclose(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE map;
    UCHAR *view;
    LARGE_INTEGER sz;
    ULONG n;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return R_ERROR; }
    map = CreateFileMappingW(h, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (map == NULL) { CloseHandle(h); return R_ERROR; }
    view = (UCHAR *)MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0);
    if (view == NULL) { CloseHandle(map); CloseHandle(h); return R_ERROR; }
    n = ct_len < (ULONG)sz.QuadPart ? ct_len : (ULONG)sz.QuadPart;
    memcpy(view, ct, n);
    CloseHandle(h);
    UnmapViewOfFile(view);
    CloseHandle(map);
    return R_PERFORMED;
}

static int do_nocachemmap(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE map, hn;
    UCHAR *view, *abuf;
    LARGE_INTEGER sz;
    ULONG n, off;
    DWORD wrote;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return R_ERROR; }
    n = (ULONG)sz.QuadPart;
    map = CreateFileMappingW(h, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (map == NULL) { CloseHandle(h); return R_ERROR; }
    view = (UCHAR *)MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0);
    if (view == NULL) { CloseHandle(map); CloseHandle(h); return R_ERROR; }

    hn = CreateFileW(victim, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                     FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hn == INVALID_HANDLE_VALUE) {
        UnmapViewOfFile(view); CloseHandle(map); CloseHandle(h); return R_ERROR;
    }
    abuf = (UCHAR *)VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (abuf == NULL) {
        CloseHandle(hn); UnmapViewOfFile(view); CloseHandle(map); CloseHandle(h); return R_ERROR;
    }
    for (off = 0; off < n; ) {
        ULONG c = (n - off) < ct_len ? (n - off) : ct_len;
        memcpy(abuf + off, ct, c);
        off += c;
    }
    SetFilePointer(hn, 0, NULL, FILE_BEGIN);
    ok = WriteFile(hn, abuf, n, &wrote, NULL) && wrote == n;
    VirtualFree(abuf, 0, MEM_RELEASE);
    CloseHandle(hn);
    UnmapViewOfFile(view);
    CloseHandle(map);
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_intermittent(const wchar_t *victim)
{
    HANDLE h = CreateFileW(victim, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER sz;
    UCHAR *buf;
    DWORD got, wrote;
    ULONG i;
    int ok;

    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0 || sz.QuadPart > (1 << 26)) {
        CloseHandle(h);
        return R_ERROR;
    }
    buf = (UCHAR *)malloc((size_t)sz.QuadPart);
    if (buf == NULL) { CloseHandle(h); return R_ERROR; }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != sz.QuadPart) {
        free(buf); CloseHandle(h); return R_ERROR;
    }
    for (i = 0; i + 16 <= got; i += 32) {
        ULONG j;
        for (j = 0; j < 16; j++)
            buf[i + j] = (UCHAR)(buf[i + j] + 0x20);
    }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, buf, got, &wrote, NULL) && wrote == got;
    FlushFileBuffers(h);
    free(buf);
    CloseHandle(h);
    return ok ? R_PERFORMED : R_ERROR;
}

static int do_copydelete(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    wchar_t enc[MAX_PATH];
    swprintf(enc, MAX_PATH, L"%s.enc", victim);
    if (!write_new(enc, ct, ct_len))
        return R_ERROR;
    return DeleteFileW(victim) ? R_PERFORMED : R_ERROR;
}

static int do_preoverwrite(const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    wchar_t staged[MAX_PATH];
    HANDLE h;
    DWORD wrote;
    int ok;

    swprintf(staged, MAX_PATH, L"%s.stage", victim);
    if (!MoveFileExW(victim, staged, MOVEFILE_REPLACE_EXISTING))
        return R_ERROR;
    h = CreateFileW(staged, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return R_ERROR;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, ct, ct_len, &wrote, NULL) && wrote == ct_len;
    FlushFileBuffers(h);
    CloseHandle(h);
    MoveFileExW(staged, victim, MOVEFILE_REPLACE_EXISTING);
    return ok ? R_PERFORMED : R_ERROR;
}

static int dispatch(const wchar_t *mode, const wchar_t *victim, const UCHAR *ct, ULONG ct_len)
{
    if (wcscmp(mode, L"noncached") == 0)     return do_noncached(victim, ct, ct_len);
    if (wcscmp(mode, L"disposeex") == 0)      return do_disposeex(victim);
    if (wcscmp(mode, L"allocshrink") == 0)    return do_allocshrink(victim);
    if (wcscmp(mode, L"setsparse") == 0)      return do_setsparse(victim);
    if (wcscmp(mode, L"trim") == 0)           return do_trim(victim);
    if (wcscmp(mode, L"blockclone") == 0)     return do_blockclone(victim, ct, ct_len);
    if (wcscmp(mode, L"linkreplace") == 0)    return do_linkreplace(victim);
    if (wcscmp(mode, L"mmapclose") == 0)      return do_mmapclose(victim, ct, ct_len);
    if (wcscmp(mode, L"nocachemmap") == 0)    return do_nocachemmap(victim, ct, ct_len);
    if (wcscmp(mode, L"intermittent") == 0)   return do_intermittent(victim);
    if (wcscmp(mode, L"copydelete") == 0)     return do_copydelete(victim, ct, ct_len);
    if (wcscmp(mode, L"preoverwrite") == 0)   return do_preoverwrite(victim, ct, ct_len);
    return -1;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *mode, *dir;
    ULONG count, size, i;
    ULONG performed = 0, unavailable = 0, errors = 0;

    if (argc < 4) {
        wprintf(L"usage: %s <mode> <dir> <count> [sizeKB]\n", argv[0]);
        wprintf(L"  modes: noncached disposeex allocshrink setsparse trim blockclone\n");
        wprintf(L"         linkreplace mmapclose intermittent copydelete preoverwrite\n");
        return 2;
    }
    mode = argv[1];
    dir = argv[2];
    count = (ULONG)_wtoi(argv[3]);
    size = (argc > 4 ? (ULONG)_wtoi(argv[4]) : 64) * 1024u;
    if (size == 0) size = 64u * 1024u;

    for (i = 0; i < count; i++) {
        wchar_t victim[MAX_PATH];
        UCHAR *pt = NULL, *ct;
        ULONG pt_len = 0, ct_cap, ct_len = 0;
        int r;

        swprintf(victim, MAX_PATH, L"%s\\victim_%05u.dat", dir, i);
        if (!make_victim(victim, size))
            continue;
        if (!read_all(victim, &pt, &pt_len))
            continue;
        ct_cap = pt_len + 32;
        ct = (UCHAR *)malloc(ct_cap);
        if (ct == NULL) { free(pt); continue; }
        if (!aes_cbc(pt, pt_len, ct, ct_cap, &ct_len)) { free(pt); free(ct); continue; }

        r = dispatch(mode, victim, ct, ct_len);
        if (r == R_PERFORMED) performed++;
        else if (r == R_UNAVAILABLE) unavailable++;
        else if (r == R_ERROR) errors++;
        else { free(pt); free(ct); wprintf(L"unknown mode: %s\n", mode); return 2; }

        free(pt);
        free(ct);
    }

    wprintf(L"%s: performed=%u unavailable=%u errors=%u of %u\n",
            mode, performed, unavailable, errors, count);
    wprintf(L"PERFORMED=%u UNAVAILABLE=%u ERRORS=%u\n", performed, unavailable, errors);
    return 0;
}
