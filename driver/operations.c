#include "driver.h"
#include "seam.h"
#include "state.h"
#include "capture.h"
#include "phantom.h"

extern PSAR_STATE g_sar_state;

_IRQL_requires_max_(APC_LEVEL)
FLT_PREOP_CALLBACK_STATUS SarPreManageBypassIo(_Inout_ PFLT_CALLBACK_DATA Data,
                                               _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                               _Flt_CompletionContext_Outptr_ PVOID *CompletionContext);

_IRQL_requires_max_(APC_LEVEL)
static sar_destruct_member_t SarClassifyWrite(_In_ ULONG IrpFlags)
{
    if (FlagOn(IrpFlags, IRP_PAGING_IO) || FlagOn(IrpFlags, IRP_SYNCHRONOUS_PAGING_IO))
        return SAR_DESTRUCT_WRITE_PAGING;
    if (FlagOn(IrpFlags, IRP_NOCACHE))
        return SAR_DESTRUCT_WRITE_NONCACHED;
    return SAR_DESTRUCT_WRITE_CACHED;
}

_IRQL_requires_max_(APC_LEVEL)
static sar_destruct_member_t SarClassifySetInformation(_In_ FILE_INFORMATION_CLASS InfoClass)
{
    switch (InfoClass) {
    case FileEndOfFileInformation:
        return SAR_DESTRUCT_SET_EOF;
    case FileAllocationInformation:
        return SAR_DESTRUCT_SET_ALLOCATION;
    case FileDispositionInformation:
        return SAR_DESTRUCT_DISPOSITION;
    case FileDispositionInformationEx:
        return SAR_DESTRUCT_DISPOSITION_EX;
    case FileRenameInformation:
        return SAR_DESTRUCT_RENAME;
    case FileRenameInformationEx:
        return SAR_DESTRUCT_RENAME_EX;
    case FileLinkInformation:
        return SAR_DESTRUCT_LINK;
    case FileLinkInformationEx:
        return SAR_DESTRUCT_LINK_EX;
    default:
        return SAR_DESTRUCT_NONE;
    }
}

_IRQL_requires_max_(APC_LEVEL)
static sar_destruct_member_t SarClassifyFsControl(_In_ ULONG FsControlCode)
{
    switch (FsControlCode) {
    case FSCTL_DUPLICATE_EXTENTS_TO_FILE:
        return SAR_DESTRUCT_DUPLICATE_EXTENTS;
    case FSCTL_OFFLOAD_WRITE:
        return SAR_DESTRUCT_OFFLOAD_WRITE;
    case FSCTL_SET_ZERO_DATA:
        return SAR_DESTRUCT_SET_ZERO_DATA;
    case FSCTL_FILE_LEVEL_TRIM:
        return SAR_DESTRUCT_FILE_LEVEL_TRIM;
    case FSCTL_SET_SPARSE:
        return SAR_DESTRUCT_SET_SPARSE;
    case FSCTL_LOCK_VOLUME:
        return SAR_DESTRUCT_LOCK_VOLUME;
    case FSCTL_DISMOUNT_VOLUME:
        return SAR_DESTRUCT_DISMOUNT_VOLUME;
    default:
        return SAR_DESTRUCT_NONE;
    }
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarFsControlIsKeyless(_In_ sar_destruct_member_t Member)
{
    switch (Member) {
    case SAR_DESTRUCT_SET_ZERO_DATA:
    case SAR_DESTRUCT_FILE_LEVEL_TRIM:
    case SAR_DESTRUCT_SET_SPARSE:
    case SAR_DESTRUCT_LOCK_VOLUME:
    case SAR_DESTRUCT_DISMOUNT_VOLUME:
        return TRUE;
    default:
        return FALSE;
    }
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarNameIsOwnStore(_In_ PCUNICODE_STRING Name)
{
    static const WCHAR marker[] = L"SemanticsAr";
    USHORT nchars = (USHORT)(Name->Length / sizeof(WCHAR));
    USHORT mlen = (USHORT)(RTL_NUMBER_OF(marker) - 1);
    USHORT i, j;

    if (nchars < mlen)
        return FALSE;
    for (i = 0; (USHORT)(i + mlen) <= nchars; i++) {
        for (j = 0; j < mlen; j++) {
            WCHAR a = Name->Buffer[i + j];
            WCHAR b = marker[j];
            if (a >= L'a' && a <= L'z') a = (WCHAR)(a - 32);
            if (b >= L'a' && b <= L'z') b = (WCHAR)(b - 32);
            if (a != b)
                break;
        }
        if (j == mlen)
            return TRUE;
    }
    return FALSE;
}

_IRQL_requires_max_(APC_LEVEL)
static PSAR_STREAM_CONTEXT SarGetStreamContext(_In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PSAR_STREAM_CONTEXT context = NULL;
    NTSTATUS status;

    status = FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT *)&context);
    if (NT_SUCCESS(status))
        return context;

    status = FltAllocateContext(FltObjects->Filter, FLT_STREAM_CONTEXT,
                                sizeof(SAR_STREAM_CONTEXT), NonPagedPoolNx,
                                (PFLT_CONTEXT *)&context);
    if (!NT_SUCCESS(status))
        return NULL;

    context->flags = 0;
    context->read_length = 0;

    status = FltSetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                 FLT_SET_CONTEXT_KEEP_IF_EXISTS, context, NULL);
    if (!NT_SUCCESS(status)) {
        FltReleaseContext(context);
        if (status == STATUS_FLT_CONTEXT_ALREADY_DEFINED) {
            status = FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                         (PFLT_CONTEXT *)&context);
            if (NT_SUCCESS(status))
                return context;
        }
        return NULL;
    }
    return context;
}

