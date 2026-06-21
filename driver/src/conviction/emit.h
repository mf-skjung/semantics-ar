#ifndef SEMANTICS_AR_EMIT_H
#define SEMANTICS_AR_EMIT_H

#include "driver_internal.h"

VOID semantics_ar_emit_chain_candidate(
    _In_ PFLT_CALLBACK_DATA Data,
    _In_ ULONG Pid,
    _In_ LARGE_INTEGER WriteOffset,
    _In_ ULONG WriteLength,
    _In_reads_bytes_(SEMANTICS_AR_CHUNK_SIZE) const UCHAR *OldChunk,
    _In_reads_bytes_(SEMANTICS_AR_CHUNK_SIZE) const UCHAR *NewChunk);

#endif