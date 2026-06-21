#include "emit.h"

VOID semantics_ar_emit_chain_candidate(
    PFLT_CALLBACK_DATA Data,
    ULONG Pid,
    LARGE_INTEGER WriteOffset,
    ULONG WriteLength,
    const UCHAR *OldChunk,
    const UCHAR *NewChunk)
{
    if (semantics_ar_globals.ClientPort == NULL)
        return;

    ULONG sampleSize = SEMANTICS_AR_ORACLE_SAMPLE_SIZE;
    if (sampleSize > SEMANTICS_AR_CHUNK_SIZE)
        sampleSize = SEMANTICS_AR_CHUNK_SIZE;

    ULONG tid = 0;
    if (Data->Thread)
        tid = HandleToULong(PsGetThreadId(Data->Thread));

    semantics_ar_chain_notification_t *notif = (semantics_ar_chain_notification_t *)
        ExAllocatePoolZero(NonPagedPoolNx, sizeof(*notif), SEMANTICS_AR_POOL_TAG);
    if (!notif)
        return;

    notif->message_type = SEMANTICS_AR_MSG_CHAIN_CANDIDATE;
    notif->process_id = Pid;
    notif->thread_id = tid;
    notif->file_offset = (uint64_t)WriteOffset.QuadPart;
    notif->write_length = WriteLength;
    notif->sample_size = sampleSize;
    RtlCopyMemory(notif->plaintext_sample, OldChunk, sampleSize);
    RtlCopyMemory(notif->ciphertext_sample, NewChunk, sampleSize);

    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    if (NT_SUCCESS(FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_CACHE_ONLY, &nameInfo))) {
        FltParseFileNameInformation(nameInfo);
        ULONG cpb = min(nameInfo->Name.Length, sizeof(notif->file_path) - sizeof(WCHAR));
        RtlCopyMemory(notif->file_path, nameInfo->Name.Buffer, cpb);
        FltReleaseFileNameInformation(nameInfo);
    }

    LARGE_INTEGER timeout;
    timeout.QuadPart = 0;

    FltSendMessage(semantics_ar_globals.FilterHandle,
        &semantics_ar_globals.ClientPort,
        notif, sizeof(*notif),
        NULL, NULL, &timeout);

    ExFreePoolWithTag(notif, SEMANTICS_AR_POOL_TAG);
}