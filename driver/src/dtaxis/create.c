#include "driver_internal.h"

FLT_PREOP_CALLBACK_STATUS semantics_ar_pre_create(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID *CompletionContext)
{
    *CompletionContext = NULL;
    if (!FLT_IS_IRP_OPERATION(Data))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (semantics_ar_globals.ServiceProcessId != 0 &&
        pid == semantics_ar_globals.ServiceProcessId)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (semantics_ar_globals.ConfirmedActive) {
        ULONG disp = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;
        if (disp == FILE_SUPERSEDE || disp == FILE_OVERWRITE || disp == FILE_OVERWRITE_IF) {
            if (semantics_ar_is_confirmed_process(pid)) {
                Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                Data->IoStatus.Information = 0;
                return FLT_PREOP_COMPLETE;
            }
        }
    }

    if (semantics_ar_globals.ServiceProcessId == 0)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    semantics_ar_instance_context_t *instCtx = NULL;
    if (!NT_SUCCESS(FltGetInstanceContext(FltObjects->Instance, (PFLT_CONTEXT *)&instCtx)))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (instCtx->ShadowInitialized != 1) {
        FltReleaseContext(instCtx);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    PFLT_FILE_NAME_INFORMATION ni = NULL;
    if (!NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &ni))) {
        FltReleaseContext(instCtx);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    FltParseFileNameInformation(ni);

    BOOLEAN isShadow = FALSE;
    if (RtlPrefixUnicodeString(&instCtx->ShadowBasePath, &ni->Name, TRUE)) {
        USHORT pc = instCtx->ShadowBasePath.Length / sizeof(WCHAR);
        if (ni->Name.Length == instCtx->ShadowBasePath.Length ||
            ni->Name.Buffer[pc] == L'\\' || ni->Name.Buffer[pc] == L':')
            isShadow = TRUE;
    }
    FltReleaseFileNameInformation(ni);
    FltReleaseContext(instCtx);

    if (!isShadow)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    if (semantics_ar_is_trusted_pid(pid))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
    Data->IoStatus.Information = 0;
    return FLT_PREOP_COMPLETE;
}

FLT_POSTOP_CALLBACK_STATUS semantics_ar_post_create(
    PFLT_CALLBACK_DATA Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID CompletionContext,
    FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (!NT_SUCCESS(Data->IoStatus.Status))
        return FLT_POSTOP_FINISHED_PROCESSING;
    if (Data->IoStatus.Information != FILE_CREATED)
        return FLT_POSTOP_FINISHED_PROCESSING;

    ULONG pid = FltGetRequestorProcessId(Data);
    if (pid == 0 || pid == 4)
        return FLT_POSTOP_FINISHED_PROCESSING;

    FILE_INTERNAL_INFORMATION ii;
    UINT64 fileId = 0;
    if (NT_SUCCESS(FltQueryInformationFile(FltObjects->Instance, FltObjects->FileObject,
            &ii, sizeof(ii), FileInternalInformation, NULL))) {
        fileId = (UINT64)ii.IndexNumber.QuadPart;
        if (fileId == (UINT64)-1)
            fileId = 0;
    }

    if (fileId != 0 && semantics_ar_globals.CreateRingEntries) {
        ULONG slot = (ULONG)(InterlockedIncrement(&semantics_ar_globals.CreateRingCursor) - 1)
            & (SEMANTICS_AR_CREATE_RING_SIZE - 1);
        semantics_ar_create_ring_entry_t *e = &semantics_ar_globals.CreateRingEntries[slot];
        e->FileId = fileId;
        KeQuerySystemTimePrecise(&e->Timestamp);
        MemoryBarrier();
        e->Pid = pid;
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}