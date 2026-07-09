#include <windows.h>
#include <stdio.h>

static int do_hold(int sec)
{
    wprintf(L"HOLD pid=%lu\n", GetCurrentProcessId());
    fflush(stdout);
    if (sec > 0)
        Sleep((DWORD)sec * 1000);
    return 0;
}

static int do_attack(DWORD pid)
{
    HANDLE h;
    int open = 0, valloc = 0, wrote = 0, thread = 0;
    DWORD gle = 0;

    h = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_CREATE_THREAD |
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h != NULL) {
        LPVOID rem;
        open = 1;
        rem = VirtualAllocEx(h, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (rem != NULL) {
            unsigned char code[1] = { 0xC3 };
            SIZE_T wn = 0;
            valloc = 1;
            if (WriteProcessMemory(h, rem, code, sizeof(code), &wn) && wn == sizeof(code))
                wrote = 1;
            {
                HANDLE t = CreateRemoteThread(h, NULL, 0, (LPTHREAD_START_ROUTINE)rem,
                                              NULL, 0, NULL);
                if (t != NULL) { thread = 1; CloseHandle(t); }
            }
        }
        gle = GetLastError();
        CloseHandle(h);
    } else {
        gle = GetLastError();
    }

    wprintf(L"ATTACK open=%d valloc=%d write=%d thread=%d gle=%lu\n",
            open, valloc, wrote, thread, gle);
    fflush(stdout);
    return (wrote || thread) ? 10 : 0;
}

static int do_query(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    int ok = (h != NULL);
    if (h != NULL)
        CloseHandle(h);
    wprintf(L"QUERY open=%d\n", ok);
    fflush(stdout);
    return ok ? 0 : 11;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && _wcsicmp(argv[1], L"hold") == 0)
        return do_hold(argc >= 3 ? _wtoi(argv[2]) : 30);
    if (argc >= 3 && _wcsicmp(argv[1], L"attack") == 0)
        return do_attack((DWORD)_wtoi(argv[2]));
    if (argc >= 3 && _wcsicmp(argv[1], L"query") == 0)
        return do_query((DWORD)_wtoi(argv[2]));
    wprintf(L"usage: inject_probe hold <sec> | attack <pid> | query <pid>\n");
    return 2;
}
