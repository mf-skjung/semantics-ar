#ifndef SEMANTICS_AR_DRIVER_PRESERVE_H
#define SEMANTICS_AR_DRIVER_PRESERVE_H

#include <fltKernel.h>

#include "driver.h"
#include "semantics_ar/preserve_format.h"

typedef struct _SAR_PRESERVE SAR_PRESERVE, *PSAR_PRESERVE;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveCreate(_In_ PFLT_FILTER Filter, _In_ PSAR_POSTURE Posture,
                           _Outptr_ PSAR_PRESERVE *Preserve);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveLoad(_Inout_ PSAR_PRESERVE Preserve);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPreserveReady(_In_opt_ PSAR_PRESERVE Preserve);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveStage(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                          _In_ UINT64 Offset, _In_ UINT64 Length,
                          _In_reads_bytes_(PlaintextLen) const UCHAR *Plaintext,
                          _In_ ULONG PlaintextLen);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveReconcile(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                          _In_ UINT64 KeyOffset, _In_ UINT64 KeyLength);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPreserveWouldExceed(_In_opt_ PSAR_PRESERVE Preserve, _In_ UINT64 IncomingBytes);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveRestore(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                            _In_ UINT64 Offset, _In_ UINT64 Length,
                            _Out_writes_bytes_to_(OutCap, *OutLen) PUCHAR Out,
                            _In_ ULONG OutCap, _Out_ PULONG OutLen);

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarPreserveProject(_In_ PSAR_PRESERVE Preserve, _In_ ULONG64 Index,
                       _Out_ semantics_ar_preserve_entry_t *Entry, _Out_ ULONG64 *Total);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveSetBudget(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 Retention100ns,
                          _In_ UINT64 CapacityBytes);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveDestroy(_Inout_ PSAR_PRESERVE Preserve);

#endif
