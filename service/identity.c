#include "identity.h"

#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <string.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#define SAR_IDENTITY_READ_CHUNK 65536u

static void sar_identity_clear(sar_identity_t *id)
{
    memset(id, 0, sizeof(*id));
}

static void sar_copy_wide(uint16_t *dst, uint32_t cap, const wchar_t *src)
{
    uint32_t i = 0;
    if (!src)
        return;
    for (i = 0; i + 1 < cap && src[i] != L'\0'; i++)
        dst[i] = (uint16_t)src[i];
    dst[i] = 0;
}

static int sar_hash_handle(HANDLE file, uint8_t *out_hash)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS bs;
    uint8_t *chunk = NULL;
    int ok = 0;
    LARGE_INTEGER zero;

    zero.QuadPart = 0;
    if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN))
        return 0;

    bs = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (bs != STATUS_SUCCESS)
        goto done;

    bs = BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    if (bs != STATUS_SUCCESS)
        goto done;

    chunk = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, SAR_IDENTITY_READ_CHUNK);
    if (!chunk)
        goto done;

    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file, chunk, SAR_IDENTITY_READ_CHUNK, &got, NULL))
            goto done;
        if (got == 0)
            break;
        bs = BCryptHashData(hash, chunk, got, 0);
        if (bs != STATUS_SUCCESS)
            goto done;
    }

    bs = BCryptFinishHash(hash, out_hash, SEMANTICS_AR_CONTENT_HASH_SIZE, 0);
    if (bs == STATUS_SUCCESS)
        ok = 1;

done:
    if (chunk)
        HeapFree(GetProcessHeap(), 0, chunk);
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static int sar_trust_verify(const wchar_t *image_path, HANDLE file)
{
    WINTRUST_FILE_INFO fileInfo;
    WINTRUST_DATA      data;
    GUID               action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG               result;
    LARGE_INTEGER      zero;

    zero.QuadPart = 0;
    (void)SetFilePointerEx(file, zero, NULL, FILE_BEGIN);

    memset(&fileInfo, 0, sizeof(fileInfo));
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = image_path;
    fileInfo.hFile = file;

    memset(&data, 0, sizeof(data));
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_DISABLE_MD2_MD4;

    result = WinVerifyTrust(NULL, &action, &data);

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(NULL, &action, &data);

    return result == ERROR_SUCCESS;
}

static int sar_signer_subject(const wchar_t *image_path, uint16_t *subject,
                              uint32_t subject_cap)
{
    HCERTSTORE store = NULL;
    HCRYPTMSG  msg = NULL;
    DWORD      encoding = 0;
    DWORD      contentType = 0;
    DWORD      formatType = 0;
    PCMSG_SIGNER_INFO signer = NULL;
    DWORD      signerSize = 0;
    PCCERT_CONTEXT cert = NULL;
    CERT_INFO  search;
    int        ok = 0;
    DWORD      chars;

    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, image_path,
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding,
                          &contentType, &formatType, &store, &msg, NULL))
        return 0;

    if (!CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &signerSize)
        || signerSize == 0)
        goto done;

    signer = (PCMSG_SIGNER_INFO)HeapAlloc(GetProcessHeap(), 0, signerSize);
    if (!signer)
        goto done;

    if (!CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signer, &signerSize))
        goto done;

    memset(&search, 0, sizeof(search));
    search.Issuer = signer->Issuer;
    search.SerialNumber = signer->SerialNumber;

    cert = CertFindCertificateInStore(store, encoding, 0,
                                      CERT_FIND_SUBJECT_CERT, &search, NULL);
    if (!cert)
        goto done;

    chars = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0,
                               (void *)szOID_COMMON_NAME, NULL, 0);
    if (chars > 1) {
        wchar_t *name = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                             chars * sizeof(wchar_t));
        if (name) {
            if (CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0,
                                   (void *)szOID_COMMON_NAME, name, chars) > 1) {
                sar_copy_wide(subject, subject_cap, name);
                ok = 1;
            }
            HeapFree(GetProcessHeap(), 0, name);
        }
    }

done:
    if (cert)
        CertFreeCertificateContext(cert);
    if (signer)
        HeapFree(GetProcessHeap(), 0, signer);
    if (msg)
        CryptMsgClose(msg);
    if (store)
        CertCloseStore(store, 0);
    return ok;
}

sar_identity_verdict_t sar_identity_evaluate(const wchar_t *image_path,
                                             sar_identity_eval_t *out)
{
    HANDLE file;

    if (!image_path || !out)
        return SAR_IDENTITY_VERDICT_ERROR;

    sar_identity_clear(&out->identity);
    out->verdict = SAR_IDENTITY_VERDICT_ERROR;

    sar_copy_wide(out->identity.image_path, SEMANTICS_AR_PROTO_PATH_MAX,
                  image_path);

    file = CreateFileW(image_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        out->verdict = SAR_IDENTITY_VERDICT_PATH_FAILED;
        return out->verdict;
    }

    if (!sar_hash_handle(file, out->identity.content_hash)) {
        CloseHandle(file);
        out->verdict = SAR_IDENTITY_VERDICT_HASH_FAILED;
        return out->verdict;
    }

    if (!sar_trust_verify(image_path, file)) {
        CloseHandle(file);
        out->verdict = SAR_IDENTITY_VERDICT_UNSIGNED;
        return out->verdict;
    }

    if (!sar_signer_subject(image_path, out->identity.cert_subject,
                            SEMANTICS_AR_PROTO_SUBJECT_MAX)) {
        CloseHandle(file);
        out->verdict = SAR_IDENTITY_VERDICT_UNSIGNED;
        return out->verdict;
    }

    CloseHandle(file);
    out->verdict = SAR_IDENTITY_VERDICT_VERIFIED;
    return out->verdict;
}
