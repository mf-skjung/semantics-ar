#include "driver_internal.h"
#include "preserve.h"

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_acquire_section(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID *CompletionContext)
{
    *CompletionContext = NULL;

    if (Data->Iopb->Parameters.AcquireForSectionSynchronization.SyncType != SyncTypeCreateSection)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG pp = Data->Iopb->Parameters.AcquireForSectionSynchronization.PageProtection;
    BOOLEAN realWrite = (pp & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
    if (!realWrite)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (pid == 0 || pid == 4)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (semantics_ar_globals.ServiceProcessId != 0 &&
        pid == semantics_ar_globals.ServiceProcessId)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    semantics_ar_stream_context_t *ctx = NULL;
    if (!NT_SUCCESS(semantics_ar_get_or_create_stream_context(FltObjects, &ctx)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    LONG prev = InterlockedOr(&ctx->Flags,
        SEMANTICS_AR_CTX_HAS_WRITABLE_SECTION | SEMANTICS_AR_CTX_CAPTURE_QUEUED);
    FltReleaseContext(ctx);

    if (prev & SEMANTICS_AR_CTX_CAPTURE_QUEUED)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    PFLT_FILE_NAME_INFORMATION ni = NULL;
    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &ni)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    FltParseFileNameInformation(ni);

    semantics_ar_queue_section_capture(FltObjects->Instance, &ni->Name, pid);

    FltReleaseFileNameInformation(ni);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}