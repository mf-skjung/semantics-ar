#include "driver_internal.h"
#include "preserve.h"

static FLT_PREOP_CALLBACK_STATUS deny_confirmed(PFLT_CALLBACK_DATA Data, ULONG Pid)
{
    if (semantics_ar_globals.ConfirmedActive && semantics_ar_is_confirmed_process(Pid)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static FLT_PREOP_CALLBACK_STATUS handle_zero_data(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid)
{
    if (Data->Iopb->Parameters.FileSystemControl.Buffered.InputBufferLength 
            sizeof(FILE_ZERO_DATA_INFORMATION))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    PFILE_ZERO_DATA_INFORMATION zi =
        (PFILE_ZERO_DATA_INFORMATION)Data->Iopb->Parameters.FileSystemControl.Buffered.SystemBuffer;
    LONGLONG zoff = zi->FileOffset.QuadPart;
    LONGLONG zbey = zi->BeyondFinalZero.QuadPart;
    if (zbey <= zoff)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    FILE_STANDARD_INFORMATION si;
    if (!NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &si, sizeof(si), FileStandardInformation, NULL)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (zoff >= si.EndOfFile.QuadPart)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    LONGLONG pend = (zbey > si.EndOfFile.QuadPart) ? si.EndOfFile.QuadPart : zbey;
    LONGLONG span = pend - zoff;
    ULONG plen = (span > SEMANTICS_AR_TRUNCATION_MAX_BYTES)
        ? SEMANTICS_AR_TRUNCATION_MAX_BYTES : (ULONG)span;

    LARGE_INTEGER wo;
    wo.QuadPart = zoff;
    semantics_ar_result_t pres = semantics_ar_preserve_range(Data,
        FltObjects->Instance, FltObjects->FileObject, NULL,
        wo, plen, si.EndOfFile.QuadPart, SEMANTICS_AR_JOURNAL_OVERWRITE, Pid);
    if (pres != SEMANTICS_AR_OK) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

static FLT_PREOP_CALLBACK_STATUS handle_bulk_destroyer(
    PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, ULONG Pid)
{
    semantics_ar_result_t pres = semantics_ar_preserve_stream_full(Data,
        FltObjects->Instance, FltObjects->FileObject, Pid);
    if (pres != SEMANTICS_AR_OK && pres != SEMANTICS_AR_ERROR_INVALID_PARAM) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_fsctl(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID *CompletionContext)
{
    *CompletionContext = NULL;
    if (!FLT_IS_IRP_OPERATION(Data))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (Data->Iopb->MinorFunction != IRP_MN_USER_FS_REQUEST)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG code = Data->Iopb->Parameters.FileSystemControl.Buffered.FsControlCode;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (pid == 0 || pid == 4)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (semantics_ar_globals.ServiceProcessId != 0 &&
        pid == semantics_ar_globals.ServiceProcessId)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    switch (code) {
    case FSCTL_SET_ZERO_DATA: {
        FLT_PREOP_CALLBACK_STATUS d = deny_confirmed(Data, pid);
        if (d == FLT_PREOP_COMPLETE) return d;
        return handle_zero_data(Data, FltObjects, pid);
    }
    case FSCTL_FILE_LEVEL_TRIM:
    case FSCTL_SET_SPARSE: {
        FLT_PREOP_CALLBACK_STATUS d = deny_confirmed(Data, pid);
        if (d == FLT_PREOP_COMPLETE) return d;
        return handle_bulk_destroyer(Data, FltObjects, pid);
    }
    case FSCTL_LOCK_VOLUME:
    case FSCTL_DISMOUNT_VOLUME:
        if (semantics_ar_globals.ConfirmedActive && semantics_ar_is_confirmed_process(pid)) {
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    default:
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
}