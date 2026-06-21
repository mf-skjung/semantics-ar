#include "preserve.h"
#include <ntstrsafe.h>

LONGLONG semantics_ar_shadow_cluster_align(
    semantics_ar_instance_context_t *InstCtx, LONGLONG Bytes)
{
    LONGLONG cs = (LONGLONG)InstCtx->ClusterSize;
    if (cs == 0) cs = SEMANTICS_AR_DEFAULT_SECTOR;
    return (Bytes + cs - 1) & ~(cs - 1);
}

BOOLEAN semantics_ar_shadow_ensure_initialized(
    PFLT_INSTANCE Instance, semantics_ar_instance_context_t *InstCtx)
{
    LONG state = InterlockedCompareExchange(&InstCtx->ShadowInitialized, 2, 0);
    if (state == 1) return TRUE;
    if (state != 0) {
        LARGE_INTEGER d;
        d.QuadPart = SEMANTICS_AR_JOURNAL_DRAIN_RECHECK_100NS;
        for (LONG i = 0; i < 50; i++) {
            KeDelayExecutionThread(KernelMode, FALSE, &d);
            if (InstCtx->ShadowInitialized == 1) return TRUE;
            if (InstCtx->ShadowInitialized == 0) break;
        }
        return InstCtx->ShadowInitialized == 1;
    }

    WCHAR pathBuf[384];
    UNICODE_STRING path;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE dh;
    NTSTATUS status;
    path.Buffer = pathBuf;
    path.MaximumLength = sizeof(pathBuf);

    PCWSTR dirs[] = { L"", L"\\overwrites", L"\\deletes" };
    for (int d = 0; d < 3; d++) {
        path.Length = 0;
        RtlCopyUnicodeString(&path, &InstCtx->ShadowBasePath);
        RtlAppendUnicodeToString(&path, dirs[d]);
        InitializeObjectAttributes(&oa, &path,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
        status = FltCreateFile(semantics_ar_globals.FilterHandle, Instance, &dh,
            FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, NULL,
            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
            FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
            NULL, 0, IO_IGNORE_SHARE_ACCESS_CHECK);
        if (!NT_SUCCESS(status)) {
            InterlockedExchange(&InstCtx->ShadowInitialized, 0);
            return FALSE;
        }
        FltClose(dh);
    }

    path.Length = 0;
    RtlCopyUnicodeString(&path, &InstCtx->ShadowBasePath);
    RtlAppendUnicodeToString(&path, L"\\journal.dat");
    InitializeObjectAttributes(&oa, &path,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = FltCreateFileEx(semantics_ar_globals.FilterHandle, Instance,
        &InstCtx->JournalHandle, &InstCtx->JournalFileObject,
        FILE_WRITE_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0, IO_IGNORE_SHARE_ACCESS_CHECK);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&InstCtx->ShadowInitialized, 0);
        return FALSE;
    }

    InstCtx->JournalWriteOffset.QuadPart = 0;
    if (iosb.Information == FILE_OPENED) {
        FILE_STANDARD_INFORMATION si;
        if (NT_SUCCESS(FltQueryInformationFile(Instance, InstCtx->JournalFileObject,
                &si, sizeof(si), FileStandardInformation, NULL)))
            InstCtx->JournalWriteOffset.QuadPart = si.EndOfFile.QuadPart;
    }
    InterlockedExchange(&InstCtx->AcceptNewJournalWrites, 1);

    FILE_FS_SIZE_INFORMATION fs;
    ULONG rl = 0;
    if (NT_SUCCESS(FltQueryVolumeInformationFile(Instance, InstCtx->JournalFileObject,
            &fs, sizeof(fs), FileFsSizeInformation, &rl)) &&
        fs.BytesPerSector > 0 && fs.SectorsPerAllocationUnit > 0) {
        InstCtx->SectorSize = fs.BytesPerSector;
        InstCtx->ClusterSize = fs.BytesPerSector * fs.SectorsPerAllocationUnit;
        LONGLONG total = (LONGLONG)fs.TotalAllocationUnits.QuadPart *
            fs.SectorsPerAllocationUnit * fs.BytesPerSector;
        LONGLONG budget = total * SEMANTICS_AR_SHADOW_PERCENT / 100;
        if (budget < SEMANTICS_AR_SHADOW_MIN_BYTES) budget = SEMANTICS_AR_SHADOW_MIN_BYTES;
        if (budget > SEMANTICS_AR_SHADOW_MAX_BYTES) budget = SEMANTICS_AR_SHADOW_MAX_BYTES;
        InstCtx->ShadowMaxBytes = budget;
    }

    InterlockedExchange(&InstCtx->ShadowInitialized, 1);
    return TRUE;
}