_IRQL_requires_max_(APC_LEVEL)
static VOID SarSubmitMetadata(_In_ PFLT_CALLBACK_DATA Data,
                              _In_ PCFLT_RELATED_OBJECTS FltObjects,
                              _In_ sar_destruct_member_t Member,
                              _In_ ULONG FsControlCode)
{
    SAR_METADATA_SEAM_REQUEST request;

    request.data = Data;
    request.related = FltObjects;
    request.member = Member;
    request.fs_control_code = FsControlCode;
    SarSeamMetadataSubmit(&request);
}

_IRQL_requires_max_(APC_LEVEL)
static VOID SarSubmitWrite(_In_ PFLT_CALLBACK_DATA Data,
                           _In_ PCFLT_RELATED_OBJECTS FltObjects,
                           _In_ sar_destruct_member_t Member,
                           _In_ ULONG IrpFlags,
                           _In_ LARGE_INTEGER Offset,
                           _In_ ULONG Length)
{
    SAR_WRITE_SEAM_REQUEST request;
    PEPROCESS process;
    PETHREAD thread;

    thread = Data->Thread;
    if (thread == NULL)
        thread = PsGetCurrentThread();
    process = IoThreadToProcess(thread);
    if (process == NULL)
        process = PsGetCurrentProcess();

    ObReferenceObject(thread);
    ObReferenceObject(process);

    RtlZeroMemory(&request, sizeof(request));
    request.data = Data;
    request.related = FltObjects;
    request.originator_process = process;
    request.originator_thread = thread;
    request.member = Member;
    request.irp_flags = IrpFlags;
    request.write_offset = Offset;
    request.write_length = Length;

    {
        PSAR_STREAM_CONTEXT sc = NULL;
        if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                           (PFLT_CONTEXT *)&sc))) {
            ULONG n = (ULONG)sc->read_length;
            if (n > 0 && sc->read_offset == (UINT64)Offset.QuadPart) {
                if (n > SAR_CAPTURE_BUFFER_BYTES)
                    n = SAR_CAPTURE_BUFFER_BYTES;
                RtlCopyMemory(request.pre_image, sc->read_sample, n);
                request.pre_image_len = n;
            }
            FltReleaseContext(sc);
        }
    }

    SarCaptureSubmitWrite(g_sar.capture, &request);
}

