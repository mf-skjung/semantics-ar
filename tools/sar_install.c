#include <windows.h>
#include <stdio.h>

#define SAR_SERVICE_NAME L"SemanticsAr"
#define SAR_ELAM_SERVICE_NAME L"semantics_ar_elam"

static int ensure_elam_service(const wchar_t *elam_path)
{
    SC_HANDLE scm;
    SC_HANDLE service;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        fwprintf(stderr, L"OpenSCManager failed (%lu)\n", GetLastError());
        return 1;
    }

    service = CreateServiceW(scm, SAR_ELAM_SERVICE_NAME, SAR_ELAM_SERVICE_NAME,
                             SERVICE_QUERY_STATUS, SERVICE_KERNEL_DRIVER,
                             SERVICE_BOOT_START, SERVICE_ERROR_NORMAL,
                             elam_path, L"Early-Launch", NULL, NULL, NULL, NULL);
    if (service == NULL) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_EXISTS) {
            fwprintf(stderr, L"CreateService(%ls) failed (%lu)\n", SAR_ELAM_SERVICE_NAME, err);
            CloseServiceHandle(scm);
            return 1;
        }
    } else {
        CloseServiceHandle(service);
    }

    CloseServiceHandle(scm);
    return 0;
}

static int install_elam(const wchar_t *elam_path)
{
    HANDLE elam_file;
    BOOL ok;

    if (ensure_elam_service(elam_path) != 0)
        return 1;

    elam_file = CreateFileW(elam_path, FILE_READ_DATA, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (elam_file == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"elam driver not found at %ls (%lu)\n", elam_path, GetLastError());
        return 1;
    }

    ok = InstallELAMCertificateInfo(elam_file);
    CloseHandle(elam_file);
    if (!ok) {
        fwprintf(stderr, L"InstallELAMCertificateInfo failed (%lu); service will run unprotected\n",
                 GetLastError());
        return 1;
    }

    return 0;
}

static int protect_service(void)
{
    SC_HANDLE scm;
    SC_HANDLE service;
    SERVICE_LAUNCH_PROTECTED_INFO info;
    BOOL ok;
    int result = 1;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        fwprintf(stderr, L"OpenSCManager failed (%lu)\n", GetLastError());
        return 1;
    }

    service = OpenServiceW(scm, SAR_SERVICE_NAME, SERVICE_CHANGE_CONFIG);
    if (service == NULL) {
        fwprintf(stderr, L"OpenService failed (%lu); service must exist before protection can be requested\n",
                 GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }

    info.dwLaunchProtected = SERVICE_LAUNCH_PROTECTED_ANTIMALWARE_LIGHT;
    ok = ChangeServiceConfig2W(service, SERVICE_CONFIG_LAUNCH_PROTECTED, &info);
    if (!ok) {
        fwprintf(stderr, L"ChangeServiceConfig2 failed (%lu); service will run unprotected\n",
                 GetLastError());
    } else {
        result = 0;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return result;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) {
        fwprintf(stderr, L"usage: sar_install <path-to-semantics_ar_elam.sys>\n");
        return 2;
    }

    if (install_elam(argv[1]) != 0)
        return 1;

    if (protect_service() != 0)
        return 1;

    wprintf(L"semantics_ar_service registered for PPL-AM protected launch\n");
    return 0;
}
