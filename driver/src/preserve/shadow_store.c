#include "preserve.h"
#include <ntstrsafe.h>

typedef struct _semantics_ar_capture_work_t {
    WORK_QUEUE_ITEM WorkItem;
    PFLT_INSTANCE   Instance;
    ULONG           Pid;
    USHORT          NameLength;
    WCHAR           Name[1];
} semantics_ar_capture_work_t;

static VOID account_shadow_usage(
    semantics_ar_instance_context_t *InstCtx, ULONG Pid, LONGLONG AlignedBytes)
{
    InterlockedExchangeAdd64(&InstCtx->ShadowBytesUsed, AlignedBytes);

    ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    semantics_ar_process_monitor_t *mon = semantics_ar_monitor_find(Pid);
    if (!mon)
        mon = semantics_ar_monitor_allocate(Pid);
    if (mon)
        InterlockedExchangeAdd64(&mon->ShadowBytesUsed, AlignedBytes);
    ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
}

semantics_ar_result_t semantics_ar_capture_read(
    PFLT_INSTANCE Instance,
    PFILE_OBJECT FileObject,
    LARGE_INTEGER Offset,
    ULONG Length,
    PUCHAR Buffer,
    PULONG BytesReturned)
{
    ULONG bytesRead = 0;
    NTSTATUS status = FltReadFile(Instance, FileObject, &Offset, Length, Buffer,
        FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET, &bytesRead, NULL, NULL);
    if (!NT_SUCCESS(status)) {
        *BytesReturned = 0;
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;
    }
    *BytesReturned = bytesRead;
    return SEMANTICS_AR_OK;
}

