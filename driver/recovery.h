#ifndef SEMANTICS_AR_DRIVER_RECOVERY_H
#define SEMANTICS_AR_DRIVER_RECOVERY_H

#include <fltKernel.h>

#include "semantics_ar/protocol.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
int SarRecoveryExecute(_In_ const semantics_ar_recovery_exec_t *Request,
                       _Out_ UINT64 *BytesRecovered);

#endif
