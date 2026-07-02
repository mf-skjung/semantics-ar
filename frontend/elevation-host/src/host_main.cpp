#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <shlwapi.h>

#include "host_internal.h"

static HANDLE g_shutdownEvent = NULL;

void SarServerLock(bool lock)
{
    if (lock) {
        CoAddRefServerProcess();
    } else if (CoReleaseServerProcess() == 0 && g_shutdownEvent) {
        SetEvent(g_shutdownEvent);
    }
}

void SarRequestShutdown(void)
{
    if (g_shutdownEvent)
        SetEvent(g_shutdownEvent);
}

static HRESULT InitSecurity(void)
{
    PSECURITY_DESCRIPTOR sd = NULL;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:(A;;0x3;;;IU)(A;;0x3;;;SY)", SDDL_REVISION_1, &sd, NULL))
        return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = CoInitializeSecurity(sd, -1, NULL, NULL,
                                      RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                      RPC_C_IMP_LEVEL_IDENTIFY, NULL,
                                      EOAC_NONE, NULL);
    LocalFree(sd);
    return hr;
}

static int RunServer(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return 1;

    hr = InitSecurity();
    if (FAILED(hr)) {
        CoUninitialize();
        return 1;
    }

    g_shutdownEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_shutdownEvent) {
        CoUninitialize();
        return 1;
    }

    IClassFactory *factory = SarCreateClassFactory();
    if (!factory) {
        CloseHandle(g_shutdownEvent);
        g_shutdownEvent = NULL;
        CoUninitialize();
        return 1;
    }

    DWORD cookie = 0;
    hr = CoRegisterClassObject(CLSID_SarElevatedControl, factory,
                               CLSCTX_LOCAL_SERVER,
                               REGCLS_MULTIPLEUSE | REGCLS_SUSPENDED, &cookie);
    if (SUCCEEDED(hr))
        hr = CoResumeClassObjects();
    if (SUCCEEDED(hr))
        WaitForSingleObject(g_shutdownEvent, INFINITE);

    if (cookie)
        CoRevokeClassObject(cookie);
    factory->Release();
    CloseHandle(g_shutdownEvent);
    g_shutdownEvent = NULL;
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const wchar_t *cmd = GetCommandLineW();

    if (StrStrIW(cmd, L"RegServer") && !StrStrIW(cmd, L"UnregServer")) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hr))
            return 1;
        hr = SarRegisterServer();
        CoUninitialize();
        return SUCCEEDED(hr) ? 0 : 1;
    }

    if (StrStrIW(cmd, L"UnregServer")) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        SarUnregisterServer();
        CoUninitialize();
        return 0;
    }

    return RunServer();
}
