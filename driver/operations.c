#include "driver.h"
#include "seam.h"
#include "state.h"
#include "capture.h"
#include "phantom.h"
#include "preserve.h"

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
    context->cap_ranges = NULL;
    context->cap_count = 0;
    context->cap_capacity = 0;
    context->mmap_map = NULL;
    context->mmap_path = NULL;
    context->mmap_arm_pid = NULL;
    FltInitializePushLock(&context->cap_lock);

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
static BOOLEAN SarStreamConfidentBlind(_In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PSAR_STREAM_CONTEXT sc = NULL;
    BOOLEAN blind = FALSE;

    if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                       (PFLT_CONTEXT *)&sc))) {
        LONG f = sc->flags;
        blind = (f & SAR_STREAMCTX_FLAG_TRACKED_FROM_OPEN) != 0 &&
                (f & SAR_STREAMCTX_FLAG_READ_OBSERVED) == 0 &&
                (f & SAR_STREAMCTX_FLAG_SECTION_DIRTY) == 0;
        FltReleaseContext(sc);
    }
    return blind;
}

_IRQL_requires_max_(APC_LEVEL)
static BOOLEAN SarStreamSectionDirty(_In_ PCFLT_RELATED_OBJECTS FltObjects)
{
    PSAR_STREAM_CONTEXT sc = NULL;
    BOOLEAN dirty = FALSE;

    if (NT_SUCCESS(FltGetStreamContext(FltObjects->Instance, FltObjects->FileObject,
                                       (PFLT_CONTEXT *)&sc))) {
        dirty = (sc->flags & SAR_STREAMCTX_FLAG_SECTION_DIRTY) != 0;
        FltReleaseContext(sc);
    }
    return dirty;
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

    if (SarStreamConfidentBlind(FltObjects))
        return;

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

    context = SarGetStreamContext(FltObjects);
    if (context == NULL)
        return FLT_POSTOP_FINISHED_PROCESSING;

    InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_READ_OBSERVED);

    if (Data->Iopb->Parameters.Read.ByteOffset.QuadPart == 0) {
        if (Data->Iopb->Parameters.Read.MdlAddress != NULL)
            src = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Read.MdlAddress,
                                               NormalPagePriority | MdlMappingNoExecute);
        else
            src = Data->Iopb->Parameters.Read.ReadBuffer;
        if (src != NULL) {
            copy = got < SAR_CAPTURE_BUFFER_BYTES ? got : SAR_CAPTURE_BUFFER_BYTES;
            context->read_length = 0;
            __try {
                RtlCopyMemory(context->read_sample, src, copy);
                context->read_offset = (UINT64)Data->Iopb->Parameters.Read.ByteOffset.QuadPart;
                context->read_length = (LONG)copy;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                context->read_length = 0;
            }
        }
    }

    FltReleaseContext(context);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS
