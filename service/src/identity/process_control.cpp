#include "service_internal.h"

BOOL svc_terminate_process(DWORD pid) {
    if (pid == 0 || pid == 4)
        return FALSE;
    if (pid == GetCurrentProcessId())
        return FALSE;

    HANDLE process = OpenProcess(
        PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, pid);
    if (!process)
        return FALSE;

    BOOL critical = FALSE;
    if (IsProcessCritical(process, &critical) && critical) {
        CloseHandle(process);
        return FALSE;
    }

    BOOL result = TerminateProcess(process, 1);
    CloseHandle(process);
    return result;
}