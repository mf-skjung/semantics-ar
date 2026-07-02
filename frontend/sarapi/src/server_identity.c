#include "server_identity.h"

sarapi_result_t sarapi_server_is_system(HANDLE pipe)
{
    ULONG                    pid = 0;
    HANDLE                   proc;
    HANDLE                   token = NULL;
    DWORD                    len = 0;
    PTOKEN_USER              user;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID                     system = NULL;
    BOOL                     equal = FALSE;

    if (!GetNamedPipeServerProcessId(pipe, &pid))
        return SARAPI_SERVER_UNTRUSTED;

    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return SARAPI_SERVER_UNTRUSTED;

    if (!OpenProcessToken(proc, TOKEN_QUERY, &token)) {
        CloseHandle(proc);
        return SARAPI_SERVER_UNTRUSTED;
    }

    GetTokenInformation(token, TokenUser, NULL, 0, &len);
    if (len == 0) {
        CloseHandle(token);
        CloseHandle(proc);
        return SARAPI_SERVER_UNTRUSTED;
    }

    user = (PTOKEN_USER)LocalAlloc(LPTR, len);
    if (!user) {
        CloseHandle(token);
        CloseHandle(proc);
        return SARAPI_SERVER_UNTRUSTED;
    }

    if (GetTokenInformation(token, TokenUser, user, len, &len)
        && AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID,
                                    0, 0, 0, 0, 0, 0, 0, &system)) {
        equal = EqualSid(user->User.Sid, system);
        FreeSid(system);
    }

    LocalFree(user);
    CloseHandle(token);
    CloseHandle(proc);
    return equal ? SARAPI_OK : SARAPI_SERVER_UNTRUSTED;
}
