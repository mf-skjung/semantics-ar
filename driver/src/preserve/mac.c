#include "driver_internal.h"

NTSTATUS semantics_ar_mac_init(VOID)
{
    NTSTATUS status;
    BCRYPT_ALG_HANDLE alg = NULL;

    status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!NT_SUCCESS(status))
        return status;

    ULONG objLen = 0;
    ULONG cb = 0;
    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen,
        sizeof(objLen), &cb, 0);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return status;
    }

    ULONG hashLen = 0;
    status = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen,
        sizeof(hashLen), &cb, 0);
    if (!NT_SUCCESS(status) || hashLen != SEMANTICS_AR_INTEGRITY_TAG_SIZE) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return STATUS_UNSUCCESSFUL;
    }

    status = BCryptGenRandom(NULL, semantics_ar_globals.MacKey,
        SEMANTICS_AR_MAC_KEY_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return status;
    }

    semantics_ar_globals.MacAlgHandle = alg;
    semantics_ar_globals.MacObjectLength = objLen;
    semantics_ar_globals.MacReady = TRUE;
    return STATUS_SUCCESS;
}

VOID semantics_ar_mac_cleanup(VOID)
{
    semantics_ar_globals.MacReady = FALSE;
    if (semantics_ar_globals.MacAlgHandle) {
        BCryptCloseAlgorithmProvider(semantics_ar_globals.MacAlgHandle, 0);
        semantics_ar_globals.MacAlgHandle = NULL;
    }
    RtlSecureZeroMemory(semantics_ar_globals.MacKey, SEMANTICS_AR_MAC_KEY_SIZE);
}

NTSTATUS semantics_ar_mac_compute(
    const UCHAR *Data,
    ULONG Length,
    UCHAR *Tag)
{
    if (!semantics_ar_globals.MacReady)
        return STATUS_UNSUCCESSFUL;

    NTSTATUS status;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR obj = (PUCHAR)ExAllocatePoolZero(NonPagedPoolNx,
        semantics_ar_globals.MacObjectLength, SEMANTICS_AR_POOL_TAG);
    if (!obj)
        return STATUS_INSUFFICIENT_RESOURCES;

    status = BCryptCreateHash(semantics_ar_globals.MacAlgHandle, &hash,
        obj, semantics_ar_globals.MacObjectLength,
        semantics_ar_globals.MacKey, SEMANTICS_AR_MAC_KEY_SIZE, 0);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(obj, SEMANTICS_AR_POOL_TAG);
        return status;
    }

    status = BCryptHashData(hash, (PUCHAR)Data, Length, 0);
    if (NT_SUCCESS(status))
        status = BCryptFinishHash(hash, Tag, SEMANTICS_AR_INTEGRITY_TAG_SIZE, 0);

    BCryptDestroyHash(hash);
    ExFreePoolWithTag(obj, SEMANTICS_AR_POOL_TAG);
    return status;
}