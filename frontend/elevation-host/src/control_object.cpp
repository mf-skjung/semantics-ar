#include <windows.h>
#include <oleauto.h>
#include <sddl.h>
#include <string.h>
#include <new>

#include "host_internal.h"
#include "sarapi.h"

namespace {

HRESULT MapResult(sarapi_result_t r)
{
    switch (r) {
    case SARAPI_OK:              return S_OK;
    case SARAPI_INVALID_ARG:     return E_INVALIDARG;
    case SARAPI_ACCESS_DENIED:   return E_ACCESSDENIED;
    case SARAPI_SERVER_UNTRUSTED:return SAR_E_SERVER_UNTRUSTED;
    case SARAPI_VERSION_MISMATCH:return SAR_E_VERSION_MISMATCH;
    case SARAPI_PIPE_UNAVAILABLE:return SAR_E_PIPE_UNAVAILABLE;
    case SARAPI_TRANSPORT_ERROR:
    default:                     return SAR_E_TRANSPORT;
    }
}

bool CallerMeetsPolicy(HANDLE token)
{
    DWORD len = 0;

    GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &len);
    if (len == 0)
        return false;

    PTOKEN_MANDATORY_LABEL label = (PTOKEN_MANDATORY_LABEL)LocalAlloc(LPTR, len);
    if (!label)
        return false;

    bool ok = false;
    if (GetTokenInformation(token, TokenIntegrityLevel, label, len, &len)) {
        UCHAR  count = *GetSidSubAuthorityCount(label->Label.Sid);
        DWORD  rid = *GetSidSubAuthority(label->Label.Sid, (DWORD)(count - 1));

        if (rid >= SECURITY_MANDATORY_MEDIUM_RID) {
            DWORD caller_session = 0;
            DWORD got = 0;

            if (GetTokenInformation(token, TokenSessionId, &caller_session,
                                    sizeof(caller_session), &got)) {
                DWORD own_session = 0;

                if (ProcessIdToSessionId(GetCurrentProcessId(), &own_session))
                    ok = (caller_session == own_session);
            }
        }
    }

    LocalFree(label);
    return ok;
}

HRESULT ValidateCaller(void)
{
    HRESULT hr = CoImpersonateClient();
    if (FAILED(hr))
        return E_ACCESSDENIED;

    HANDLE  token = NULL;
    HRESULT result = E_ACCESSDENIED;

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        if (CallerMeetsPolicy(token))
            result = S_OK;
        CloseHandle(token);
    }

    CoRevertToSelf();
    return result;
}

HRESULT PackBlob(const void *data, ULONG cb, SAFEARRAY **out)
{
    SAFEARRAY *sa = SafeArrayCreateVector(VT_UI1, 0, cb);
    if (!sa)
        return E_OUTOFMEMORY;

    if (cb != 0) {
        void   *p = NULL;
        HRESULT hr = SafeArrayAccessData(sa, &p);

        if (FAILED(hr)) {
            SafeArrayDestroy(sa);
            return hr;
        }
        memcpy(p, data, cb);
        SafeArrayUnaccessData(sa);
    }

    *out = sa;
    return S_OK;
}

