#include <windows.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SAR_NONCE_SIZE        32u
#define SAR_P256_PUB_BLOB     72u
#define SAR_P256_SIG_SIZE     64u
#define SAR_ECDSA_P256_MAGIC  0x31534345u

static int g_checks;
static int g_fails;

static void check(int cond, const char *msg)
{
    g_checks++;
    if (!cond) {
        g_fails++;
        printf("FAIL: %s\n", msg);
    } else {
        printf("ok  : %s\n", msg);
    }
}

static SECURITY_STATUS make_signer(NCRYPT_PROV_HANDLE *prov, NCRYPT_KEY_HANDLE *key)
{
    SECURITY_STATUS st = NCryptOpenStorageProvider(prov, MS_KEY_STORAGE_PROVIDER, 0);
    if (st != ERROR_SUCCESS)
        return st;
    st = NCryptCreatePersistedKey(*prov, key, NCRYPT_ECDSA_P256_ALGORITHM, NULL, 0, 0);
    if (st != ERROR_SUCCESS)
        return st;
    return NCryptFinalizeKey(*key, 0);
}

int main(void)
{
    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE signer = 0;
    NCRYPT_PROV_HANDLE prov2 = 0;
    NCRYPT_KEY_HANDLE other = 0;
    SECURITY_STATUS ss;
    NTSTATUS ns;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE verifier = NULL;
    unsigned char blob[256];
    unsigned char other_blob[256];
    unsigned char nonce[SAR_NONCE_SIZE];
    unsigned char tampered[SAR_NONCE_SIZE];
    unsigned char sig[256];
    unsigned char bad_sig[SAR_P256_SIG_SIZE];
    DWORD blob_len = 0;
    DWORD other_blob_len = 0;
    DWORD sig_len = 0;
    uint32_t magic = 0;
    uint32_t cb_key = 0;

    ss = make_signer(&prov, &signer);
    check(ss == ERROR_SUCCESS, "create+finalize ephemeral ECDSA-P256 signer (NCrypt)");

    ss = NCryptExportKey(signer, 0, BCRYPT_ECCPUBLIC_BLOB, NULL,
                         blob, sizeof(blob), &blob_len, 0);
    check(ss == ERROR_SUCCESS, "export BCRYPT_ECCPUBLIC_BLOB");
    check(blob_len == SAR_P256_PUB_BLOB, "public blob is 72 bytes (8 hdr + 32 X + 32 Y)");

    memcpy(&magic, blob, 4);
    memcpy(&cb_key, blob + 4, 4);
    check(magic == SAR_ECDSA_P256_MAGIC, "blob magic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC ('ECS1')");
    check(cb_key == 32u, "blob cbKey == 32 (per-coordinate)");

    ns = BCryptGenRandom(NULL, nonce, SAR_NONCE_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    check(BCRYPT_SUCCESS(ns), "generate 32-byte nonce");

    ss = NCryptSignHash(signer, NULL, nonce, SAR_NONCE_SIZE,
                        sig, sizeof(sig), &sig_len, NCRYPT_SILENT_FLAG);
    check(ss == ERROR_SUCCESS, "NCryptSignHash over RAW 32-byte nonce, NULL padding (service side)");
    check(sig_len == SAR_P256_SIG_SIZE, "signature is 64 bytes (IEEE P1363 fixed r||s, not DER)");

    ns = BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0);
    check(BCRYPT_SUCCESS(ns), "BCryptOpenAlgorithmProvider ECDSA-P256 (driver side)");

    ns = BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &verifier,
                             blob, blob_len, 0);
    check(BCRYPT_SUCCESS(ns), "BCryptImportKeyPair of exported public blob");

    ns = BCryptVerifySignature(verifier, NULL, nonce, SAR_NONCE_SIZE, sig, sig_len, 0);
    check(BCRYPT_SUCCESS(ns),
          "BCryptVerifySignature accepts raw nonce + NULL padding (NCrypt<->BCrypt interop)");

    memcpy(tampered, nonce, SAR_NONCE_SIZE);
    tampered[0] ^= 0x01;
    ns = BCryptVerifySignature(verifier, NULL, tampered, SAR_NONCE_SIZE, sig, sig_len, 0);
    check(!BCRYPT_SUCCESS(ns), "tampered nonce is rejected");

    memcpy(bad_sig, sig, SAR_P256_SIG_SIZE);
    bad_sig[0] ^= 0x01;
    ns = BCryptVerifySignature(verifier, NULL, nonce, SAR_NONCE_SIZE, bad_sig, SAR_P256_SIG_SIZE, 0);
    check(!BCRYPT_SUCCESS(ns), "tampered signature is rejected");

    ss = make_signer(&prov2, &other);
    check(ss == ERROR_SUCCESS, "create a second independent signer");
    ss = NCryptExportKey(other, 0, BCRYPT_ECCPUBLIC_BLOB, NULL,
                         other_blob, sizeof(other_blob), &other_blob_len, 0);
    check(ss == ERROR_SUCCESS, "export second public blob");
    {
        BCRYPT_KEY_HANDLE wrong = NULL;
        ns = BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &wrong,
                                 other_blob, other_blob_len, 0);
        check(BCRYPT_SUCCESS(ns), "import second (wrong) public blob");
        ns = BCryptVerifySignature(wrong, NULL, nonce, SAR_NONCE_SIZE, sig, sig_len, 0);
        check(!BCRYPT_SUCCESS(ns), "signature does NOT verify under a different key (no cross-key accept)");
        if (wrong)
            BCryptDestroyKey(wrong);
    }

    if (verifier)
        BCryptDestroyKey(verifier);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    if (signer)
        NCryptFreeObject(signer);
    if (prov)
        NCryptFreeObject(prov);
    if (other)
        NCryptFreeObject(other);
    if (prov2)
        NCryptFreeObject(prov2);

    printf("\nhandshake-crypto: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