FLT_POSTOP_CALLBACK_STATUS
SarPostRead(_Inout_ PFLT_CALLBACK_DATA Data,
            _In_ PCFLT_RELATED_OBJECTS FltObjects,
            _In_opt_ PVOID CompletionContext,
            _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PSAR_STREAM_CONTEXT context;
    PVOID src;
    ULONG got;
    ULONG copy;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->Iopb == NULL || !NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;

    got = (ULONG)Data->IoStatus.Information;
    if (got == 0)
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (Data->Iopb->Parameters.Read.ByteOffset.QuadPart != 0)
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (Data->Iopb->Parameters.Read.MdlAddress != NULL)
        src = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Read.MdlAddress,
                                           NormalPagePriority | MdlMappingNoExecute);
    else
        src = Data->Iopb->Parameters.Read.ReadBuffer;
    if (src == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    context = SarGetStreamContext(FltObjects);
    if (context == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    copy = got < SAR_CAPTURE_BUFFER_BYTES ? got : SAR_CAPTURE_BUFFER_BYTES;
    context->read_length = 0;
    __try {
        RtlCopyMemory(context->read_sample, src, copy);
        context->read_offset = (UINT64)Data->Iopb->Parameters.Read.ByteOffset.QuadPart;
        context->read_length = (LONG)copy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->read_length = 0;
    }

    FltReleaseContext(context);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
SarPostCreate(_Inout_ PFLT_CALLBACK_DATA Data,
              _In_ PCFLT_RELATED_OBJECTS FltObjects,
              _In_opt_ PVOID CompletionContext,
              _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    PFLT_FILE_NAME_INFORMATION info = NULL;
    PSAR_STREAM_CONTEXT context;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
                                              FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
                                              &info)))
        return FLT_POSTOP_FINISHED_PROCESSING;

    if (NT_SUCCESS(FltParseFileNameInformation(info))) {
        if (SarNameIsOwnStore(&info->Name)) {
            context = SarGetStreamContext(FltObjects);
            if (context != NULL) {
                InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_OWN_STORE);
                if (SarPhantomIsPhantomPath(&info->Name))
                    InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_PHANTOM_BACKING);
                FltReleaseContext(context);
            }
        }
    }

    FltReleaseFileNameInformation(info);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarTargetIsProtectedStore(_In_ PCFLT_RELATED_OBJECTS FltObjects,
                                         _In_ PFLT_CALLBACK_DATA Data)
{
    PSAR_STREAM_CONTEXT context = NULL;
    BOOLEAN own = FALSE;

    if (Data->RequestorMode != UserMode)
        return FALSE;

    if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                       (PFLT_CONTEXT *)&context))) {
        own = (context->flags & SAR_STREAMCTX_FLAG_OWN_STORE) != 0;
        FltReleaseContext(context);
    }
    return own;
}