static semantics_ar_result_t write_shadow_file(
    PFLT_INSTANCE Instance, semantics_ar_instance_context_t *InstCtx,
    ULONG Pid, LARGE_INTEGER Offset, const UCHAR *Data, ULONG DataLen,
    LONGLONG Timestamp, WCHAR *ShadowPathOut, USHORT *ShadowPathLenOut)
{
    LONGLONG seq = InterlockedIncrement64(&semantics_ar_globals.ShadowSequence);

    NTSTATUS s = RtlStringCbPrintfW(ShadowPathOut, 384 * sizeof(WCHAR),
        L"%wZ\\overwrites\\%08X_%016llX.shd", &InstCtx->ShadowBasePath, Pid, (ULONGLONG)seq);
    if (!NT_SUCCESS(s))
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;

    UNICODE_STRING sp;
    RtlInitUnicodeString(&sp, ShadowPathOut);
    *ShadowPathLenOut = sp.Length;

    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    InitializeObjectAttributes(&oa, &sp, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE sh = NULL;
    PFILE_OBJECT sfo = NULL;
    NTSTATUS status = FltCreateFileEx(semantics_ar_globals.FilterHandle, Instance,
        &sh, &sfo, GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, 0, FILE_CREATE,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0, IO_IGNORE_SHARE_ACCESS_CHECK);
    if (!NT_SUCCESS(status))
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;

    semantics_ar_shadow_overwrite_header_t hdr;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = SEMANTICS_AR_SHADOW_MAGIC;
    hdr.process_id = Pid;
    hdr.original_offset = (uint64_t)Offset.QuadPart;
    hdr.original_length = DataLen;
    hdr.timestamp = Timestamp;

    ULONG bw = 0;
    LARGE_INTEGER wp;
    wp.QuadPart = 0;
    BOOLEAN ok = FALSE;
    status = FltWriteFile(Instance, sfo, &wp, (ULONG)sizeof(hdr), &hdr, 0, &bw, NULL, NULL);
    if (NT_SUCCESS(status)) {
        wp.QuadPart = sizeof(hdr);
        status = FltWriteFile(Instance, sfo, &wp, DataLen, (PVOID)Data, 0, &bw, NULL, NULL);
        if (NT_SUCCESS(status))
            ok = TRUE;
    }

    ObDereferenceObject(sfo);
    FltClose(sh);

    return ok ? SEMANTICS_AR_OK : SEMANTICS_AR_ERROR_PRESERVE_FAILED;
}

semantics_ar_result_t semantics_ar_preserve_range(
    PFLT_CALLBACK_DATA Data,
    PFLT_INSTANCE Instance,
    PFILE_OBJECT FileObject,
    PUNICODE_STRING OverrideOriginalPath,
    LARGE_INTEGER WriteOffset,
    ULONG WriteLength,
    LONGLONG Eof,
    ULONG EntryType,
    ULONG Pid)
{
    semantics_ar_instance_context_t *instCtx = NULL;
    if (!NT_SUCCESS(FltGetInstanceContext(Instance, (PFLT_CONTEXT *)&instCtx)))
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;

    if (!semantics_ar_shadow_ensure_initialized(Instance, instCtx)) {
        FltReleaseContext(instCtx);
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;
    }

    ULONG preserveLen = WriteLength;
    if ((LONGLONG)(WriteOffset.QuadPart + preserveLen) > Eof)
        preserveLen = (ULONG)(Eof - WriteOffset.QuadPart);
    if (preserveLen == 0) {
        FltReleaseContext(instCtx);
        return SEMANTICS_AR_ERROR_INVALID_PARAM;
    }

    PUCHAR dataBuffer = (PUCHAR)ExAllocatePoolZero(PagedPool, preserveLen, SEMANTICS_AR_POOL_TAG_PG);
    if (!dataBuffer) {
        FltReleaseContext(instCtx);
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;
    }

    ULONG bytesRead = 0;
    semantics_ar_result_t rd = semantics_ar_capture_read(Instance,
        FileObject, WriteOffset, preserveLen, dataBuffer, &bytesRead);
    if (rd != SEMANTICS_AR_OK || bytesRead == 0) {
        ExFreePoolWithTag(dataBuffer, SEMANTICS_AR_POOL_TAG_PG);
        FltReleaseContext(instCtx);
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;
    }

    LARGE_INTEGER now;
    KeQuerySystemTimePrecise(&now);

    WCHAR shadowPath[384];
    USHORT shadowPathLen = 0;
    semantics_ar_result_t ws = write_shadow_file(Instance, instCtx, Pid,
        WriteOffset, dataBuffer, bytesRead, now.QuadPart, shadowPath, &shadowPathLen);

    ExFreePoolWithTag(dataBuffer, SEMANTICS_AR_POOL_TAG_PG);

    if (ws != SEMANTICS_AR_OK) {
        FltReleaseContext(instCtx);
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;
    }

    LONGLONG aligned = semantics_ar_shadow_cluster_align(instCtx,
        (LONGLONG)sizeof(semantics_ar_shadow_overwrite_header_t) + bytesRead);
    account_shadow_usage(instCtx, Pid, aligned);

    UNICODE_STRING origPath;
    PFLT_FILE_NAME_INFORMATION ni = NULL;
    if (OverrideOriginalPath != NULL) {
        origPath = *OverrideOriginalPath;
    } else {
        RtlInitUnicodeString(&origPath, NULL);
        if (Data != NULL && NT_SUCCESS(FltGetFileNameInformation(Data,
                FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_CACHE_ONLY, &ni))) {
            FltParseFileNameInformation(ni);
            origPath = ni->Name;
        }
    }

    semantics_ar_journal_append(Instance, instCtx, &origPath, EntryType, Pid,
        shadowPath, shadowPathLen, WriteOffset, bytesRead, now.QuadPart);

    if (ni != NULL)
        FltReleaseFileNameInformation(ni);

    FltReleaseContext(instCtx);
    return SEMANTICS_AR_OK;
}

static semantics_ar_result_t preserve_full_from_object(
    PFLT_CALLBACK_DATA Data, PFLT_INSTANCE Instance, PFILE_OBJECT FileObject,
    PUNICODE_STRING RecordName, ULONG Pid)
{
    FILE_STANDARD_INFORMATION si;
    if (!NT_SUCCESS(FltQueryInformationFile(Instance, FileObject,
            &si, sizeof(si), FileStandardInformation, NULL)))
        return SEMANTICS_AR_ERROR_PRESERVE_FAILED;

    if (si.EndOfFile.QuadPart == 0)
        return SEMANTICS_AR_ERROR_INVALID_PARAM;

    LARGE_INTEGER off;
    off.QuadPart = 0;
    LONGLONG remaining = si.EndOfFile.QuadPart;

    while (remaining > 0) {
        ULONG chunk = (remaining > SEMANTICS_AR_TRUNCATION_MAX_BYTES)
            ? SEMANTICS_AR_TRUNCATION_MAX_BYTES : (ULONG)remaining;
        semantics_ar_result_t r = semantics_ar_preserve_range(Data, Instance, FileObject,
            RecordName, off, chunk, si.EndOfFile.QuadPart, SEMANTICS_AR_JOURNAL_OVERWRITE, Pid);
        if (r != SEMANTICS_AR_OK)
            return r;
        off.QuadPart += chunk;
        remaining -= chunk;
    }

    return SEMANTICS_AR_OK;
}

static semantics_ar_result_t preserve_full_via_reopen(
    PFLT_INSTANCE Instance, PUNICODE_STRING OpenName, PUNICODE_STRING RecordName,
    ULONG Pid, BOOLEAN AllowMissing)
{
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    InitializeObjectAttributes(&oa, OpenName,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE h = NULL;
    PFILE_OBJECT fo = NULL;
    NTSTATUS status = FltCreateFileEx(semantics_ar_globals.FilterHandle, Instance,
        &h, &fo, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0, IO_IGNORE_SHARE_ACCESS_CHECK);
    if (!NT_SUCCESS(status))
        return AllowMissing ? SEMANTICS_AR_ERROR_NOT_FOUND : SEMANTICS_AR_ERROR_PRESERVE_FAILED;

    semantics_ar_result_t result = preserve_full_from_object(NULL, Instance, fo, RecordName, Pid);

    ObDereferenceObject(fo);
    FltClose(h);

    if (result == SEMANTICS_AR_ERROR_INVALID_PARAM)
        return SEMANTICS_AR_OK;
    return result;
}

semantics_ar_result_t semantics_ar_preserve_stream_full(
    PFLT_CALLBACK_DATA Data,
    PFLT_INSTANCE Instance,
    PFILE_OBJECT FileObject,
    ULONG Pid)
{
    return preserve_full_from_object(Data, Instance, FileObject, NULL, Pid);
}

static VOID capture_worker(PVOID Parameter)
{
    semantics_ar_capture_work_t *work = (semantics_ar_capture_work_t *)Parameter;

    UNICODE_STRING name;
    name.Buffer = work->Name;
    name.Length = work->NameLength;
    name.MaximumLength = work->NameLength;

    preserve_full_via_reopen(work->Instance, &name, &name, work->Pid, FALSE);

    FltObjectDereference(work->Instance);
    ExFreePoolWithTag(work, SEMANTICS_AR_POOL_TAG);
    InterlockedDecrement(&semantics_ar_globals.CaptureWorkerCount);
}

VOID semantics_ar_queue_section_capture(
    PFLT_INSTANCE Instance,
    PUNICODE_STRING NormalizedName,
    ULONG Pid)
{
    if (NormalizedName->Length == 0)
        return;

    SIZE_T size = FIELD_OFFSET(semantics_ar_capture_work_t, Name) + NormalizedName->Length;
    semantics_ar_capture_work_t *work =
        (semantics_ar_capture_work_t *)ExAllocatePoolZero(NonPagedPoolNx, size, SEMANTICS_AR_POOL_TAG);
    if (!work)
        return;

    if (!NT_SUCCESS(FltObjectReference(Instance))) {
        ExFreePoolWithTag(work, SEMANTICS_AR_POOL_TAG);
        return;
    }

    work->Instance = Instance;
    work->Pid = Pid;
    work->NameLength = NormalizedName->Length;
    RtlCopyMemory(work->Name, NormalizedName->Buffer, NormalizedName->Length);

    InterlockedIncrement(&semantics_ar_globals.CaptureWorkerCount);
    ExInitializeWorkItem(&work->WorkItem, capture_worker, work);
    ExQueueWorkItem(&work->WorkItem, DelayedWorkQueue);
}

semantics_ar_result_t semantics_ar_preserve_victim(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    ULONG Pid)
{
    UNREFERENCED_PARAMETER(Data);

    PFLT_FILE_NAME_INFORMATION destInfo = NULL;
    NTSTATUS status = FltGetDestinationFileNameInformation(
        FltObjects->Instance, FltObjects->FileObject, NULL, NULL, 0,
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &destInfo);
    if (!NT_SUCCESS(status))
        return SEMANTICS_AR_ERROR_NOT_FOUND;

    semantics_ar_result_t result = preserve_full_via_reopen(
        FltObjects->Instance, &destInfo->Name, &destInfo->Name, Pid, TRUE);

    FltReleaseFileNameInformation(destInfo);
    return result;
}

FLT_PREOP_CALLBACK_STATUS semantics_ar_preserve_delete(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    ULONG Pid)
{
    semantics_ar_instance_context_t *instCtx = NULL;
    if (!NT_SUCCESS(FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&instCtx)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (!semantics_ar_shadow_ensure_initialized(FltObjects->Instance, instCtx)) {
        FltReleaseContext(instCtx);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    LARGE_INTEGER now;
    KeQuerySystemTimePrecise(&now);
    LONGLONG seq = InterlockedIncrement64(&semantics_ar_globals.ShadowSequence);

    WCHAR targetPath[384];
    if (!NT_SUCCESS(RtlStringCbPrintfW(targetPath, sizeof(targetPath),
            L"%wZ\\deletes\\%08X_%016llX", &instCtx->ShadowBasePath, Pid, (ULONGLONG)seq))) {
        FltReleaseContext(instCtx);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    FILE_STANDARD_INFORMATION si;
    LONGLONG fileBytes = 0;
    if (NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &si, sizeof(si), FileStandardInformation, NULL)))
        fileBytes = semantics_ar_shadow_cluster_align(instCtx, si.EndOfFile.QuadPart);

    PFLT_FILE_NAME_INFORMATION ni = NULL;
    UNICODE_STRING origPath;
    RtlInitUnicodeString(&origPath, NULL);
    if (NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &ni))) {
        FltParseFileNameInformation(ni);
        origPath = ni->Name;
    }

    UNICODE_STRING tps;
    RtlInitUnicodeString(&tps, targetPath);
    ULONG ris = (ULONG)(FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + tps.Length);
    PFILE_RENAME_INFORMATION ri = (PFILE_RENAME_INFORMATION)ExAllocatePoolZero(
        PagedPool, ris, SEMANTICS_AR_POOL_TAG_PG);
    if (!ri) {
        if (ni) FltReleaseFileNameInformation(ni);
        FltReleaseContext(instCtx);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    ri->ReplaceIfExists = TRUE;
    ri->RootDirectory = NULL;
    ri->FileNameLength = tps.Length;
    RtlCopyMemory(ri->FileName, tps.Buffer, tps.Length);

    NTSTATUS status = FltSetInformationFile(FltObjects->Instance, FltObjects->FileObject,
        ri, ris, FileRenameInformation);
    ExFreePoolWithTag(ri, SEMANTICS_AR_POOL_TAG_PG);

    if (!NT_SUCCESS(status)) {
        if (ni) FltReleaseFileNameInformation(ni);
        FltReleaseContext(instCtx);
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    Data->IoStatus.Status = STATUS_SUCCESS;
    Data->IoStatus.Information = 0;

    LARGE_INTEGER zeroOff;
    zeroOff.QuadPart = 0;
    account_shadow_usage(instCtx, Pid, fileBytes);
    semantics_ar_journal_append(FltObjects->Instance, instCtx, &origPath,
        SEMANTICS_AR_JOURNAL_DELETE, Pid, targetPath, tps.Length, zeroOff, 0, now.QuadPart);

    if (ni) FltReleaseFileNameInformation(ni);
    FltReleaseContext(instCtx);
    return FLT_PREOP_COMPLETE;
}