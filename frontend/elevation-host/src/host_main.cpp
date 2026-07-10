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
    PSECURITY_DESCRIPTOR relativeSd = NULL;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:(A;;0x3;;;IU)(A;;0x3;;;SY)", SDDL_REVISION_1, &relativeSd, NULL))
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD bodySize = 0, daclSize = 0, saclSize = 0, ownerSize = 0, groupSize = 0;
    MakeAbsoluteSD(relativeSd, NULL, &bodySize, NULL, &daclSize, NULL, &saclSize,
                   NULL, &ownerSize, NULL, &groupSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        LocalFree(relativeSd);
        return E_UNEXPECTED;
    }

    BYTE *absoluteBuf = (BYTE *)LocalAlloc(LPTR, bodySize + daclSize + saclSize + ownerSize + groupSize);
    if (!absoluteBuf) {
        LocalFree(relativeSd);
        return E_OUTOFMEMORY;
    }

    PSECURITY_DESCRIPTOR absoluteSd = absoluteBuf;
    PACL dacl = (PACL)(absoluteBuf + bodySize);
    PACL sacl = (PACL)((BYTE *)dacl + daclSize);
    PSID owner = (PSID)((BYTE *)sacl + saclSize);
    PSID group = (PSID)((BYTE *)owner + ownerSize);

    BOOL ok = MakeAbsoluteSD(relativeSd, absoluteSd, &bodySize, dacl, &daclSize,
                             sacl, &saclSize, owner, &ownerSize, group, &groupSize);
    LocalFree(relativeSd);
    if (!ok) {
        LocalFree(absoluteBuf);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = CoInitializeSecurity(absoluteSd, -1, NULL, NULL,
                                      RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                      RPC_C_IMP_LEVEL_IDENTIFY, NULL,
                                      EOAC_NONE, NULL);
    LocalFree(absoluteBuf);
    return hr;
}

static void HardenProcess(void)
{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoad = {};
    imageLoad.NoRemoteImages = 1;
    imageLoad.NoLowMandatoryLabelImages = 1;
    imageLoad.PreferSystem32Images = 1;
    SetProcessMitigationPolicy(ProcessImageLoadPolicy, &imageLoad, sizeof(imageLoad));

    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensionPoint = {};
    extensionPoint.DisableExtensionPoints = 1;
    SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &extensionPoint, sizeof(extensionPoint));

    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature = {};
    signature.MicrosoftSignedOnly = 1;
    SetProcessMitigationPolicy(ProcessSignaturePolicy, &signature, sizeof(signature));

    PROCESS_MITIGATION_CHILD_PROCESS_POLICY childProcess = {};
    childProcess.NoChildProcessCreation = 1;
    SetProcessMitigationPolicy(ProcessChildProcessPolicy, &childProcess, sizeof(childProcess));
}

static int RunServer(void)
{
    HardenProcess();

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
                               REGCLS_SINGLEUSE | REGCLS_SUSPENDED, &cookie);
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
