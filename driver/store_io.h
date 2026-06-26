#ifndef SEMANTICS_AR_DRIVER_STORE_IO_H
#define SEMANTICS_AR_DRIVER_STORE_IO_H

#include <fltKernel.h>

#include "driver.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarStoreEnsureDir(_In_ PCWSTR Dir, _In_ ULONG SecTag);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStoreReadAll(_In_ PCWSTR Path, _In_ ULONG BufTag, _In_ ULONG64 MaxBytes,
                         _Outptr_result_maybenull_ PUCHAR *OutBuf, _Out_ SIZE_T *OutLen);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStoreWriteAtomic(_In_ PCWSTR Tmp, _In_ PCWSTR Final,
                             _In_reads_bytes_(Len) const VOID *Buf, _In_ ULONG Len,
                             _In_ ULONG BufTag);

#endif
