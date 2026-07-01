#include "recovery.h"
#include "driver.h"
#include "keystore_persist.h"
#include "preserve.h"

#include <ntifs.h>

#include "sar_recover.h"
#include "sar_keystore.h"

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
    PWCHAR target;
    PWCHAR temp;
    PUCHAR ctchunk = NULL;
    PUCHAR ptchunk = NULL;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    FILE_STANDARD_INFORMATION si;
    HANDLE th = NULL;
    HANDLE oh = NULL;
    UINT64 targetlen = 0;
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
    ctchunk = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_PRESERVE_STAGE_MAX + 16,
                                      SAR_POOL_TAG_RECOVER);
    ptchunk = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_PRESERVE_STAGE_MAX,
                                      SAR_POOL_TAG_RECOVER);
    if (record == NULL || ctchunk == NULL || ptchunk == NULL) {
        result = SAR_RECOVER_INVALID;
        goto cleanup;
    }

    RtlInitUnicodeString(&name, target);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateFile(&th, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                 FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0))) {
        th = NULL;
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }
    if (NT_SUCCESS(ZwQueryInformationFile(th, &iosb, &si, sizeof(si), FileStandardInformation)))
        targetlen = (UINT64)si.EndOfFile.QuadPart;

    if (targetlen == 0) {
        result = NT_SUCCESS(SarRecWriteTemp(temp, NULL, 0))
                     ? SAR_RECOVER_OK : SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    for (index = 0; SarKeystoreRecordAt(g_sar.keystore, index, record, &total); index++) {
        UINT64 off, rlen, soff, slen, pos;
        ULONG prefix;
        uint8_t tag[SEMANTICS_AR_MAC_SIZE];

        if (!RtlEqualMemory(record->key_id, Request->key_id, SEMANTICS_AR_KEY_ID_SIZE))
            continue;
        matched = 1;

        off = record->provenance_offset;
        if (off >= targetlen)
            continue;
        rlen = record->provenance_length;
        if (rlen == 0 || rlen > targetlen - off)
            rlen = targetlen - off;

        soff = record->sample_offset;
        slen = record->sample_length;
        if (slen == 0 || slen > SAR_PRESERVE_STAGE_MAX || soff < off || soff + slen > off + rlen)
            continue;

        sar_recovery_key_from_record(record, &rk);

        prefix = (soff >= 16) ? 16u : 0u;
        {
            LARGE_INTEGER ro;
            ro.QuadPart = (LONGLONG)(soff - prefix);
            if (!NT_SUCCESS(ZwReadFile(th, NULL, NULL, NULL, &iosb, ctchunk,
                                       (ULONG)(prefix + slen), &ro, NULL))) {
                RtlSecureZeroMemory(&rk, sizeof(rk));
                continue;
            }
        }
        if (sar_recover_chunk(&rk, SAR_CTR_CONTINUOUS, ctchunk + prefix, ptchunk,
                              off, soff, slen, soff, targetlen) != SAR_RECOVER_OK) {
            RtlSecureZeroMemory(&rk, sizeof(rk));
            continue;
        }
        sar_sample_tag(ptchunk, (uint32_t)slen, tag);
        if (!RtlEqualMemory(tag, record->sample_tag, SEMANTICS_AR_MAC_SIZE)) {
            RtlSecureZeroMemory(&rk, sizeof(rk));
            continue;
        }

        if (oh == NULL) {
            UNICODE_STRING tname;
            OBJECT_ATTRIBUTES toa;
            RtlInitUnicodeString(&tname, temp);
            InitializeObjectAttributes(&toa, &tname, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                       NULL, NULL);
            if (!NT_SUCCESS(ZwCreateFile(&oh, FILE_WRITE_DATA | SYNCHRONIZE, &toa, &iosb, NULL,
                                         FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                                         FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                         NULL, 0))) {
                oh = NULL;
                RtlSecureZeroMemory(&rk, sizeof(rk));
                result = SAR_RECOVER_DECLINED_TARGET_IO;
                goto cleanup;
            }
            for (pos = 0; pos < targetlen; pos += SAR_PRESERVE_STAGE_MAX) {
                ULONG cs = (targetlen - pos > SAR_PRESERVE_STAGE_MAX)
                               ? SAR_PRESERVE_STAGE_MAX : (ULONG)(targetlen - pos);
                LARGE_INTEGER io;
                io.QuadPart = (LONGLONG)pos;
                if (!NT_SUCCESS(ZwReadFile(th, NULL, NULL, NULL, &iosb, ctchunk, cs, &io, NULL)) ||
                    !NT_SUCCESS(ZwWriteFile(oh, NULL, NULL, NULL, &iosb, ctchunk, cs, &io, NULL))) {
                    RtlSecureZeroMemory(&rk, sizeof(rk));
                    result = SAR_RECOVER_DECLINED_TARGET_IO;
                    goto cleanup;
                }
            }
        }

        for (pos = off; pos < off + rlen; pos += SAR_PRESERVE_STAGE_MAX) {
            ULONG wlen = (off + rlen - pos > SAR_PRESERVE_STAGE_MAX)
                             ? SAR_PRESERVE_STAGE_MAX : (ULONG)(off + rlen - pos);
            ULONG pfx = (pos >= 16) ? 16u : 0u;
            LARGE_INTEGER ro, wo;
            ro.QuadPart = (LONGLONG)(pos - pfx);
            wo.QuadPart = (LONGLONG)pos;
            if (!NT_SUCCESS(ZwReadFile(th, NULL, NULL, NULL, &iosb, ctchunk, pfx + wlen, &ro, NULL))) {
                RtlSecureZeroMemory(&rk, sizeof(rk));
                result = SAR_RECOVER_DECLINED_TARGET_IO;
                goto cleanup;
            }
            if (sar_recover_chunk(&rk, SAR_CTR_CONTINUOUS, ctchunk + pfx, ptchunk,
                                  off, pos, wlen, pos, targetlen) != SAR_RECOVER_OK) {
                RtlSecureZeroMemory(&rk, sizeof(rk));
                result = SAR_RECOVER_DECLINED_MISMATCH;
                goto cleanup;
            }
            if (!NT_SUCCESS(ZwWriteFile(oh, NULL, NULL, NULL, &iosb, ptchunk, wlen, &wo, NULL))) {
                RtlSecureZeroMemory(&rk, sizeof(rk));
                result = SAR_RECOVER_DECLINED_TARGET_IO;
                goto cleanup;
            }
        }
        RtlSecureZeroMemory(&rk, sizeof(rk));
        applied++;
        recovered += rlen;
    }

    if (!matched) {
        result = SAR_RECOVER_DECLINED_NO_KEY_ID;
        goto cleanup;
    }
    if (!applied) {
        result = SAR_RECOVER_DECLINED_MISMATCH;
        goto cleanup;
    }
    if (oh != NULL && !NT_SUCCESS(ZwFlushBuffersFile(oh, &iosb))) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    *BytesRecovered = recovered;
    result = SAR_RECOVER_OK;

cleanup:
    if (oh != NULL)
        ZwClose(oh);
    if (th != NULL)
        ZwClose(th);
    if (ptchunk != NULL) {
        RtlSecureZeroMemory(ptchunk, SAR_PRESERVE_STAGE_MAX);
        ExFreePoolWithTag(ptchunk, SAR_POOL_TAG_RECOVER);
    }
    if (ctchunk != NULL) {
        RtlSecureZeroMemory(ctchunk, SAR_PRESERVE_STAGE_MAX + 16);
        ExFreePoolWithTag(ctchunk, SAR_POOL_TAG_RECOVER);
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
                                  FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
        UNICODE_STRING link;
        UNICODE_STRING dev;
        OBJECT_ATTRIBUTES loa;
        HANDLE lh;
        WCHAR linkBuf[8];
        WCHAR devBuf[128];
        ULONG i;
        ULONG n;

        if (name.Length < 7 * sizeof(WCHAR))
            return;
        RtlCopyMemory(linkBuf, DosPath, 6 * sizeof(WCHAR));
        linkBuf[6] = 0;
        RtlInitUnicodeString(&link, linkBuf);
        InitializeObjectAttributes(&loa, &link, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        if (!NT_SUCCESS(ZwOpenSymbolicLinkObject(&lh, SYMBOLIC_LINK_QUERY, &loa)))
            return;
        dev.Buffer = devBuf;
        dev.Length = 0;
        dev.MaximumLength = sizeof(devBuf);
        if (NT_SUCCESS(ZwQuerySymbolicLinkObject(lh, &dev, NULL))) {
            ULONG dchars = dev.Length / sizeof(WCHAR);
            n = 0;
            for (i = 0; i < dchars && n < SEMANTICS_AR_PROVENANCE_PATH_MAX - 1; i++)
                NtPath[n++] = (UINT16)devBuf[i];
            for (i = 6; DosPath[i] != 0 && n < SEMANTICS_AR_PROVENANCE_PATH_MAX - 1; i++)
                NtPath[n++] = (UINT16)DosPath[i];
            NtPath[n] = 0;
        }
        ZwClose(lh);
        return;
    }

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
    PUCHAR region = NULL;
    PUCHAR chunk = NULL;
    UINT64 off = Request->offset;
    UINT64 rlen = Request->length;
    UINT64 targetlen = 0;
    UINT64 final_size;
    UINT64 pos;
    UINT16 resolved[SEMANTICS_AR_PROVENANCE_PATH_MAX];
    const UINT16 *provpath;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE th = NULL;
    HANDLE oh = NULL;
    FILE_STANDARD_INFORMATION si;
    NTSTATUS status;
    int result;
    ULONG produced = 0;

    *BytesRecovered = 0;

    if (g_sar.preserve == NULL)
        return SAR_RECOVER_DECLINED_TARGET_IO;
    if (rlen == 0 || off + rlen < off)
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

    region = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)rlen, SAR_POOL_TAG_RECOVER);
    chunk = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, SAR_PRESERVE_STAGE_MAX, SAR_POOL_TAG_RECOVER);
    if (region == NULL || chunk == NULL) {
        result = SAR_RECOVER_INVALID;
        goto cleanup;
    }

    SarRecResolveNtPath(target, resolved);
    provpath = resolved[0] != 0 ? resolved : Request->target_path;
    status = SarPreserveRestore(g_sar.preserve, provpath, off, rlen, region, (ULONG)rlen, &produced);
    if (!NT_SUCCESS(status) || produced != (ULONG)rlen) {
        result = SAR_RECOVER_DECLINED_MISMATCH;
        goto cleanup;
    }

    RtlInitUnicodeString(&name, target);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(ZwCreateFile(&th, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0))) {
        if (NT_SUCCESS(ZwQueryInformationFile(th, &iosb, &si, sizeof(si), FileStandardInformation)))
            targetlen = (UINT64)si.EndOfFile.QuadPart;
    } else {
        th = NULL;
    }

    final_size = (off + rlen > targetlen) ? off + rlen : targetlen;

    RtlInitUnicodeString(&name, temp);
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateFile(&oh, FILE_WRITE_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                                 FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0))) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    for (pos = 0; pos < final_size; pos += SAR_PRESERVE_STAGE_MAX) {
        ULONG csize = (final_size - pos > SAR_PRESERVE_STAGE_MAX)
                          ? SAR_PRESERVE_STAGE_MAX : (ULONG)(final_size - pos);
        UINT64 ostart, oend;
        LARGE_INTEGER fo;

        RtlZeroMemory(chunk, csize);

        if (th != NULL && pos < targetlen) {
            ULONG rd = (targetlen - pos > csize) ? csize : (ULONG)(targetlen - pos);
            fo.QuadPart = (LONGLONG)pos;
            if (!NT_SUCCESS(ZwReadFile(th, NULL, NULL, NULL, &iosb, chunk, rd, &fo, NULL))) {
                result = SAR_RECOVER_DECLINED_TARGET_IO;
                goto cleanup;
            }
        }

        ostart = off > pos ? off : pos;
        oend = (off + rlen < pos + csize) ? off + rlen : pos + csize;
        if (ostart < oend)
            RtlCopyMemory(chunk + (ostart - pos), region + (ostart - off), (SIZE_T)(oend - ostart));

        fo.QuadPart = (LONGLONG)pos;
        if (!NT_SUCCESS(ZwWriteFile(oh, NULL, NULL, NULL, &iosb, chunk, csize, &fo, NULL))) {
            result = SAR_RECOVER_DECLINED_TARGET_IO;
            goto cleanup;
        }
    }

    if (!NT_SUCCESS(ZwFlushBuffersFile(oh, &iosb))) {
        result = SAR_RECOVER_DECLINED_TARGET_IO;
        goto cleanup;
    }

    *BytesRecovered = rlen;
    result = SAR_RECOVER_OK;

cleanup:
    if (oh != NULL)
        ZwClose(oh);
    if (th != NULL)
        ZwClose(th);
    if (chunk != NULL) {
        RtlSecureZeroMemory(chunk, SAR_PRESERVE_STAGE_MAX);
        ExFreePoolWithTag(chunk, SAR_POOL_TAG_RECOVER);
    }
    if (region != NULL) {
        RtlSecureZeroMemory(region, (SIZE_T)rlen);
        ExFreePoolWithTag(region, SAR_POOL_TAG_RECOVER);
    }
    ExFreePoolWithTag(target, SAR_POOL_TAG_RECOVER);
    return result;
}