class SarControl final : public ISarElevatedControl {
public:
    SarControl() : m_ref(1) { SarServerLock(true); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ISarElevatedControl) {
            *ppv = static_cast<ISarElevatedControl *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef(void) override
    {
        return (ULONG)InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release(void) override
    {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0)
            delete this;
        return (ULONG)n;
    }

    HRESULT STDMETHODCALLTYPE CatalogPage(ULONG start, ULONG *total,
                                          ULONG *returned, SAFEARRAY **blob) override
    {
        if (!total || !returned || !blob)
            return E_POINTER;
        *total = 0;
        *returned = 0;
        *blob = NULL;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        sarapi_catalog_entry_t entries[SARAPI_PAGE];
        uint32_t               t = 0, n = 0;
        sarapi_result_t        r = sarapi_catalog_page(start, entries, &t, &n);
        if (r != SARAPI_OK)
            return MapResult(r);

        hr = PackBlob(entries, n * (ULONG)sizeof(entries[0]), blob);
        if (FAILED(hr))
            return hr;

        *total = t;
        *returned = n;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreservePage(ULONG start, ULONG *total,
                                           ULONG *returned, SAFEARRAY **blob) override
    {
        if (!total || !returned || !blob)
            return E_POINTER;
        *total = 0;
        *returned = 0;
        *blob = NULL;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        sarapi_preserve_entry_t entries[SARAPI_PAGE];
        uint32_t                t = 0, n = 0;
        sarapi_result_t         r = sarapi_preserve_page(start, entries, &t, &n);
        if (r != SARAPI_OK)
            return MapResult(r);

        hr = PackBlob(entries, n * (ULONG)sizeof(entries[0]), blob);
        if (FAILED(hr))
            return hr;

        *total = t;
        *returned = n;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Recover(SAFEARRAY *keyId, BSTR targetPath,
                                      LONG *result) override
    {
        if (!result)
            return E_POINTER;
        *result = -1;
        if (!keyId || !targetPath)
            return E_INVALIDARG;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        if (SafeArrayGetDim(keyId) != 1)
            return E_INVALIDARG;

        LONG lb = 0, ub = 0;
        SafeArrayGetLBound(keyId, 1, &lb);
        SafeArrayGetUBound(keyId, 1, &ub);
        if ((ub - lb + 1) != SARAPI_KEY_ID_SIZE)
            return E_INVALIDARG;

        uint8_t key[SARAPI_KEY_ID_SIZE];
        void   *kp = NULL;
        hr = SafeArrayAccessData(keyId, &kp);
        if (FAILED(hr))
            return hr;
        memcpy(key, kp, SARAPI_KEY_ID_SIZE);
        SafeArrayUnaccessData(keyId);

        int32_t         res = -1;
        sarapi_result_t r = sarapi_recover(key, (const uint16_t *)targetPath, &res);
        if (r != SARAPI_OK)
            return MapResult(r);
        *result = res;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreserveRecover(BSTR targetPath, ULONGLONG offset,
                                              ULONGLONG length, LONG *result) override
    {
        if (!result)
            return E_POINTER;
        *result = -1;
        if (!targetPath)
            return E_INVALIDARG;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        int32_t         res = -1;
        sarapi_result_t r = sarapi_preserve_recover((const uint16_t *)targetPath,
                                                    offset, length, &res);
        if (r != SARAPI_OK)
            return MapResult(r);
        *result = res;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetMode(ULONG mode, LONG *result) override
    {
        if (!result)
            return E_POINTER;
        *result = -1;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        int32_t         res = -1;
        sarapi_result_t r = sarapi_set_mode(mode, &res);
        if (r != SARAPI_OK)
            return MapResult(r);
        *result = res;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetBudget(ULONGLONG retention100ns,
                                        ULONGLONG capacityBytes, LONG *result) override
    {
        if (!result)
            return E_POINTER;
        *result = -1;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        int32_t         res = -1;
        sarapi_result_t r = sarapi_set_budget(retention100ns, capacityBytes, &res);
        if (r != SARAPI_OK)
            return MapResult(r);
        *result = res;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ResolveIdentity(BSTR imagePath, SAFEARRAY **identityBlob,
                                              ULONG *verdict, LONG *result) override
    {
        if (!identityBlob || !verdict || !result)
            return E_POINTER;
        *identityBlob = NULL;
        *verdict = 0;
        *result = -1;
        if (!imagePath)
            return E_INVALIDARG;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        sarapi_identity_t identity;
        uint32_t          v = 0;
        int32_t           res = -1;
        sarapi_result_t   r = sarapi_resolve_identity((const uint16_t *)imagePath,
                                                      &identity, &v, &res);
        if (r != SARAPI_OK)
            return MapResult(r);

        hr = PackBlob(&identity, (ULONG)sizeof(identity), identityBlob);
        if (FAILED(hr))
            return hr;

        *verdict = v;
        *result = res;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WhitelistAdd(BSTR imagePath, ULONG *verdict,
                                           LONG *result) override
    {
        return Whitelist(true, imagePath, verdict, result);
    }

    HRESULT STDMETHODCALLTYPE WhitelistRemove(BSTR imagePath, ULONG *verdict,
                                              LONG *result) override
    {
        return Whitelist(false, imagePath, verdict, result);
    }

    HRESULT STDMETHODCALLTYPE Shutdown(void) override
    {
        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;
        SarRequestShutdown();
        return S_OK;
    }

private:
    ~SarControl() { SarServerLock(false); }

    HRESULT Whitelist(bool add, BSTR imagePath, ULONG *verdict, LONG *result)
    {
        if (!verdict || !result)
            return E_POINTER;
        *verdict = 0;
        *result = -1;
        if (!imagePath)
            return E_INVALIDARG;

        HRESULT hr = ValidateCaller();
        if (FAILED(hr))
            return hr;

        uint32_t        v = 0;
        int32_t         res = -1;
        sarapi_result_t r = add
            ? sarapi_whitelist_add((const uint16_t *)imagePath, &v, &res)
            : sarapi_whitelist_remove((const uint16_t *)imagePath, &v, &res);
        if (r != SARAPI_OK)
            return MapResult(r);

        *verdict = v;
        *result = res;
        return S_OK;
    }

    volatile LONG m_ref;
};

}

HRESULT SarCreateControlObject(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    SarControl *obj = new (std::nothrow) SarControl();
    if (!obj)
        return E_OUTOFMEMORY;

    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}