FLT_PREOP_CALLBACK_STATUS
SarPreWrite(_Inout_ PFLT_CALLBACK_DATA Data,
            _In_ PCFLT_RELATED_OBJECTS FltObjects,
            _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    sar_destruct_member_t member;
    LARGE_INTEGER offset;
    ULONG length;
    PETHREAD thread;
    PEPROCESS process;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    thread = Data->Thread;
    if (thread == NULL)
        thread = PsGetCurrentThread();
    process = IoThreadToProcess(thread);
    if (process == NULL)
        process = PsGetCurrentProcess();

    if (SarCaptureOriginatorBlocked(g_sar.capture, process)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    {
        PSAR_STREAM_CONTEXT sc = NULL;
        BOOLEAN phantomBacking = FALSE;
        if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                           (PFLT_CONTEXT *)&sc))) {
            phantomBacking = (sc->flags & SAR_STREAMCTX_FLAG_PHANTOM_BACKING) != 0;
            FltReleaseContext(sc);
        }
        if (phantomBacking) {
            if (Data->RequestorMode == UserMode) {
                HANDLE ppid = PsGetProcessId(process);
                SarPhantomRecordEvidence(ppid);
                if (SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE &&
                    SarPhantomIsConvicted(ppid)) {
                    SarCaptureBlockOriginator(g_sar.capture, process);
                    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                    Data->IoStatus.Information = 0;
                    return FLT_PREOP_COMPLETE;
                }
            }
            return FLT_PREOP_SUCCESS_NO_CALLBACK;
        }
    }

    if (SarTargetIsProtectedStore(FltObjects, Data)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    member = SarClassifyWrite(Data->Iopb->IrpFlags);

    offset = Data->Iopb->Parameters.Write.ByteOffset;
    length = Data->Iopb->Parameters.Write.Length;

    SarSubmitWrite(Data, FltObjects, member, Data->Iopb->IrpFlags, offset, length);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarStreamWasRead(_In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PSAR_STREAM_CONTEXT context = NULL;
    BOOLEAN was = FALSE;

    if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                       (PFLT_CONTEXT *)&context))) {
        was = context->read_length > 0;
        FltReleaseContext(context);
    }
    return was;
}

