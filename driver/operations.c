#include "driver.h"
#include "seam.h"
#include "state.h"

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
    request.stream_context = SarGetStreamContext(FltObjects);
    request.member = Member;
    request.irp_flags = IrpFlags;
    request.write_offset = Offset;
    request.write_length = Length;
    request.capture_buffer = NULL;
    KeInitializeEvent(&request.continuation.register_grab_done, NotificationEvent, FALSE);
    request.continuation.deferred_work = NULL;
    request.continuation.deferred_queued = FALSE;

    SarSeamWriteSubmit(&request);

    if (request.stream_context != NULL)
        FltReleaseContext(request.stream_context);
    SarSeamWriteRelease(&request);
}

FLT_PREOP_CALLBACK_STATUS
SarPreWrite(_Inout_ PFLT_CALLBACK_DATA Data,
            _In_ PCFLT_RELATED_OBJECTS FltObjects,
            _Flt_CompletionContext_Outptr_ PVOID *CompletionContext)
{
    sar_destruct_member_t member;
    LARGE_INTEGER offset;
    ULONG length;

    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->Iopb == NULL)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    member = SarClassifyWrite(Data->Iopb->IrpFlags);

    offset = Data->Iopb->Parameters.Write.ByteOffset;
    length = Data->Iopb->Parameters.Write.Length;

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

    member = SarClassifyFsControl(control_code);
    if (member == SAR_DESTRUCT_NONE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

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
