#ifndef SEMANTICS_AR_DRIVER_PRESERVE_H
#define SEMANTICS_AR_DRIVER_PRESERVE_H

#include <fltKernel.h>

#include "driver.h"
#include "semantics_ar/preserve_format.h"

typedef struct _SAR_PRESERVE SAR_PRESERVE, *PSAR_PRESERVE;

typedef enum _SAR_STAGE_RESULT {
    SAR_STAGE_STORED = 0,
    SAR_STAGE_ALREADY_COVERED,
    SAR_STAGE_DROPPED,
    SAR_STAGE_FAILED
} SAR_STAGE_RESULT;

typedef struct _SAR_PRESERVE_STATS {
    UINT64 capacity_bytes;
    UINT64 used_bytes;
    UINT64 retention_100ns;
    UINT64 oldest_protected_time;
    UINT32 protected_count;
    UINT32 probation_count;
} SAR_PRESERVE_STATS, *PSAR_PRESERVE_STATS;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveCreate(_In_ PFLT_FILTER Filter, _In_ PSAR_POSTURE Posture,
                           _Outptr_ PSAR_PRESERVE *Preserve);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveLoad(_Inout_ PSAR_PRESERVE Preserve);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarPreserveReady(_In_opt_ PSAR_PRESERVE Preserve);

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarPreserveStage(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                                  _In_ UINT64 Offset, _In_ UINT64 Length, _In_ UINT64 ActorId,
                                  _In_reads_bytes_(PlaintextLen) const UCHAR *Plaintext,
                                  _In_ ULONG PlaintextLen, _In_ BOOLEAN AgainstReservation);

_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN SarPreserveReserve(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 Bytes);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveRelease(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 Bytes);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarPreserveStageLink(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                              _In_ UINT64 FileSize, _In_ UINT64 ActorId, _In_ UINT64 LinkId);

UINT64 SarPreserveAllocLinkId(_Inout_ PSAR_PRESERVE Preserve);

BOOLEAN SarPreserveQuarantinePaths(_In_ const UINT16 *Path, _In_ UINT64 LinkId,
                                   _Out_writes_(DirCapChars) PWCHAR DirBuf, _In_ USHORT DirCapChars,
                                   _Out_writes_(LinkCapChars) PWCHAR LinkBuf,
                                   _In_ USHORT LinkCapChars);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreservePromote(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 ActorId);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveReconcile(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *Path,
                          _In_ UINT64 KeyOffset, _In_ UINT64 KeyLength);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveRename(_Inout_ PSAR_PRESERVE Preserve, _In_ const UINT16 *OldPath,
                       _In_ const UINT16 *NewPath);

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

_IRQL_requires_max_(APC_LEVEL)
VOID SarPreserveStats(_In_opt_ PSAR_PRESERVE Preserve, _Out_ PSAR_PRESERVE_STATS Stats);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveSetBudget(_Inout_ PSAR_PRESERVE Preserve, _In_ UINT64 Retention100ns,
                          _In_ UINT64 CapacityBytes);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarPreserveDestroy(_Inout_ PSAR_PRESERVE Preserve);

#endif
