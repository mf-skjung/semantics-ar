#ifndef SEMANTICS_AR_DRIVER_CAPTURE_H
#define SEMANTICS_AR_DRIVER_CAPTURE_H

#include <fltKernel.h>

#include "driver.h"
#include "seam.h"

typedef struct _SAR_CAPTURE_CTX SAR_CAPTURE_CTX, *PSAR_CAPTURE_CTX;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarCaptureCreate(_In_ PFLT_FILTER Filter, _Outptr_ PSAR_CAPTURE_CTX *Ctx);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureDestroy(_Inout_ PSAR_CAPTURE_CTX Ctx);

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureSubmitWrite(_In_opt_ PSAR_CAPTURE_CTX Ctx,
                           _Inout_ PSAR_WRITE_SEAM_REQUEST Request);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureSubmitRenameTarget(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                  _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                  _In_ PEPROCESS Originator, _In_ PETHREAD Thread);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarCaptureSubmitOpenBaseline(_In_opt_ PSAR_CAPTURE_CTX Ctx, _In_ PFLT_CALLBACK_DATA Data,
                                  _In_ PCFLT_RELATED_OBJECTS FltObjects,
                                  _In_ PEPROCESS Originator, _In_ PETHREAD Thread);

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarCaptureOriginatorBlocked(_In_opt_ PSAR_CAPTURE_CTX Ctx,
                                    _In_ PEPROCESS Originator);

_IRQL_requires_max_(APC_LEVEL)
VOID SarCaptureBlockOriginator(_In_ PSAR_CAPTURE_CTX Ctx, _In_ PEPROCESS Originator);

#endif
