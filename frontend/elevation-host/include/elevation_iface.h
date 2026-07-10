#ifndef SAR_ELEVATION_IFACE_H
#define SAR_ELEVATION_IFACE_H

#include <windows.h>
#include <combaseapi.h>
#include <oaidl.h>

#define SAR_E_PIPE_UNAVAILABLE MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0201)
#define SAR_E_SERVER_UNTRUSTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0202)
#define SAR_E_VERSION_MISMATCH MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0203)
#define SAR_E_TRANSPORT        MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x0204)

MIDL_INTERFACE("B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A11")
ISarElevatedControl : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CatalogPage(ULONG start, ULONG *total,
                                                  ULONG *returned, SAFEARRAY **blob) = 0;
    virtual HRESULT STDMETHODCALLTYPE PreservePage(ULONG start, ULONG *total,
                                                   ULONG *returned, SAFEARRAY **blob) = 0;
    virtual HRESULT STDMETHODCALLTYPE AppIdentityPage(ULONG start, ULONG *total,
                                                      ULONG *returned, SAFEARRAY **blob) = 0;
    virtual HRESULT STDMETHODCALLTYPE Recover(SAFEARRAY *keyId, BSTR targetPath,
                                              LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE PreserveRecover(BSTR targetPath, ULONGLONG offset,
                                                      ULONGLONG length, LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMode(ULONG mode, LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBudget(ULONGLONG retention100ns,
                                                ULONGLONG capacityBytes, LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResolveIdentity(BSTR imagePath, SAFEARRAY **identityBlob,
                                                      ULONG *verdict, LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE WhitelistAdd(BSTR imagePath, ULONG *verdict,
                                                   LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE WhitelistRemove(BSTR imagePath, ULONG *verdict,
                                                      LONG *result) = 0;
    virtual HRESULT STDMETHODCALLTYPE Shutdown(void) = 0;
};

DEFINE_GUID(LIBID_SemanticsArElevationLib, 0xB3F2A6C1, 0x5D84, 0x4E2A,
            0x9C, 0x77, 0x1E, 0x5A, 0x0D, 0x9C, 0x4A, 0x10);
DEFINE_GUID(IID_ISarElevatedControl, 0xB3F2A6C1, 0x5D84, 0x4E2A,
            0x9C, 0x77, 0x1E, 0x5A, 0x0D, 0x9C, 0x4A, 0x11);
DEFINE_GUID(CLSID_SarElevatedControl, 0xB3F2A6C1, 0x5D84, 0x4E2A,
            0x9C, 0x77, 0x1E, 0x5A, 0x0D, 0x9C, 0x4A, 0x12);
DEFINE_GUID(APPID_SarElevation, 0xB3F2A6C1, 0x5D84, 0x4E2A,
            0x9C, 0x77, 0x1E, 0x5A, 0x0D, 0x9C, 0x4A, 0x13);

#endif
