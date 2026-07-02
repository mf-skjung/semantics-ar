#ifndef SAR_HOST_INTERNAL_H
#define SAR_HOST_INTERNAL_H

#include <windows.h>

#include "elevation_iface.h"

HRESULT SarCreateControlObject(REFIID riid, void **ppv);
IClassFactory *SarCreateClassFactory(void);

HRESULT SarRegisterServer(void);
HRESULT SarUnregisterServer(void);

void SarServerLock(bool lock);
void SarRequestShutdown(void);

#endif
