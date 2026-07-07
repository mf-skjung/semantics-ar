#ifndef SEMANTICS_AR_DRIVER_CAPTURE_H
#define SEMANTICS_AR_DRIVER_CAPTURE_H

#include <fltKernel.h>

#include "driver.h"
#include "seam.h"
#include "preserve.h"

typedef struct _SAR_CAPTURE_CTX SAR_CAPTURE_CTX, *PSAR_CAPTURE_CTX;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCaptureCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_CAPTURE_CTX *Ctx);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureDestroy(_Inout_ PSAR_CAPTURE_CTX Ctx);

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureSubmitWrite(_In_opt_ PSAR_CAPTURE_CTX Ctx,
                           _Inout_ PSAR_WRITE_SEAM_REQUEST Request);

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitRenameTarget(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                  _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                  _In_ PEPROCESS Originator, _In_ PETHREAD Thread);

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitDelete(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                            _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                            _In_ PETHREAD Thread);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureSubmitRenameRekey(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                 _In_ PCFLT_RELATED_OBJECTS FltObjects);

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureSubmitSupersede(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                           _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                                           _In_ PETHREAD Thread);

_IRQL_requires_max_(PASSIVE_LEVEL)
SAR_STAGE_RESULT SarCaptureInPlaceRegion(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                         _In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ PEPROCESS Originator,
                                         _In_ UINT64 Offset, _In_ UINT64 Length);

typedef struct _SAR_MMAP_INSTCTX {
    PDEVICE_OBJECT disk_top;
    ULONG bytes_per_cluster;
    ULONG bytes_per_sector;
} SAR_MMAP_INSTCTX, *PSAR_MMAP_INSTCTX;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapInstanceResolve(_In_ PFLT_FILTER Filter, _In_ PCFLT_RELATED_OBJECTS FltObjects);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapInstanceCleanup(_In_ PFLT_CONTEXT Context, _In_ FLT_CONTEXT_TYPE ContextType);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapArm(_In_ PFLT_INSTANCE Instance, _In_ PFILE_OBJECT FileObject,
                _Inout_ PSAR_STREAM_CONTEXT Sc, _In_ HANDLE ArmPid);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapCaptureEager(_In_ PFLT_INSTANCE Instance, _In_ PSAR_STREAM_CONTEXT Sc, _In_ UINT64 Actor);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarMmapOnPagingWrite(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PCFLT_RELATED_OBJECTS FltObjects,
                             _In_ PFLT_CALLBACK_DATA Data);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarMmapCaptureNocache(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PCFLT_RELATED_OBJECTS FltObjects,
                           _In_ PFLT_CALLBACK_DATA Data, _In_ PEPROCESS Originator);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarCaptureOriginatorBlocked(_In_opt_ PSAR_CAPTURE_CTX Ctx,
                                    _In_ PEPROCESS Originator);

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureBlockOriginator(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Originator,
                               _In_ UINT32 EventClass);

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureNoteCapacityRefusal(_In_ PEPROCESS Originator);

_IRQL_requires_max_(APC_LEVEL)
LONG SarCaptureInflight(_In_opt_ PSAR_CAPTURE_CTX Ctx);

#endif
