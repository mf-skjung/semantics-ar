#include "server_identity.h"
#include <aclapi.h>

sarapi_result_t sarapi_server_is_system(HANDLE pipe)
{
    PSID                     owner = NULL;
    PSECURITY_DESCRIPTOR     sd = NULL;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID                     system = NULL;
    BOOL                     equal = FALSE;

    if (GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                        &owner, NULL, NULL, NULL, &sd) != ERROR_SUCCESS)
        return SARAPI_SERVER_UNTRUSTED;

    if (owner != NULL && IsValidSid(owner)
        && AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID,
                                    0, 0, 0, 0, 0, 0, 0, &system)) {
        equal = EqualSid(owner, system);
        FreeSid(system);
    }

    if (sd != NULL)
        LocalFree(sd);

    return equal ? SARAPI_OK : SARAPI_SERVER_UNTRUSTED;
}

#define SARAPI_READ_TIMEOUT_MS 5000u

sarapi_result_t sarapi_read_frame(HANDLE pipe, void *out, DWORD len, DWORD *got)
{
    OVERLAPPED ov;
    HANDLE     ev;
    DWORD      n = 0;
    BOOL       ok;
    DWORD      e;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ev == NULL)
        return SARAPI_TRANSPORT_ERROR;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;

    ok = ReadFile(pipe, out, len, &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev, SARAPI_READ_TIMEOUT_MS) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(pipe, &ov, &n, FALSE);
        } else {
            CancelIoEx(pipe, &ov);
            GetOverlappedResult(pipe, &ov, &n, TRUE);
            CloseHandle(ev);
            return SARAPI_TRANSPORT_ERROR;
        }
    }

    e = GetLastError();
    CloseHandle(ev);
    if (!ok)
        return (e == ERROR_MORE_DATA) ? SARAPI_VERSION_MISMATCH : SARAPI_TRANSPORT_ERROR;
    if (got != NULL)
        *got = n;
    return SARAPI_OK;
}
