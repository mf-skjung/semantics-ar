#include <windows.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

#define SAR_KEY_NAME   L"SemanticsArServiceKey"
#define SAR_KEY_SDDL   L"D:P(A;;GA;;;SY)(A;;GA;;;BA)"
#define SAR_P256_BLOB  72u

static int sar_write_header(const char *path, const unsigned char *blob, unsigned long len)
{
    FILE *f = fopen(path, "w");
    unsigned long i;

    if (f == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", path);
        return 0;
    }

    fputs("#ifndef SEMANTICS_AR_SERVICE_PUBKEY_H\n", f);
    fputs("#define SEMANTICS_AR_SERVICE_PUBKEY_H\n\n", f);
    fprintf(f, "static const unsigned char g_sar_service_public_key[%lu] = {\n", len);
    for (i = 0; i < len; i++) {
        if ((i % 12u) == 0u)
            fputs("    ", f);
        fprintf(f, "0x%02x,", blob[i]);
        if ((i % 12u) == 11u || (i + 1u) == len)
            fputc('\n', f);
        else
            fputc(' ', f);
    }
    fputs("};\n\n#endif\n", f);
    fclose(f);
    return 1;
}

static int sar_set_dacl(NCRYPT_KEY_HANDLE key)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    ULONG sd_len = 0;
    SECURITY_STATUS st;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            SAR_KEY_SDDL, SDDL_REVISION_1, &sd, &sd_len)) {
        fprintf(stderr, "build security descriptor failed: %lu\n", GetLastError());
        return 0;
    }

    st = NCryptSetProperty(key, NCRYPT_SECURITY_DESCR_PROPERTY,
                           (PBYTE)sd, sd_len,
                           NCRYPT_SILENT_FLAG | DACL_SECURITY_INFORMATION);
    LocalFree(sd);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "set key DACL failed: 0x%08lx\n", (unsigned long)st);
        return 0;
    }
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    SECURITY_STATUS st;
    DWORD flags = NCRYPT_OVERWRITE_KEY_FLAG;
    unsigned char blob[256];
    DWORD blob_len = 0;
    int dev = 0;
    const char *out = "service_pubkey.h";
    int i;

    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--user") == 0) {
            dev = 1;
        } else if (wcscmp(argv[i], L"--out") == 0 && i + 1 < argc) {
            static char path[1024];
            size_t n = 0;
            wcstombs_s(&n, path, sizeof(path), argv[i + 1], _TRUNCATE);
            out = path;
            i++;
        } else {
            fprintf(stderr, "usage: provision_handshake_key [--user] [--out <header>]\n");
            return 2;
        }
    }

    if (!dev)
        flags |= NCRYPT_MACHINE_KEY_FLAG;

    st = NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "open provider failed: 0x%08lx\n", (unsigned long)st);
        return 1;
    }

    st = NCryptCreatePersistedKey(prov, &key, NCRYPT_ECDSA_P256_ALGORITHM,
                                  SAR_KEY_NAME, 0, flags);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "create persisted key failed: 0x%08lx%s\n",
                (unsigned long)st,
                (st == NTE_PERM) ? " (machine key needs elevation; try --user for dev)" : "");
        NCryptFreeObject(prov);
        return 1;
    }

    if (!dev) {
        if (!sar_set_dacl(key)) {
            NCryptFreeObject(key);
            NCryptFreeObject(prov);
            return 1;
        }
    }

    st = NCryptFinalizeKey(key, 0);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "finalize key failed: 0x%08lx\n", (unsigned long)st);
        NCryptFreeObject(key);
        NCryptFreeObject(prov);
        return 1;
    }

    st = NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, NULL,
                         blob, sizeof(blob), &blob_len, 0);
    NCryptFreeObject(key);
    NCryptFreeObject(prov);
    if (st != ERROR_SUCCESS) {
        fprintf(stderr, "export public blob failed: 0x%08lx\n", (unsigned long)st);
        return 1;
    }
    if (blob_len != SAR_P256_BLOB) {
        fprintf(stderr, "unexpected public blob length %lu (expected %u)\n",
                (unsigned long)blob_len, SAR_P256_BLOB);
        return 1;
    }

    if (!sar_write_header(out, blob, blob_len))
        return 1;

    printf("provisioned %ls (%s store), wrote %s (%lu bytes)\n",
           SAR_KEY_NAME, dev ? "current-user" : "machine", out,
           (unsigned long)blob_len);
    return 0;
}
