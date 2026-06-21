#include "driver_internal.h"

VOID semantics_ar_stream_context_cleanup(PFLT_CONTEXT Context, FLT_CONTEXT_TYPE ContextType)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextType);
}

NTSTATUS semantics_ar_get_or_create_stream_context(
    PCFLT_RELATED_OBJECTS FltObjects,
    semantics_ar_stream_context_t **Context)
{
    semantics_ar_stream_context_t *context = NULL;
    semantics_ar_stream_context_t *oldContext = NULL;
    NTSTATUS status;

    *Context = NULL;

    status = FltGetStreamContext(
        FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT *)&context);
    if (NT_SUCCESS(status)) {
        *Context = context;
        return STATUS_SUCCESS;
    }

    if (!FltSupportsStreamContexts(FltObjects->FileObject))
        return STATUS_NOT_SUPPORTED;

    status = FltAllocateContext(
        semantics_ar_globals.FilterHandle,
        FLT_STREAM_CONTEXT,
        sizeof(semantics_ar_stream_context_t),
        NonPagedPool,
        (PFLT_CONTEXT *)&context);
    if (!NT_SUCCESS(status))
        return status;

    RtlZeroMemory(context, sizeof(semantics_ar_stream_context_t));

    status = FltSetStreamContext(
        FltObjects->Instance,
        FltObjects->FileObject,
        FLT_SET_CONTEXT_KEEP_IF_EXISTS,
        context,
        (PFLT_CONTEXT *)&oldContext);

    if (status == STATUS_FLT_CONTEXT_ALREADY_DEFINED) {
        FltReleaseContext(context);
        *Context = oldContext;
        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status)) {
        FltReleaseContext(context);
        return status;
    }

    *Context = context;
    return STATUS_SUCCESS;
}