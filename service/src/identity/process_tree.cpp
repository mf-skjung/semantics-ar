#include "service_internal.h"
#include <tlhelp32.h>

DWORD svc_collect_process_tree(DWORD rootPid, DWORD *outPids, DWORD maxPids) {
    if (!outPids || maxPids == 0)
        return 0;

    FILETIME creationTimes[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
    memset(creationTimes, 0, sizeof(creationTimes));

    outPids[0] = rootPid;
    DWORD count = 1;

    HANDLE hRoot = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, rootPid);
    if (hRoot) {
        FILETIME exit, kernel, user;
        GetProcessTimes(hRoot, &creationTimes[0], &exit, &kernel, &user);
        CloseHandle(hRoot);
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return count;

    BOOL changed;
    do {
        changed = FALSE;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (!Process32FirstW(snap, &pe))
            break;
        do {
            if (pe.th32ProcessID <= 4)
                continue;

            LONG parentIdx = -1;
            for (DWORD j = 0; j < count; j++) {
                if (pe.th32ParentProcessID == outPids[j]) {
                    parentIdx = (LONG)j;
                    break;
                }
            }
            if (parentIdx < 0)
                continue;

            BOOL alreadyInList = FALSE;
            for (DWORD j = 0; j < count; j++) {
                if (pe.th32ProcessID == outPids[j]) {
                    alreadyInList = TRUE;
                    break;
                }
            }
            if (alreadyInList)
                continue;
            if (count >= maxPids)
                break;

            HANDLE hChild = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                        FALSE, pe.th32ProcessID);
            if (!hChild)
                continue;

            FILETIME childCreation, exit, kernel, user;
            if (!GetProcessTimes(hChild, &childCreation, &exit, &kernel, &user)) {
                CloseHandle(hChild);
                continue;
            }
            CloseHandle(hChild);

            ULARGE_INTEGER parentTime;
            parentTime.LowPart = creationTimes[parentIdx].dwLowDateTime;
            parentTime.HighPart = creationTimes[parentIdx].dwHighDateTime;
            if (parentTime.QuadPart != 0) {
                ULARGE_INTEGER childTime;
                childTime.LowPart = childCreation.dwLowDateTime;
                childTime.HighPart = childCreation.dwHighDateTime;
                if (childTime.QuadPart < parentTime.QuadPart)
                    continue;
            }

            outPids[count] = pe.th32ProcessID;
            creationTimes[count] = childCreation;
            count++;
            changed = TRUE;
        } while (Process32NextW(snap, &pe));
    } while (changed);

    CloseHandle(snap);
    return count;
}