SarPreCreate(_Inout_ PFLT_CALLBACK_DATA Data,
             _In_ PCFLT_RELATED_OBJECTS FltObjects,
             _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    if (Data->Iopb != NULL && Data->RequestorMode == UserMode &&
        KeGetCurrentIrql() == PASSIVE_LEVEL) {
        ULONG disp = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
        if ((disp == FILE_SUPERSEDE || disp == FILE_OVERWRITE || disp == FILE_OVERWRITE_IF) &&
            !FlagOn(Data->Iopb->Parameters.Create.Options, FILE_DIRECTORY_FILE)) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            if (SarStateIdentityLookup(g_sar_state, PsGetProcessId(process)) != SAR_IDSTATE_EXEMPT)
                SarCaptureSubmitSupersede(g_sar.capture, Data, FltObjects, process, thread);
        }
    }

    return SarPhantomPreCreate(Data, FltObjects, CompletionContext);
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
        } else if (Data->RequestorMode == UserMode &&
                   Data->IoStatus.Information == FILE_OPENED &&
                   Data->Iopb->Parameters.Create.SecurityContext != NULL &&
                   FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess,
                          FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE) &&
                   !FlagOn(Data->Iopb->Parameters.Create.Options, FILE_DIRECTORY_FILE)) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            if (SarStateIdentityLookup(g_sar_state, PsGetProcessId(process)) != SAR_IDSTATE_EXEMPT) {
                context = SarGetStreamContext(FltObjects);
                if (context != NULL) {
                    InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_TRACKED_FROM_OPEN);
                    FltReleaseContext(context);
                }
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

    if (SarMmapOnPagingWrite(g_sar.capture, FltObjects, Data))
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
                    SarCaptureBlockOriginator(g_sar.capture, process, SAR_EVENT_CLASS_BLOCK_PHANTOM);
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

    if (KeGetCurrentIrql() == PASSIVE_LEVEL &&
        (Data->Iopb->IrpFlags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO)) == 0 &&
        IoGetTopLevelIrp() == NULL && length > 0) {
        if (SarStreamSectionDirty(FltObjects)) {
            if (member == SAR_DESTRUCT_WRITE_NONCACHED)
                SarMmapCaptureNocache(g_sar.capture, FltObjects, Data, process);
        } else if (!SarStreamConfidentBlind(FltObjects)) {
            SarCaptureInPlaceRegion(g_sar.capture, Data, FltObjects, process,
                                    (UINT64)offset.QuadPart, length);
        }
    }

    SarSubmitWrite(Data, FltObjects, member, Data->Iopb->IrpFlags, offset, length);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
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

    if ((member == SAR_DESTRUCT_RENAME || member == SAR_DESTRUCT_RENAME_EX ||
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
        if (member == SAR_DESTRUCT_RENAME || member == SAR_DESTRUCT_RENAME_EX)
            SarCaptureSubmitRenameRekey(g_sar.capture, Data, FltObjects);
    }

    if ((member == SAR_DESTRUCT_DISPOSITION || member == SAR_DESTRUCT_DISPOSITION_EX) &&
        KeGetCurrentIrql() == PASSIVE_LEVEL &&
        Data->Iopb->Parameters.SetFileInformation.InfoBuffer != NULL) {
        PVOID ib = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
        BOOLEAN deleting;
        if (member == SAR_DESTRUCT_DISPOSITION_EX)
            deleting = (((PFILE_DISPOSITION_INFORMATION_EX)ib)->Flags & FILE_DISPOSITION_DELETE) != 0;
        else
            deleting = ((PFILE_DISPOSITION_INFORMATION)ib)->DeleteFile != 0;
        if (deleting) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            SarCaptureSubmitDelete(g_sar.capture, Data, FltObjects, process, thread);
        }
    }

    if ((member == SAR_DESTRUCT_SET_EOF || member == SAR_DESTRUCT_SET_ALLOCATION) &&
        KeGetCurrentIrql() == PASSIVE_LEVEL &&
        Data->Iopb->Parameters.SetFileInformation.InfoBuffer != NULL &&
        !SarStreamConfidentBlind(FltObjects)) {
        FILE_STANDARD_INFORMATION fsi;
        LARGE_INTEGER newEof;
        PVOID ib = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

        if (member == SAR_DESTRUCT_SET_EOF)
            newEof = ((PFILE_END_OF_FILE_INFORMATION)ib)->EndOfFile;
        else
            newEof = ((PFILE_ALLOCATION_INFORMATION)ib)->AllocationSize;

        RtlZeroMemory(&fsi, sizeof(fsi));
        if (NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject, &fsi,
                                               sizeof(fsi), FileStandardInformation, NULL)) &&
            (UINT64)newEof.QuadPart < (UINT64)fsi.EndOfFile.QuadPart) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            SarCaptureInPlaceRegion(g_sar.capture, Data, FltObjects, process,
                                    (UINT64)newEof.QuadPart,
                                    (UINT64)fsi.EndOfFile.QuadPart - (UINT64)newEof.QuadPart);
        }
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

    if (control_code == FSCTL_SET_ZERO_DATA && KeGetCurrentIrql() == PASSIVE_LEVEL &&
        !SarStreamConfidentBlind(FltObjects)) {
        PFILE_ZERO_DATA_INFORMATION z =
            (PFILE_ZERO_DATA_INFORMATION)Data->Iopb->Parameters.FileSystemControl.Buffered.SystemBuffer;
        ULONG inlen = Data->Iopb->Parameters.FileSystemControl.Buffered.InputBufferLength;
        if (z != NULL && inlen >= sizeof(FILE_ZERO_DATA_INFORMATION) &&
            (UINT64)z->BeyondFinalZero.QuadPart > (UINT64)z->FileOffset.QuadPart) {
            PETHREAD thread = Data->Thread;
            PEPROCESS process;
            if (thread == NULL)
                thread = PsGetCurrentThread();
            process = IoThreadToProcess(thread);
            if (process == NULL)
                process = PsGetCurrentProcess();
            SarCaptureInPlaceRegion(g_sar.capture, Data, FltObjects, process,
                                    (UINT64)z->FileOffset.QuadPart,
                                    (UINT64)(z->BeyondFinalZero.QuadPart - z->FileOffset.QuadPart));
        }
    }

    if (SarFsControlIsKeyless(member)) {
        SarSubmitMetadata(Data, FltObjects, member, control_code);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
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
    ULONG prot;
    BOOLEAN rw;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb->Parameters.AcquireForSectionSynchronization.SyncType != SyncTypeCreateSection)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    prot = Data->Iopb->Parameters.AcquireForSectionSynchronization.PageProtection;
    rw = (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE);

    context = SarGetStreamContext(FltObjects);
    if (context == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_READ_OBSERVED);

    if (rw) {
        PETHREAD thread = Data->Thread;
        PEPROCESS process;
        HANDLE pid;

        if (thread == NULL)
            thread = PsGetCurrentThread();
        process = IoThreadToProcess(thread);
        if (process == NULL)
            process = PsGetCurrentProcess();
        pid = PsGetProcessId(process);

        InterlockedOr(&context->flags, (LONG)SAR_STREAMCTX_FLAG_SECTION_DIRTY);

        if (KeGetCurrentIrql() == PASSIVE_LEVEL &&
            SarStateModeGet(g_sar_state) == SEMANTICS_AR_MODE_ENFORCE &&
            SarStateIdentityLookup(g_sar_state, pid) != SAR_IDSTATE_EXEMPT) {
            UINT64 est = SAR_CAPTURE_BUFFER_BYTES;
            PFSRTL_ADVANCED_FCB_HEADER hdr =
                (PFSRTL_ADVANCED_FCB_HEADER)FltObjects->FileObject->FsContext;
            if (hdr != NULL && (UINT64)hdr->FileSize.QuadPart > est)
                est = (UINT64)hdr->FileSize.QuadPart;
            if (SarPreserveWouldExceed(g_sar.preserve, est)) {
                SarCaptureBlockOriginator(g_sar.capture, process, SAR_EVENT_CLASS_BLOCK_CAPACITY);
                FltReleaseContext(context);
                Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                Data->IoStatus.Information = 0;
                return FLT_PREOP_COMPLETE;
            }
        }

        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            SarMmapArm(FltObjects->Instance, FltObjects->FileObject, context, pid);
        SarSubmitMetadata(Data, FltObjects, SAR_DESTRUCT_SECTION_SYNC, 0);
    }

    FltReleaseContext(context);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
