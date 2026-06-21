#ifndef SEMANTICS_AR_PRESERVE_H
#define SEMANTICS_AR_PRESERVE_H

#include "driver_internal.h"

BOOLEAN semantics_ar_shadow_ensure_initialized(
    _In_ PFLT_INSTANCE Instance,
    _In_ semantics_ar_instance_context_t *InstCtx);

semantics_ar_result_t semantics_ar_preserve_range(
    _In_opt_ PFLT_CALLBACK_DATA Data,
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PUNICODE_STRING OverrideOriginalPath,
    _In_ LARGE_INTEGER WriteOffset,
    _In_ ULONG WriteLength,
    _In_ LONGLONG Eof,
    _In_ ULONG EntryType,
    _In_ ULONG Pid);

semantics_ar_result_t semantics_ar_preserve_stream_full(
    _In_opt_ PFLT_CALLBACK_DATA Data,
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG Pid);

VOID semantics_ar_queue_section_capture(
    _In_ PFLT_INSTANCE Instance,
    _In_ PUNICODE_STRING NormalizedName,
    _In_ ULONG Pid);

FLT_PREOP_CALLBACK_STATUS semantics_ar_preserve_delete(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ ULONG Pid);

semantics_ar_result_t semantics_ar_preserve_victim(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ ULONG Pid);

LONGLONG semantics_ar_shadow_cluster_align(
    _In_ semantics_ar_instance_context_t *InstCtx,
    _In_ LONGLONG Bytes);

semantics_ar_result_t semantics_ar_capture_read(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _In_ LARGE_INTEGER Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _Out_ PULONG BytesReturned);

VOID semantics_ar_journal_append(
    _In_ PFLT_INSTANCE Instance,
    _In_ semantics_ar_instance_context_t *InstCtx,
    _In_ PUNICODE_STRING OriginalPath,
    _In_ ULONG EntryType,
    _In_ ULONG Pid,
    _In_ const WCHAR *ShadowPath,
    _In_ USHORT ShadowPathLength,
    _In_ LARGE_INTEGER WriteOffset,
    _In_ ULONG OriginalLength,
    _In_ LONGLONG Timestamp);

#endif