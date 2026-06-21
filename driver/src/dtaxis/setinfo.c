#include "driver_internal.h"
#include "preserve.h"

static FLT_PREOP_CALLBACK_STATUS handle_disposition(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid,
    FILE_INFORMATION_CLASS ic)
{
    PVOID ib = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
    BOOLEAN isDelete;
    if (ic == FileDispositionInformation)
        isDelete = ((PFILE_DISPOSITION_INFORMATION)ib)->DeleteFile;
    else
        isDelete = (*((PULONG)ib) & FILE_DISPOSITION_FLAG_DELETE) != 0;
    if (!isDelete)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    FILE_INTERNAL_INFORMATION ii;
    if (NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &ii, sizeof(ii), FileInternalInformation, NULL))) {
        UINT64 fid = (UINT64)ii.IndexNumber.QuadPart;
        if (fid != (UINT64)-1 && fid != 0 && semantics_ar_file_self_created(Pid, fid))
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    return semantics_ar_preserve_delete(Data, FltObjects, Pid);
}

static FLT_PREOP_CALLBACK_STATUS handle_truncate(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid,
    LONGLONG NewEof)
{
    FILE_STANDARD_INFORMATION si;
    if (!NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &si, sizeof(si), FileStandardInformation, NULL)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (NewEof >= si.EndOfFile.QuadPart)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    LARGE_INTEGER wo;
    wo.QuadPart = NewEof;
    LONGLONG span = si.EndOfFile.QuadPart - NewEof;
    ULONG tlen = (span > SEMANTICS_AR_TRUNCATION_MAX_BYTES)
        ? SEMANTICS_AR_TRUNCATION_MAX_BYTES : (ULONG)span;

    semantics_ar_result_t pres = semantics_ar_preserve_range(Data,
        FltObjects->Instance, FltObjects->FileObject, NULL,
        wo, tlen, si.EndOfFile.QuadPart, SEMANTICS_AR_JOURNAL_TRUNCATE, Pid);
    if (pres != SEMANTICS_AR_OK) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static FLT_PREOP_CALLBACK_STATUS handle_alloc_shrink(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid)
{
    PFILE_ALLOCATION_INFORMATION ai =
        (PFILE_ALLOCATION_INFORMATION)Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
    return handle_truncate(Data, FltObjects, Pid, ai->AllocationSize.QuadPart);
}

static FLT_PREOP_CALLBACK_STATUS handle_rename_or_link(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid)
{
    semantics_ar_result_t pres = semantics_ar_preserve_victim(Data, FltObjects, Pid);
    if (pres == SEMANTICS_AR_ERROR_NOT_FOUND)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (pres != SEMANTICS_AR_OK) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_set_information(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID *CompletionContext)
{
    *CompletionContext = NULL;
    if (!FLT_IS_IRP_OPERATION(Data))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (pid == 0 || pid == 4)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (semantics_ar_globals.ServiceProcessId != 0 &&
        pid == semantics_ar_globals.ServiceProcessId)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (semantics_ar_globals.ConfirmedActive && semantics_ar_is_confirmed_process(pid)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    FILE_INFORMATION_CLASS ic = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;

    if (ic == FileDispositionInformation || ic == FileDispositionInformationEx)
        return handle_disposition(Data, FltObjects, pid, ic);

    if (ic == FileEndOfFileInformation) {
        if (Data->Iopb->Parameters.SetFileInformation.AdvanceOnly)
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        PFILE_END_OF_FILE_INFORMATION eof =
            (PFILE_END_OF_FILE_INFORMATION)Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
        return handle_truncate(Data, FltObjects, pid, eof->EndOfFile.QuadPart);
    }

    if (ic == FileAllocationInformation)
        return handle_alloc_shrink(Data, FltObjects, pid);

    if (ic == FileRenameInformation || ic == FileRenameInformationEx ||
        ic == FileLinkInformation || ic == FileLinkInformationEx)
        return handle_rename_or_link(Data, FltObjects, pid);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}