_IRQL_requires_max_(APC_LEVEL)
static UINT64 SarQueryEndOfFile(_In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    FILE_STANDARD_INFORMATION fsi;

    if (NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
                                           &fsi, sizeof(fsi), FileStandardInformation, NULL)))
        return (UINT64)fsi.EndOfFile.QuadPart;
    return 0;
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarDestructionRegion(_In_ PFLT_CALLBACK_DATA Data,
                                    _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                    _In_ sar_destruct_member_t Member,
                                    _Out_ PUINT64 Offset, _Out_ PUINT64 Length)
{
    PVOID buf = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

    *Offset = 0;
    *Length = 0;
    if (buf == NULL)
        return FALSE;

    switch (Member) {
    case SAR_DESTRUCT_DISPOSITION:
        if (!((FILE_DISPOSITION_INFORMATION *)buf)->DeleteFile)
            return FALSE;
        *Length = SarQueryEndOfFile(FltObjects);
        return *Length > 0;
    case SAR_DESTRUCT_DISPOSITION_EX:
        if (!(((FILE_DISPOSITION_INFORMATION_EX *)buf)->Flags & FILE_DISPOSITION_DELETE))
            return FALSE;
        *Length = SarQueryEndOfFile(FltObjects);
        return *Length > 0;
    case SAR_DESTRUCT_SET_EOF: {
        UINT64 cur = SarQueryEndOfFile(FltObjects);
        UINT64 neweof = (UINT64)((FILE_END_OF_FILE_INFORMATION *)buf)->EndOfFile.QuadPart;
        if (neweof >= cur)
            return FALSE;
        *Offset = neweof;
        *Length = cur - neweof;
        return TRUE;
    }
    case SAR_DESTRUCT_SET_ALLOCATION: {
        UINT64 cur = SarQueryEndOfFile(FltObjects);
        UINT64 newalloc = (UINT64)((FILE_ALLOCATION_INFORMATION *)buf)->AllocationSize.QuadPart;
        if (newalloc >= cur)
            return FALSE;
        *Offset = newalloc;
        *Length = cur - newalloc;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

FLT_PREOP_CALLBACK_STATUS
SarPreSetInformation(_Inout_ PFLT_CALLBACK_DATA Data,
                     _In_ PCFLT_RELATED_OBJECTS FltObjects,
                     _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    sar_destruct_member_t member;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    member = SarClassifySetInformation(Data->Iopb->Parameters.SetFileInformation.FileInformationClass);
    if (member == SAR_DESTRUCT_NONE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    {
        PETHREAD thread = Data->Thread;
        PEPROCESS process;
        if (thread == NULL)
            thread = PsGetCurrentThread();
        process = IoThreadToProcess(thread);
        if (process == NULL)
            process = PsGetCurrentProcess();
        if (SarCaptureOriginatorBlocked(g_sar.capture, process)) {
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
    }

    if (SarTargetIsProtectedStore(FltObjects, Data)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    if ((member == SAR_DESTRUCT_DISPOSITION || member == SAR_DESTRUCT_DISPOSITION_EX ||
         member == SAR_DESTRUCT_SET_EOF || member == SAR_DESTRUCT_SET_ALLOCATION) &&
        KeGetCurrentIrql() == PASSIVE_LEVEL && SarStreamWasRead(FltObjects)) {
        UINT64 off;
        UINT64 len;
        if (SarDestructionRegion(Data, FltObjects, member, &off, &len)) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            SarCaptureSubmitDestruction(g_sar.capture, Data, FltObjects, process, thread, off, len);
        }
    } else if ((member == SAR_DESTRUCT_RENAME || member == SAR_DESTRUCT_RENAME_EX ||
                member == SAR_DESTRUCT_LINK || member == SAR_DESTRUCT_LINK_EX) &&
               KeGetCurrentIrql() == PASSIVE_LEVEL) {
        PETHREAD thread = Data->Thread;
        PEPROCESS process;
        if (thread == NULL)
            thread = PsGetCurrentThread();
        process = IoThreadToProcess(thread);
        if (process == NULL)
            process = PsGetCurrentProcess();
        SarCaptureSubmitRenameTarget(g_sar.capture, Data, FltObjects, process, thread);
    }

    SarSubmitMetadata(Data, FltObjects, member, 0);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
SarPreFsControl(_Inout_ PFLT_CALLBACK_DATA Data,
                _In_ PCFLT_RELATED_OBJECTS FltObjects,
                _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    sar_destruct_member_t member;
    ULONG control_code;
    LARGE_INTEGER offset;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    control_code = Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;

    if (control_code == FSCTL_MANAGE_BYPASS_IO)
        return SarPreManageBypassIo(Data, FltObjects, CompletionContext);

    if (control_code == FSCTL_GET_NTFS_FILE_RECORD && SarPhantomActive(g_sar.phantom) &&
        Data->RequestorMode != KernelMode) {
        PETHREAD thr = Data->Thread;
        if (thr == NULL) thr = PsGetCurrentThread();
        PEPROCESS proc = IoThreadToProcess(thr);
        if (proc == NULL) proc = PsGetCurrentProcess();
        HANDLE pid = PsGetProcessId(proc);
        if (!SarPhantomIsTrustedProcess(pid)) {
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
    }

    member = SarClassifyFsControl(control_code);
    if (member == SAR_DESTRUCT_NONE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (SarTargetIsProtectedStore(FltObjects, Data)) {
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
    }

    if (SarFsControlIsKeyless(member)) {
        if (member == SAR_DESTRUCT_SET_ZERO_DATA && KeGetCurrentIrql() == PASSIVE_LEVEL &&
            SarStreamWasRead(FltObjects)) {
            PFILE_ZERO_DATA_INFORMATION z = (PFILE_ZERO_DATA_INFORMATION)
                Data->Iopb->Parameters.FileSystemControl.Buffered.SystemBuffer;
            ULONG inlen = Data->Iopb->Parameters.FileSystemControl.Buffered.InputBufferLength;
            if (z != NULL && inlen >= sizeof(*z) &&
                z->BeyondFinalZero.QuadPart > z->FileOffset.QuadPart) {
                PETHREAD thread = Data->Thread;
                PEPROCESS process;
                if (thread == NULL)
                    thread = PsGetCurrentThread();
                process = IoThreadToProcess(thread);
                if (process == NULL)
                    process = PsGetCurrentProcess();
                SarCaptureSubmitDestruction(
                    g_sar.capture, Data, FltObjects, process, thread,
                    (UINT64)z->FileOffset.QuadPart,
                    (UINT64)(z->BeyondFinalZero.QuadPart - z->FileOffset.QuadPart));
            }
        } else if (member == SAR_DESTRUCT_FILE_LEVEL_TRIM && KeGetCurrentIrql() == PASSIVE_LEVEL &&
                   SarStreamWasRead(FltObjects)) {
            PFILE_LEVEL_TRIM t = (PFILE_LEVEL_TRIM)
                Data->Iopb->Parameters.FileSystemControl.Buffered.SystemBuffer;
            ULONG inlen = Data->Iopb->Parameters.FileSystemControl.Buffered.InputBufferLength;
            if (t != NULL && inlen >= FIELD_OFFSET(FILE_LEVEL_TRIM, Ranges)) {
                ULONG maxn = (inlen - FIELD_OFFSET(FILE_LEVEL_TRIM, Ranges)) /
                             sizeof(FILE_LEVEL_TRIM_RANGE);
                ULONG n = t->NumRanges < maxn ? t->NumRanges : maxn;
                ULONG r;
                PETHREAD thread = Data->Thread;
                PEPROCESS process;
                if (thread == NULL)
                    thread = PsGetCurrentThread();
                process = IoThreadToProcess(thread);
                if (process == NULL)
                    process = PsGetCurrentProcess();
                for (r = 0; r < n; r++) {
                    if (t->Ranges[r].Length > 0)
                        SarCaptureSubmitDestruction(g_sar.capture, Data, FltObjects, process, thread,
                                                    (UINT64)t->Ranges[r].Offset,
                                                    (UINT64)t->Ranges[r].Length);
                }
            }
        }
        SarSubmitMetadata(Data, FltObjects, member, control_code);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    if ((member == SAR_DESTRUCT_DUPLICATE_EXTENTS || member == SAR_DESTRUCT_OFFLOAD_WRITE) &&
        KeGetCurrentIrql() == PASSIVE_LEVEL && SarStreamWasRead(FltObjects)) {
        PVOID inbuf = Data->Iopb->Parameters.FileSystemControl.Buffered.SystemBuffer;
        ULONG inlen = Data->Iopb->Parameters.FileSystemControl.Buffered.InputBufferLength;
        UINT64 doff = 0;
        UINT64 dlen = 0;
        if (member == SAR_DESTRUCT_DUPLICATE_EXTENTS) {
            PDUPLICATE_EXTENTS_DATA d = (PDUPLICATE_EXTENTS_DATA)inbuf;
            if (d != NULL && inlen >= sizeof(*d)) {
                doff = (UINT64)d->TargetFileOffset.QuadPart;
                dlen = (UINT64)d->ByteCount.QuadPart;
            }
        } else {
            PFSCTL_OFFLOAD_WRITE_INPUT o = (PFSCTL_OFFLOAD_WRITE_INPUT)inbuf;
            if (o != NULL && inlen >= sizeof(*o)) {
                doff = (UINT64)o->FileOffset;
                dlen = (UINT64)o->CopyLength;
            }
        }
        if (dlen > 0) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            SarCaptureSubmitDestruction(g_sar.capture, Data, FltObjects, process, thread, doff, dlen);
        }
    }

    offset.QuadPart = 0;
    SarSubmitWrite(Data, FltObjects, member, Data->Iopb->IrpFlags, offset, 0);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
SarPreAcquireForSection(_Inout_ PFLT_CALLBACK_DATA Data,
                        _In_ PCFLT_RELATED_OBJECTS FltObjects,
                        _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    PSAR_STREAM_CONTEXT context;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb->Parameters.AcquireForSectionSynchronization.PageProtection == PAGE_READONLY)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    context = SarGetStreamContext(FltObjects);
    if (context != NULL) {
        InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_SECTION_DIRTY);
        SarSubmitMetadata(Data, FltObjects, SAR_DESTRUCT_SECTION_SYNC, 0);
        FltReleaseContext(context);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
