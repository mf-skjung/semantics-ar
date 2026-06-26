#include "recovery.h"
#include "driver.h"
#include "keystore_persist.h"
#include "preserve.h"

#include <ntifs.h>

#include "sar_recover.h"

#define SAR_REC_NT_PREFIX      L"\\??\\"
#define SAR_REC_NT_PREFIX_CCH  4
#define SAR_REC_TMP_SUFFIX     L".sarrectmp"
#define SAR_REC_TMP_SUFFIX_CCH 10
#define SAR_REC_PATH_CCH       (SAR_REC_NT_PREFIX_CCH + SEMANTICS_AR_PROTO_PATH_MAX + \
                                SAR_REC_TMP_SUFFIX_CCH + 1)

_IRQL_requires_max_(PASSIVE_LEVEL)
static USHORT SarRecBuildPaths(_In_reads_(SEMANTICS_AR_PROTO_PATH_MAX) const UINT16 *In,
                               _Out_writes_(SAR_REC_PATH_CCH) PWCHAR Target,
                               _Out_writes_(SAR_REC_PATH_CCH) PWCHAR Temp)
{
    USHORT n = 0;
    USHORT i;

    RtlCopyMemory(Target, SAR_REC_NT_PREFIX, SAR_REC_NT_PREFIX_CCH * sizeof(WCHAR));
    for (i = 0; i < SEMANTICS_AR_PROTO_PATH_MAX && In[i] != 0; i++)
        Target[SAR_REC_NT_PREFIX_CCH + i] = (WCHAR)In[i];
    n = (USHORT)(SAR_REC_NT_PREFIX_CCH + i);
    Target[n] = L'\0';
    if (i == 0)
        return 0;

    RtlCopyMemory(Temp, Target, (SIZE_T)n * sizeof(WCHAR));
    RtlCopyMemory(Temp + n, SAR_REC_TMP_SUFFIX, SAR_REC_TMP_SUFFIX_CCH * sizeof(WCHAR));
    Temp[n + SAR_REC_TMP_SUFFIX_CCH] = L'\0';
    return n;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarRecReadTarget(_In_ PCWSTR Path, _Outptr_result_maybenull_ PUCHAR *Buf,
                                 _Out_ SIZE_T *Len)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;
    FILE_STANDARD_INFORMATION si;
    PUCHAR buf;
    SIZE_T len;
    LARGE_INTEGER off;

    *Buf = NULL;
    *Len = 0;

    RtlInitUnicodeString(&name, Path);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    status = ZwQueryInformationFile(h, &iosb, &si, sizeof(si), FileStandardInformation);
    if (!NT_SUCCESS(status)) {
        ZwClose(h);
        return status;
    }
    if (si.EndOfFile.QuadPart == 0) {
        ZwClose(h);
        return STATUS_SUCCESS;
    }
    if ((ULONG64)si.EndOfFile.QuadPart > SAR_RECOVERY_MAX_BYTES) {
        ZwClose(h);
        return STATUS_FILE_TOO_LARGE;
    }

    len = (SIZE_T)si.EndOfFile.QuadPart;
    buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, len, SAR_POOL_TAG_RECOVER);
    if (buf == NULL) {
        ZwClose(h);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    off.QuadPart = 0;
    status = ZwReadFile(h, NULL, NULL, NULL, &iosb, buf, (ULONG)len, &off, NULL);
    ZwClose(h);
    if (!NT_SUCCESS(status) || iosb.Information != len) {
        RtlSecureZeroMemory(buf, len);
        ExFreePoolWithTag(buf, SAR_POOL_TAG_RECOVER);
        return NT_SUCCESS(status) ? STATUS_END_OF_FILE : status;
    }

    *Buf = buf;
    *Len = len;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static NTSTATUS SarRecWriteTemp(_In_ PCWSTR Path, _In_reads_bytes_(Len) const VOID *Buf,
                               _In_ SIZE_T Len)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    NTSTATUS status;

    RtlInitUnicodeString(&name, Path);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, FILE_WRITE_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status))
        return status;

    if (Len > 0)
        status = ZwWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)(ULONG_PTR)Buf, (ULONG)Len,
                             NULL, NULL);
    if (NT_SUCCESS(status))
        status = ZwFlushBuffersFile(h, &iosb);
    ZwClose(h);
    return status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarRecoveryExecute(_In_ const semantics_ar_recovery_exec_t *Request,
                       _Out_ UINT64 *BytesRecovered)
{
    semantics_ar_keystore_record_t *record = NULL;
    sar_recovery_key_t rk;
    sar_recovery_verify_t verify;
    PWCHAR target;
    PWCHAR temp;
    PUCHAR ct = NULL;
    PUCHAR pt = NULL;
    PUCHAR work = NULL;
    SIZE_T len = 0;
    NTSTATUS status;
    int result;
    ULONG64 index;
    ULONG64 total = 0;
    int matched = 0;
    int applied = 0;
    UINT64 recovered = 0;

    *BytesRecovered = 0;

    if (g_sar.keystore == NULL || !SarKeystoreReady(g_sar.keystore))
        return SAR_RECOVER_DECLINED_TARGET_IO;

    target = (PWCHAR)ExAllocatePool2(POOL_FLAG_PAGED, 2 * SAR_REC_PATH_CCH * sizeof(WCHAR),
                                     SAR_POOL_TAG_RECOVER);
    if (target == NULL)
        return SAR_RECOVER_INVALID;
    temp = target + SAR_REC_PATH_CCH;

    if (SarRecBuildPaths(Request->target_path, target, temp) == 0) {
        ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
        return SAR_RECOVER_INVALID;
    }

    record = (semantics_ar_keystore_record_t *)ExAllocatePool2(
        POOL_FLAG_PAGED, sizeof(*record), SAR_POOL_TAG_RECOVER);
    if (record == NULL) {
        ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
        return SAR_RECOVER_INVALID;
    }

    status = SarRecReadTarget(target, &ct, &len);
    if (status == STATUS_FILE_TOO_LARGE) {
        result = SAR_RECOVER_DECLINED_TOO_LARGE;
        goto cleanup;
    }
    if (!NT_SUCCESS(status)) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    if (len == 0) {
        result = NT_SUCCESS(SarRecWriteTemp(temp, NULL, 0))
                     ? SAR_RECOVER_OK : SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    pt = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, len, SAR_POOL_TAG_RECOVER);
    work = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, len, SAR_POOL_TAG_RECOVER);
    if (pt == NULL || work == NULL) {
        result = SAR_RECOVER_INVALID;
        goto cleanup;
    }
    RtlCopyMemory(pt, ct, len);

    for (index = 0; SarKeystoreRecordAt(g_sar.keystore, index, record, &total); index++) {
        UINT64 off;
        UINT64 rlen;

        if (!RtlEqualMemory(record->key_id, Request->key_id, SEMANTICS_AR_KEY_ID_SIZE))
            continue;
        matched = 1;

        off = record->provenance_offset;
        if (off >= (UINT64)len)
            continue;
        rlen = record->provenance_length;
        if (rlen == 0 || rlen > (UINT64)len - off)
            rlen = (UINT64)len - off;

        sar_recovery_key_from_record(record, &rk);
        status = (sar_recover_range(&rk, ct, work, (uint64_t)len, off, rlen) == SAR_RECOVER_OK)
                     ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        RtlSecureZeroMemory(&rk, sizeof(rk));
        if (!NT_SUCCESS(status))
            continue;

        sar_recovery_verify_from_record(record, &verify);
        if (sar_recover_verify(work, (uint64_t)len, &verify) == SAR_RECOVER_OK) {
            RtlCopyMemory(pt + off, work + off, (SIZE_T)rlen);
            applied++;
            recovered += rlen;
        }
    }

    if (!matched) {
        result = SAR_RECOVER_DECLINED_NO_KEY_ID;
        goto cleanup;
    }
    if (!applied) {
        result = SAR_RECOVER_DECLINED_MISMATCH;
        goto cleanup;
    }

    if (!NT_SUCCESS(SarRecWriteTemp(temp, pt, len))) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    *BytesRecovered = recovered;
    result = SAR_RECOVER_OK;

