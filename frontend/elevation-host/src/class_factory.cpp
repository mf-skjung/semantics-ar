#include <windows.h>
#include <new>

#include "host_internal.h"

namespace {

class SarClassFactory final : public IClassFactory {
public:
    SarClassFactory() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
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

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid,
                                             void **ppv) override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        return SarCreateControlObject(riid, ppv);
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        SarServerLock(lock != FALSE);
        return S_OK;
    }

private:
    ~SarClassFactory() = default;

    volatile LONG m_ref;
};

}

IClassFactory *SarCreateClassFactory(void)
{
    return new (std::nothrow) SarClassFactory();
}
