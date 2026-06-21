#include "service_internal.h"
#include <wchar.h>

#define SAR_SVC_DISPLAY    L"Semantics Anti-Ransomware Minifilter"
#define SAR_SVC_GROUP      L"FSFilter Activity Monitor"
#define SAR_SVC_DEPEND     L"FltMgr\0"
#define SAR_INSTANCE_NAME  L"SemanticsAR Instance"
#define SAR_ALTITUDE       L"385400"
#define SAR_DRIVER_TAIL    L"\\drivers\\semantics_ar.sys"
#define SAR_IMAGE_PATH     L"system32\\drivers\\semantics_ar.sys"
#define SAR_REG_SVC_BASE   L"SYSTEM\\CurrentControlSet\\Services\\" SEMANTICS_AR_SVC_NAME

static BOOL write_instance_keys(HKEY hBase) {
    HKEY hInst = NULL;
    DWORD flags = 0;

    if (RegCreateKeyExW(hBase, L"Instances", 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY | KEY_SET_VALUE,
            NULL, &hInst, NULL) != ERROR_SUCCESS)
        return FALSE;

    LONG r = RegSetValueExW(hInst, L"DefaultInstance", 0, REG_SZ,
        (const BYTE *)SAR_INSTANCE_NAME,
        (DWORD)((wcslen(SAR_INSTANCE_NAME) + 1) * sizeof(WCHAR)));
    RegCloseKey(hInst);
    hInst = NULL;

    if (r != ERROR_SUCCESS)
        return FALSE;

    if (RegCreateKeyExW(hBase, L"Instances\\" SAR_INSTANCE_NAME, 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
            NULL, &hInst, NULL) != ERROR_SUCCESS)
        return FALSE;

    r = RegSetValueExW(hInst, L"Altitude", 0, REG_SZ,
        (const BYTE *)SAR_ALTITUDE,
        (DWORD)((wcslen(SAR_ALTITUDE) + 1) * sizeof(WCHAR)));
    if (r == ERROR_SUCCESS)
        r = RegSetValueExW(hInst, L"Flags", 0, REG_DWORD,
            (const BYTE *)&flags, sizeof(flags));
    RegCloseKey(hInst);
    return r == ERROR_SUCCESS;
}

static BOOL write_filter_registry(void) {
    HKEY hService = NULL;
    HKEY hParams = NULL;
    BOOL ok = FALSE;
    DWORD supported = 3;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SAR_REG_SVC_BASE,
            0, KEY_CREATE_SUB_KEY, &hService) != ERROR_SUCCESS)
        return FALSE;

    if (!write_instance_keys(hService))
        goto done;

    if (RegCreateKeyExW(hService, L"Parameters", 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY | KEY_SET_VALUE,
            NULL, &hParams, NULL) != ERROR_SUCCESS)
        goto done;

    if (RegSetValueExW(hParams, L"SupportedFeatures", 0, REG_DWORD,
            (const BYTE *)&supported, sizeof(supported)) != ERROR_SUCCESS)
        goto done;

    if (!write_instance_keys(hParams))
        goto done;

    ok = TRUE;

done:
    if (hParams) RegCloseKey(hParams);
    RegCloseKey(hService);
    return ok;
}

static void delete_filter_registry(void) {
    HKEY hService = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SAR_REG_SVC_BASE,
            0, KEY_ALL_ACCESS, &hService) == ERROR_SUCCESS) {
        RegDeleteTreeW(hService, L"Instances");
        RegDeleteTreeW(hService, L"Parameters");
        RegCloseKey(hService);
    }
}

semantics_ar_result_t svc_install_driver(const WCHAR *driverPath) {
    if (!driverPath)
        return SEMANTICS_AR_ERROR_INVALID_PARAM;

    WCHAR sysDir[MAX_PATH];
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return SEMANTICS_AR_ERROR_INTERNAL;

    WCHAR destPath[MAX_PATH];
    if (swprintf_s(destPath, MAX_PATH, L"%s" SAR_DRIVER_TAIL, sysDir) < 0)
        return SEMANTICS_AR_ERROR_INTERNAL;

    if (!CopyFileW(driverPath, destPath, FALSE))
        return SEMANTICS_AR_ERROR_INTERNAL;

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        DeleteFileW(destPath);
        return SEMANTICS_AR_ERROR_INTERNAL;
    }

    BOOL created = FALSE;
    SC_HANDLE hSvc = CreateServiceW(
        hSCM, SEMANTICS_AR_SVC_NAME, SAR_SVC_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_FILE_SYSTEM_DRIVER,
        SERVICE_BOOT_START,
        SERVICE_ERROR_NORMAL,
        SAR_IMAGE_PATH,
        SAR_SVC_GROUP,
        NULL, SAR_SVC_DEPEND, NULL, NULL);

    if (hSvc) {
        created = TRUE;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS)
            hSvc = OpenServiceW(hSCM, SEMANTICS_AR_SVC_NAME, SERVICE_QUERY_STATUS);
        if (!hSvc) {
            CloseServiceHandle(hSCM);
            DeleteFileW(destPath);
            return SEMANTICS_AR_ERROR_INTERNAL;
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    if (!write_filter_registry()) {
        delete_filter_registry();
        if (created) {
            hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
            if (hSCM) {
                hSvc = OpenServiceW(hSCM, SEMANTICS_AR_SVC_NAME, DELETE);
                if (hSvc) {
                    DeleteService(hSvc);
                    CloseServiceHandle(hSvc);
                }
                CloseServiceHandle(hSCM);
            }
            DeleteFileW(destPath);
        }
        return SEMANTICS_AR_ERROR_INTERNAL;
    }

    return SEMANTICS_AR_OK;
}

semantics_ar_result_t svc_uninstall_driver(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM)
        return SEMANTICS_AR_ERROR_INTERNAL;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SEMANTICS_AR_SVC_NAME,
        SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        return (err == ERROR_SERVICE_DOES_NOT_EXIST)
            ? SEMANTICS_AR_OK : SEMANTICS_AR_ERROR_INTERNAL;
    }

    SERVICE_STATUS ss;
    if (QueryServiceStatus(hSvc, &ss) && ss.dwCurrentState != SERVICE_STOPPED) {
        ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
        for (int i = 0; i < 30; i++) {
            Sleep(1000);
            if (!QueryServiceStatus(hSvc, &ss))
                break;
            if (ss.dwCurrentState == SERVICE_STOPPED)
                break;
        }
    }

    DeleteService(hSvc);
    delete_filter_registry();
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    WCHAR sysDir[MAX_PATH];
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        WCHAR path[MAX_PATH];
        if (swprintf_s(path, MAX_PATH, L"%s" SAR_DRIVER_TAIL, sysDir) > 0)
            DeleteFileW(path);
    }

    return SEMANTICS_AR_OK;
}