cleanup:
    if (work != NULL) {
        RtlSecureZeroMemory(work, len);
        ExFreePoolWithTag(work, SAR_POOL_TAG_RECOVER);
    }
    if (pt != NULL) {
        RtlSecureZeroMemory(pt, len);
        ExFreePoolWithTag(pt, SAR_POOL_TAG_RECOVER);
    }
    if (ct != NULL) {
        RtlSecureZeroMemory(ct, len);
        ExFreePoolWithTag(ct, SAR_POOL_TAG_RECOVER);
    }
    if (record != NULL) {
        RtlSecureZeroMemory(record, sizeof(*record));
        ExFreePoolWithTag(record, SAR_POOL_TAG_RECOVER);
    }
    ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
    return result;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
static VOID SarRecResolveNtPath(_In_ PCWSTR DosPath,
                                _Out_writes_(SEMANTICS_AR_PROVENANCE_PATH_MAX) UINT16 *NtPath)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    PFILE_OBJECT fo;

    RtlZeroMemory(NtPath, SEMANTICS_AR_PROVENANCE_PATH_MAX * sizeof(UINT16));
    RtlInitUnicodeString(&name, DosPath);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
                                  FILE_ATTRIBUTE_NORMAL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0)))
        return;

    if (NT_SUCCESS(ObReferenceObjectByHandle(h, 0, *IoFileObjectType, KernelMode,
                                              (PVOID *)&fo, NULL))) {
        ULONG needed = 0;
        ObQueryNameString(fo, NULL, 0, &needed);
        if (needed > 0) {
            POBJECT_NAME_INFORMATION info = (POBJECT_NAME_INFORMATION)ExAllocatePool2(
                POOL_FLAG_PAGED, needed, SAR_POOL_TAG_RECOVER);
            if (info != NULL) {
                if (NT_SUCCESS(ObQueryNameString(fo, info, needed, &needed))) {
                    ULONG chars = info->Name.Length / sizeof(WCHAR);
                    ULONG i;
                    if (chars > SEMANTICS_AR_PROVENANCE_PATH_MAX - 1)
                        chars = SEMANTICS_AR_PROVENANCE_PATH_MAX - 1;
                    for (i = 0; i < chars; i++)
                        NtPath[i] = (UINT16)info->Name.Buffer[i];
                }
                ExFreePoolWithTag(info, SAR_POOL_TAG_RECOVER);
            }
        }
        ObDereferenceObject(fo);
    }
    ZwClose(h);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarPreserveRecoveryExecute(_In_ const semantics_ar_preserve_recover_t *Request,
                               _Out_ UINT64 *BytesRecovered)
{
    PWCHAR target;
    PWCHAR temp;
    PUCHAR ct = NULL;
    PUCHAR region = NULL;
    SIZE_T len = 0;
    UINT64 off = Request->offset;
    UINT64 rlen = Request->length;
    NTSTATUS status;
    int result;
    ULONG produced = 0;

    *BytesRecovered = 0;

    if (g_sar.preserve == NULL)
        return SAR_RECOVER_DECLINED_TARGET_IO;
    if (rlen == 0 || rlen > SAR_RECOVERY_MAX_BYTES)
        return SAR_RECOVER_INVALID;

    target = (PWCHAR)ExAllocatePool2(POOL_FLAG_PAGED, 2 * SAR_REC_PATH_CCH * sizeof(WCHAR),
                                     SAR_POOL_TAG_RECOVER);
    if (target == NULL)
        return SAR_RECOVER_INVALID;
    temp = target + SAR_REC_PATH_CCH;

    if (SarRecBuildPaths(Request->target_path, target, temp) == 0) {
        ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
        return SAR_RECOVER_INVALID;
    }

    status = SarRecReadTarget(target, &ct, &len);
    if (status == STATUS_FILE_TOO_LARGE) {
        result = SAR_RECOVER_DECLINED_TOO_LARGE;
        goto cleanup;
    }
    if (!NT_SUCCESS(status)) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }
    if (off >= (UINT64)len || rlen > (UINT64)len - off) {
        result = SAR_RECOVER_DECLINED_MISMATCH;
        goto cleanup;
    }

    region = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)rlen, SAR_POOL_TAG_RECOVER);
    if (region == NULL) {
        result = SAR_RECOVER_INVALID;
        goto cleanup;
    }

    {
        UINT16 resolved[SEMANTICS_AR_PROVENANCE_PATH_MAX];
        SarRecResolveNtPath(target, resolved);
        status = SarPreserveRestore(g_sar.preserve,
                                    resolved[0] != 0 ? resolved : Request->target_path,
                                    off, rlen, region, (ULONG)rlen, &produced);
    }
    if (!NT_SUCCESS(status) || produced != (ULONG)rlen) {
        result = SAR_RECOVER_DECLINED_MISMATCH;
        goto cleanup;
    }

    RtlCopyMemory(ct + off, region, (SIZE_T)rlen);

    if (!NT_SUCCESS(SarRecWriteTemp(temp, ct, len))) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    *BytesRecovered = rlen;
    result = SAR_RECOVER_OK;

cleanup:
    if (region != NULL) {
        RtlSecureZeroMemory(region, (SIZE_T)rlen);
        ExFreePoolWithTag(region, SAR_POOL_TAG_RECOVER);
    }
    if (ct != NULL) {
        RtlSecureZeroMemory(ct, len);
        ExFreePoolWithTag(ct, SAR_POOL_TAG_RECOVER);
    }
    ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
    return result;
}
