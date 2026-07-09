#ifndef SEMANTICS_AR_DRIVER_OBGUARD_H
#define SEMANTICS_AR_DRIVER_OBGUARD_H

#include <fltKernel.h>

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarObGuardRegister(VOID);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarObGuardUnregister(VOID);

#endif