VOID semantics_ar_journal_append(
    PFLT_INSTANCE Instance,
    semantics_ar_instance_context_t *InstCtx,
    PUNICODE_STRING OriginalPath,
    ULONG EntryType,
    ULONG Pid,
    const WCHAR *ShadowPath,
    USHORT ShadowPathLength,
    LARGE_INTEGER WriteOffset,
    ULONG OriginalLength,
    LONGLONG Timestamp)
{
    semantics_ar_journal_entry_t je;
    RtlZeroMemory(&je, sizeof(je));
    je.entry_type = EntryType;
    je.process_id = Pid;
    je.original_offset = (UINT64)WriteOffset.QuadPart;
    je.original_length = OriginalLength;
    je.timestamp = Timestamp;

    if (OriginalPath != NULL && OriginalPath->Buffer != NULL) {
        ULONG cpn = min(OriginalPath->Length, sizeof(je.original_file_path) - sizeof(WCHAR));
        RtlCopyMemory(je.original_file_path, OriginalPath->Buffer, cpn);
    }
    ULONG cps = min(ShadowPathLength, sizeof(je.shadow_file_path) - sizeof(WCHAR));
    RtlCopyMemory(je.shadow_file_path, ShadowPath, cps);

    ULONG macRegion = FIELD_OFFSET(semantics_ar_journal_entry_t, integrity_tag);
    if (!NT_SUCCESS(semantics_ar_mac_compute((const UCHAR *)&je, macRegion, je.integrity_tag)))
        return;

    ULONG bw = 0;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&InstCtx->JournalResource, TRUE);
    if (InstCtx->JournalFileObject && InstCtx->AcceptNewJournalWrites) {
        InterlockedIncrement(&InstCtx->PendingJournalWrites);
        FltWriteFile(Instance, InstCtx->JournalFileObject,
            &InstCtx->JournalWriteOffset, (ULONG)sizeof(je), &je, 0, &bw, NULL, NULL);
        InstCtx->JournalWriteOffset.QuadPart += sizeof(je);
        InterlockedDecrement(&InstCtx->PendingJournalWrites);
    }
    ExReleaseResourceLite(&InstCtx->JournalResource);
    KeLeaveCriticalRegion();
}

VOID semantics_ar_drain_journal_all_instances(VOID)
{
    PFLT_INSTANCE instList[32];
    ULONG count = 0;
    if (!NT_SUCCESS(FltEnumerateInstances(NULL, semantics_ar_globals.FilterHandle,
            instList, 32, &count)))
        return;
    if (count > 32) count = 32;
    for (ULONG i = 0; i < count; i++) {
        semantics_ar_instance_context_t *ctx = NULL;
        if (NT_SUCCESS(FltGetInstanceContext(instList[i], (PFLT_CONTEXT *)&ctx))) {
            LARGE_INTEGER timeout;
            for (LONG attempt = 0; attempt < 3; attempt++) {
                timeout.QuadPart = (attempt == 0)
                    ? SEMANTICS_AR_JOURNAL_DRAIN_TIMEOUT_100NS
                    : SEMANTICS_AR_JOURNAL_DRAIN_RECHECK_100NS;
                KeWaitForSingleObject(&ctx->JournalDrainEvent, Executive, KernelMode,
                    FALSE, &timeout);
                if (ctx->PendingJournalWrites == 0)
                    break;
            }
            FltReleaseContext(ctx);
        }
        FltObjectDereference(instList[i]);
    }
}