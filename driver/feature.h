#ifndef SEMANTICS_AR_DRIVER_FEATURE_H
#define SEMANTICS_AR_DRIVER_FEATURE_H

#include <fltKernel.h>

#include "driver.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarFeatureDetect(_Inout_ PSAR_POSTURE Posture);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarFeatureDetectDevDrive(_In_ PCFLT_RELATED_OBJECTS FltObjects, _Inout_ PSAR_POSTURE Posture);

#endif
