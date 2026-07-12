#define INITGUID
#include <windows.h>
#include <sddl.h>
#include <oleauto.h>
#include <shlwapi.h>
#include <stdio.h>

#include "host_internal.h"

namespace {

const wchar_t *kAccessSddl = L"O:BAG:BAD:(A;;0x3;;;IU)(A;;0x3;;;SY)";
const wchar_t *kAutomationProxy = L"{00020424-0000-0000-C000-000000000046}";
const wchar_t *kTlbFile = L"SemanticsArElevation.tlb";

void GuidStr(REFGUID guid, wchar_t *out, int cch)
{
    StringFromGUID2(guid, out, cch);
}

LONG SetSz(const wchar_t *subkey, const wchar_t *value, const wchar_t *data)
{
    HKEY key = NULL;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, NULL, 0,
                             KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS)
        return r;
    r = RegSetValueExW(key, value, 0, REG_SZ, (const BYTE *)data,
                       (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r;
}

LONG SetDword(const wchar_t *subkey, const wchar_t *value, DWORD data)
{
    HKEY key = NULL;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, NULL, 0,
                             KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS)
        return r;
    r = RegSetValueExW(key, value, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
    RegCloseKey(key);
    return r;
}

LONG SetBin(const wchar_t *subkey, const wchar_t *value, const void *data, DWORD cb)
{
    HKEY key = NULL;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, NULL, 0,
                             KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS)
        return r;
    r = RegSetValueExW(key, value, 0, REG_BINARY, (const BYTE *)data, cb);
    RegCloseKey(key);
    return r;
}

bool ExePath(wchar_t *out, DWORD cch)
{
    DWORD n = GetModuleFileNameW(NULL, out, cch);
    return n != 0 && n < cch;
}

}

HRESULT SarRegisterServer(void)
{
    wchar_t exe[MAX_PATH];
    if (!ExePath(exe, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    const wchar_t *leaf = PathFindFileNameW(exe);

    wchar_t clsid[64], iid[64], libid[64], appid[64];
    GuidStr(CLSID_SarElevatedControl, clsid, 64);
    GuidStr(IID_ISarElevatedControl, iid, 64);
    GuidStr(LIBID_SemanticsArElevationLib, libid, 64);
    GuidStr(APPID_SarElevation, appid, 64);

    wchar_t sub[256];

    swprintf(sub, 256, L"Software\\Classes\\CLSID\\%ls", clsid);
    if (SetSz(sub, NULL, L"Semantics-AR Elevated Control") != ERROR_SUCCESS)
        return E_ACCESSDENIED;
    {
        wchar_t localizedString[MAX_PATH + 16];
        swprintf(localizedString, MAX_PATH + 16, L"@%ls,-101", exe);
        SetSz(sub, L"LocalizedString", localizedString);
    }
    SetSz(sub, L"AppID", appid);

    swprintf(sub, 256, L"Software\\Classes\\CLSID\\%ls\\LocalServer32", clsid);
    SetSz(sub, NULL, exe);

    swprintf(sub, 256, L"Software\\Classes\\CLSID\\%ls\\Elevation", clsid);
    SetDword(sub, L"Enabled", 1);

    swprintf(sub, 256, L"Software\\Classes\\AppID\\%ls", appid);
    SetSz(sub, NULL, L"Semantics-AR Elevation");

    {
        PSECURITY_DESCRIPTOR sd = NULL;
        ULONG                sdLen = 0;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kAccessSddl, SDDL_REVISION_1, &sd, &sdLen)) {
            SetBin(sub, L"AccessPermission", sd, sdLen);
            LocalFree(sd);
        }
    }

    swprintf(sub, 256, L"Software\\Classes\\AppID\\%ls", leaf);
    SetSz(sub, L"AppID", appid);

    swprintf(sub, 256, L"Software\\Classes\\Interface\\%ls", iid);
    SetSz(sub, NULL, L"ISarElevatedControl");

    swprintf(sub, 256, L"Software\\Classes\\Interface\\%ls\\ProxyStubClsid32", iid);
    SetSz(sub, NULL, kAutomationProxy);

    swprintf(sub, 256, L"Software\\Classes\\Interface\\%ls\\TypeLib", iid);
    SetSz(sub, NULL, libid);
    SetSz(sub, L"Version", L"1.0");

    {
        wchar_t tlb[MAX_PATH];
        wcscpy_s(tlb, MAX_PATH, exe);
        PathRemoveFileSpecW(tlb);
        PathAppendW(tlb, kTlbFile);

        ITypeLib *lib = NULL;
        HRESULT   hrTlb = LoadTypeLibEx(tlb, REGKIND_NONE, &lib);
        if (FAILED(hrTlb))
            return hrTlb;
        RegisterTypeLib(lib, tlb, NULL);
        lib->Release();
    }

    return S_OK;
}

HRESULT SarUnregisterServer(void)
{
    wchar_t clsid[64], iid[64], libid[64], appid[64];
    GuidStr(CLSID_SarElevatedControl, clsid, 64);
    GuidStr(IID_ISarElevatedControl, iid, 64);
    GuidStr(LIBID_SemanticsArElevationLib, libid, 64);
    GuidStr(APPID_SarElevation, appid, 64);

    wchar_t exe[MAX_PATH];
    const wchar_t *leaf = ExePath(exe, MAX_PATH) ? PathFindFileNameW(exe) : NULL;

    wchar_t sub[256];

    swprintf(sub, 256, L"Software\\Classes\\CLSID\\%ls", clsid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);

    swprintf(sub, 256, L"Software\\Classes\\AppID\\%ls", appid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);

    if (leaf) {
        swprintf(sub, 256, L"Software\\Classes\\AppID\\%ls", leaf);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);
    }

    swprintf(sub, 256, L"Software\\Classes\\Interface\\%ls", iid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);

    UnRegisterTypeLib(LIBID_SemanticsArElevationLib, 1, 0, 0, SYS_WIN64);

    return S_OK;
}
