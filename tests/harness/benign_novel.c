#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_doc(UCHAR *p, ULONG n, ULONG seed)
{
    static const char *w = "the report covers quarterly results and the team review notes ";
    ULONG i, k = seed % 40u;
    for (i = 0; i < n; i++) {
        p[i] = (UCHAR)w[k];
        if (w[++k] == '\0') k = 0;
    }
}

static void fill_entropy(UCHAR *p, ULONG n, unsigned long long seed)
{
    unsigned long long s = seed;
    ULONG i;
    for (i = 0; i < n; i++) {
        unsigned long long z;
        s += 0x9E3779B97F4A7C15ULL;
        z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        p[i] = (UCHAR)z;
    }
}

static int write_file(const wchar_t *path, const UCHAR *buf, ULONG len, DWORD disp)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote;
    int ok;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(h, buf, len, &wrote, NULL) && wrote == len;
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

static int overwrite_inplace(const wchar_t *path, const UCHAR *buf, ULONG len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote;
    int ok;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ok = WriteFile(h, buf, len, &wrote, NULL) && wrote == len;
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

static int aes_cbc_userkey(const UCHAR *in, ULONG in_len, UCHAR *out, ULONG out_cap, ULONG *out_len)
{
    UCHAR ukey[32], uiv[16];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    NTSTATUS s;
    int ok = 0;
    ULONG i;

    for (i = 0; i < 32; i++) ukey[i] = (UCHAR)(0x80 + i);
    for (i = 0; i < 16; i++) uiv[i] = (UCHAR)(0x40 + i);

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0)))
        return 0;
    if (!BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0)))
        goto done;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &key, NULL, 0, ukey, sizeof(ukey), 0)))
        goto done;
    s = BCryptEncrypt(key, (PUCHAR)in, in_len, NULL, uiv, sizeof(uiv), out, out_cap, out_len,
                      BCRYPT_BLOCK_PADDING);
    ok = BCRYPT_SUCCESS(s);
done:
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *dir;
    ULONG count, size, i;
    ULONG comp_blk = 0, vac_blk = 0, trans_blk = 0, enc_blk = 0, bulk_blk = 0;
    ULONG comp_ok = 0, vac_ok = 0, trans_ok = 0, enc_ok = 0, bulk_ok = 0;
    UCHAR *doc, *ent, *ct;

    if (argc < 3) {
        wprintf(L"usage: %s <dir> <count> [sizeKB]\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    count = (ULONG)_wtoi(argv[2]);
    size = (argc > 3 ? (ULONG)_wtoi(argv[3]) : 32) * 1024u;
    if (size == 0) size = 32u * 1024u;

    doc = (UCHAR *)malloc(size);
    ent = (UCHAR *)malloc(size);
    ct = (UCHAR *)malloc(size + 32);
    if (doc == NULL || ent == NULL || ct == NULL) return 1;

    for (i = 0; i < count; i++) {
        wchar_t a[MAX_PATH], b[MAX_PATH], c[MAX_PATH];

        fill_doc(doc, size, i);
        fill_entropy(ent, size, 0x1000ull + i);

        swprintf(a, MAX_PATH, L"%s\\compress_%05u.dat", dir, i);
        if (write_file(a, doc, size, CREATE_ALWAYS)) {
            if (overwrite_inplace(a, ent, size)) comp_ok++; else comp_blk++;
        }

        swprintf(a, MAX_PATH, L"%s\\vac_%05u.db", dir, i);
        swprintf(b, MAX_PATH, L"%s\\vac_%05u.db-wal", dir, i);
        if (write_file(a, doc, size, CREATE_ALWAYS)) {
            fill_doc(doc, size, i + 7);
            if (write_file(b, doc, size, CREATE_ALWAYS)) {
                if (MoveFileExW(b, a, MOVEFILE_REPLACE_EXISTING)) vac_ok++;
                else { vac_blk++; DeleteFileW(b); }
            }
        }

        swprintf(a, MAX_PATH, L"%s\\media_%05u.raw", dir, i);
        swprintf(b, MAX_PATH, L"%s\\media_%05u.enc", dir, i);
        if (write_file(a, doc, size, CREATE_ALWAYS)) {
            if (write_file(b, ent, size, CREATE_ALWAYS) && DeleteFileW(a)) trans_ok++;
            else trans_blk++;
        }

        swprintf(a, MAX_PATH, L"%s\\vault_%05u.doc", dir, i);
        swprintf(b, MAX_PATH, L"%s\\vault_%05u.doc.locked", dir, i);
        if (write_file(a, doc, size, CREATE_ALWAYS)) {
            ULONG ctlen = 0;
            if (aes_cbc_userkey(doc, size, ct, size + 32, &ctlen) &&
                write_file(b, ct, ctlen, CREATE_ALWAYS) && DeleteFileW(a)) enc_ok++;
            else enc_blk++;
        }

        swprintf(a, MAX_PATH, L"%s\\bulk_%05u.tmp", dir, i);
        swprintf(b, MAX_PATH, L"%s\\bulk_%05u.bak", dir, i);
        swprintf(c, MAX_PATH, L"%s\\bulk_%05u.old", dir, i);
        if (write_file(a, doc, size, CREATE_ALWAYS)) {
            int step = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) &&
                       CopyFileW(b, c, FALSE) &&
                       DeleteFileW(b) && DeleteFileW(c);
            if (step) bulk_ok++; else bulk_blk++;
        }
    }

    free(doc);
    free(ent);
    free(ct);

    wprintf(L"benign_novel: compress_inplace ok=%u blk=%u | vacuum ok=%u blk=%u | "
            L"transcode ok=%u blk=%u | encrypt_tool ok=%u blk=%u | bulk_churn ok=%u blk=%u\n",
            comp_ok, comp_blk, vac_ok, vac_blk, trans_ok, trans_blk, enc_ok, enc_blk, bulk_ok, bulk_blk);
    wprintf(L"BLOCKED_TOTAL=%u\n", comp_blk + vac_blk + trans_blk + enc_blk + bulk_blk);
    return 0;
}
