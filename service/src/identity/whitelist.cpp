#include "service_internal.h"
#include <tlhelp32.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <psapi.h>
#include <string.h>

static const WCHAR *LOLBIN_LIST[] = {
    L"powershell.exe", L"cmd.exe", L"wscript.exe", L"cscript.exe",
    L"mshta.exe", L"rundll32.exe", L"regsvr32.exe", L"certutil.exe",
    L"bitsadmin.exe", L"msbuild.exe", L"installutil.exe"
};
#define LOLBIN_COUNT (sizeof(LOLBIN_LIST) / sizeof(LOLBIN_LIST[0]))

static const WCHAR *extract_exe_name(const WCHAR *path) {
    const WCHAR *name = wcsrchr(path, L'\\');
    return name ? name + 1 : path;
}

static BOOL str_equal_ci(const WCHAR *a, const WCHAR *b) {
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static BOOL is_lolbin(const WCHAR *path) {
    const WCHAR *fileName = extract_exe_name(path);
    for (DWORD i = 0; i < LOLBIN_COUNT; i++) {
        if (str_equal_ci(fileName, LOLBIN_LIST[i]))
            return TRUE;
    }
    return FALSE;
}

static BOOL get_process_image_path(DWORD pid, WCHAR *path, DWORD maxChars) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc)
        return FALSE;
    DWORD size = maxChars;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);
    return ok;
}

static LONGLONG get_process_creation_time(HANDLE hProcess) {
    FILETIME creation, exitTime, kernel, user;
    if (GetProcessTimes(hProcess, &creation, &exitTime, &kernel, &user)) {
        LARGE_INTEGER li;
        li.LowPart = creation.dwLowDateTime;
        li.HighPart = creation.dwHighDateTime;
        return li.QuadPart;
    }
    return 0;
}

static BOOL verify_cert_subject(const WCHAR *filePath, WCHAR *subjectOut, DWORD maxChars) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath;

    GUID policyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    BOOL result = FALSE;
    LONG status = WinVerifyTrust(NULL, &policyGUID, &trustData);

    if (status == 0) {
        CRYPT_PROVIDER_DATA *provData =
            WTHelperProvDataFromStateData(trustData.hWVTStateData);
        if (provData) {
            CRYPT_PROVIDER_SGNR *signer =
                WTHelperGetProvSignerFromChain(provData, 0, FALSE, 0);
            if (signer && signer->pasCertChain && signer->csCertChain > 0) {
                PCCERT_CONTEXT cert = signer->pasCertChain[0].pCert;
                if (cert) {
                    CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                       0, NULL, subjectOut, maxChars);
                    result = TRUE;
                }
            }
        }
    }

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &policyGUID, &trustData);

    return result;
}

BOOL svc_hash_file(const WCHAR *path, BYTE hash[32]) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BOOL result = FALSE;
    BYTE buf[8192];
    DWORD bytesRead;

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        goto done;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
        goto done;

    while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
        if (!CryptHashData(hHash, buf, bytesRead, 0))
            goto done;
    }

    {
        DWORD hashLen = 32;
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
            result = TRUE;
    }

done:
    if (hHash) CryptDestroyHash(hHash);
    if (hProv) CryptReleaseContext(hProv, 0);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    return result;
}

static void push_trusted_pid(DWORD pid, LONGLONG creationTime) {
    if (!g_svc.Port)
        return;
    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_trusted_pid_info_t info;
    } msg;
    msg.hdr.message_type = SEMANTICS_AR_MSG_ADD_TRUSTED_PID;
    msg.info.pid = pid;
    msg.info.creation_time = creationTime;
    DWORD bytesReturned;
    FilterSendMessage(g_svc.Port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}

static void push_remove_pid(DWORD pid, LONGLONG creationTime) {
    if (!g_svc.Port)
        return;
    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_trusted_pid_info_t info;
    } msg;
    msg.hdr.message_type = SEMANTICS_AR_MSG_REMOVE_TRUSTED_PID;
    msg.info.pid = pid;
    msg.info.creation_time = creationTime;
    DWORD bytesReturned;
    FilterSendMessage(g_svc.Port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}

static semantics_ar_active_process_t *find_active(DWORD pid) {
    for (DWORD i = 0; i < SEMANTICS_AR_MAX_ACTIVE_PROCESSES; i++) {
        if (g_svc.Whitelist.Active[i].Valid && g_svc.Whitelist.Active[i].Pid == pid)
            return &g_svc.Whitelist.Active[i];
    }
    return NULL;
}

static semantics_ar_active_process_t *alloc_active(void) {
    for (DWORD i = 0; i < SEMANTICS_AR_MAX_ACTIVE_PROCESSES; i++) {
        if (!g_svc.Whitelist.Active[i].Valid)
            return &g_svc.Whitelist.Active[i];
    }
    return NULL;
}

static void CALLBACK process_exit_callback(PVOID context, BOOLEAN timedOut) {
    if (timedOut)
        return;
    DWORD pid = (DWORD)(ULONG_PTR)context;

    EnterCriticalSection(&g_svc.Whitelist.Lock);
    semantics_ar_active_process_t *proc = find_active(pid);
    if (proc && proc->Valid && proc->ProcessHandle) {
        LONGLONG ct = get_process_creation_time(proc->ProcessHandle);
        push_remove_pid(pid, ct);
        CloseHandle(proc->ProcessHandle);
        proc->ProcessHandle = NULL;
        proc->WaitHandle = NULL;
        proc->Valid = FALSE;
    }
    LeaveCriticalSection(&g_svc.Whitelist.Lock);
}

static BOOL activate_process(DWORD pid, const WCHAR *path, DWORD trustTier) {
    semantics_ar_active_process_t *proc = alloc_active();
    if (!proc)
        return FALSE;

    memset(proc, 0, sizeof(*proc));
    proc->Pid = pid;
    proc->Valid = TRUE;
    proc->TrustTier = trustTier;
    wcscpy_s(proc->ExePath, MAX_PATH, path);

    proc->ProcessHandle = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!proc->ProcessHandle) {
        proc->Valid = FALSE;
        return FALSE;
    }

    LONGLONG ct = get_process_creation_time(proc->ProcessHandle);
    push_trusted_pid(pid, ct);

    RegisterWaitForSingleObject(
        &proc->WaitHandle, proc->ProcessHandle, process_exit_callback,
        (PVOID)(ULONG_PTR)pid, INFINITE, WT_EXECUTEONLYONCE);

    return TRUE;
}

static BOOL whitelist_find_publisher(const WCHAR *certSubject, const WCHAR *exeName) {
    for (DWORD i = 0; i < g_svc.Whitelist.EntryCount; i++) {
        semantics_ar_whitelist_entry_t *e = &g_svc.Whitelist.Entries[i];
        if (e->TrustTier != SEMANTICS_AR_TRUST_TIER_PUBLISHER)
            continue;
        if (str_equal_ci(e->CertSubject, certSubject) &&
            str_equal_ci(e->TrustedExeName, exeName))
            return TRUE;
    }
    return FALSE;
}

static BOOL whitelist_verify_hash(const WCHAR *path) {
    BYTE fileHash[32];
    if (!svc_hash_file(path, fileHash))
        return FALSE;
    for (DWORD i = 0; i < g_svc.Whitelist.EntryCount; i++) {
        semantics_ar_whitelist_entry_t *e = &g_svc.Whitelist.Entries[i];
        if (e->TrustTier != SEMANTICS_AR_TRUST_TIER_HASH)
            continue;
        if (str_equal_ci(e->ExePath, path) && memcmp(e->FileHash, fileHash, 32) == 0)
            return TRUE;
    }
    return FALSE;
}

static void load_whitelist(void) {
    HANDLE f = CreateFileA(SEMANTICS_AR_SVC_WHITELIST_PATH,
                           GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;

    DWORD bytesRead;
    DWORD magic = 0;
    if (!ReadFile(f, &magic, sizeof(DWORD), &bytesRead, NULL) ||
        bytesRead != sizeof(DWORD) || magic != SEMANTICS_AR_WL_MAGIC) {
        CloseHandle(f);
        return;
    }

    DWORD count = 0;
    if (!ReadFile(f, &count, sizeof(DWORD), &bytesRead, NULL) ||
        bytesRead != sizeof(DWORD)) {
        CloseHandle(f);
        return;
    }
    if (count > SEMANTICS_AR_MAX_WHITELIST_ENTRIES)
        count = SEMANTICS_AR_MAX_WHITELIST_ENTRIES;

    for (DWORD i = 0; i < count; i++) {
        if (!ReadFile(f, &g_svc.Whitelist.Entries[i],
                      sizeof(semantics_ar_whitelist_entry_t), &bytesRead, NULL) ||
            bytesRead != sizeof(semantics_ar_whitelist_entry_t))
            break;
        g_svc.Whitelist.EntryCount = i + 1;
    }

    CloseHandle(f);
}

void svc_whitelist_init(void) {
    InitializeCriticalSectionAndSpinCount(&g_svc.Whitelist.Lock, 0x400);
    g_svc.Whitelist.EntryCount = 0;
    memset(g_svc.Whitelist.Active, 0, sizeof(g_svc.Whitelist.Active));
    load_whitelist();
}

void svc_whitelist_cleanup(void) {
    HANDLE waitHandles[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
    HANDLE procHandles[SEMANTICS_AR_MAX_ACTIVE_PROCESSES];
    DWORD count = 0;

    EnterCriticalSection(&g_svc.Whitelist.Lock);
    for (DWORD i = 0; i < SEMANTICS_AR_MAX_ACTIVE_PROCESSES; i++) {
        if (g_svc.Whitelist.Active[i].Valid) {
            waitHandles[count] = g_svc.Whitelist.Active[i].WaitHandle;
            procHandles[count] = g_svc.Whitelist.Active[i].ProcessHandle;
            g_svc.Whitelist.Active[i].Valid = FALSE;
            g_svc.Whitelist.Active[i].WaitHandle = NULL;
            g_svc.Whitelist.Active[i].ProcessHandle = NULL;
            count++;
        }
    }
    LeaveCriticalSection(&g_svc.Whitelist.Lock);

    for (DWORD i = 0; i < count; i++) {
        if (waitHandles[i])
            UnregisterWaitEx(waitHandles[i], INVALID_HANDLE_VALUE);
        if (procHandles[i])
            CloseHandle(procHandles[i]);
    }

    DeleteCriticalSection(&g_svc.Whitelist.Lock);
}

int svc_whitelist_check_and_verify(DWORD pid) {
    WCHAR currentPath[MAX_PATH];

    EnterCriticalSection(&g_svc.Whitelist.Lock);

    semantics_ar_active_process_t *proc = find_active(pid);
    if (proc) {
        if (!get_process_image_path(pid, currentPath, MAX_PATH) ||
            !str_equal_ci(currentPath, proc->ExePath)) {
            if (proc->ProcessHandle) {
                LONGLONG ct = get_process_creation_time(proc->ProcessHandle);
                push_remove_pid(proc->Pid, ct);
            }
            proc->Valid = FALSE;
            proc = NULL;
        }
    }

    if (proc) {
        LeaveCriticalSection(&g_svc.Whitelist.Lock);
        return SEMANTICS_AR_WL_VERIFIED;
    }

    if (!get_process_image_path(pid, currentPath, MAX_PATH)) {
        LeaveCriticalSection(&g_svc.Whitelist.Lock);
        return SEMANTICS_AR_WL_NOT_LISTED;
    }

    if (is_lolbin(currentPath)) {
        LeaveCriticalSection(&g_svc.Whitelist.Lock);
        return SEMANTICS_AR_WL_NOT_LISTED;
    }

    const WCHAR *exeName = extract_exe_name(currentPath);

    WCHAR certSubject[256] = {};
    if (verify_cert_subject(currentPath, certSubject, 256)) {
        if (whitelist_find_publisher(certSubject, exeName)) {
            activate_process(pid, currentPath, SEMANTICS_AR_TRUST_TIER_PUBLISHER);
            LeaveCriticalSection(&g_svc.Whitelist.Lock);
            return SEMANTICS_AR_WL_VERIFIED;
        }
    }

    if (whitelist_verify_hash(currentPath)) {
        activate_process(pid, currentPath, SEMANTICS_AR_TRUST_TIER_HASH);
        LeaveCriticalSection(&g_svc.Whitelist.Lock);
        return SEMANTICS_AR_WL_VERIFIED;
    }

    LeaveCriticalSection(&g_svc.Whitelist.Lock);
    return SEMANTICS_AR_WL_NOT_LISTED;
}

void svc_whitelist_scan_running(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID <= 4)
                continue;

            WCHAR path[MAX_PATH];
            if (!get_process_image_path(pe.th32ProcessID, path, MAX_PATH))
                continue;
            if (is_lolbin(path))
                continue;

            const WCHAR *exeName = extract_exe_name(path);

            EnterCriticalSection(&g_svc.Whitelist.Lock);
            if (find_active(pe.th32ProcessID)) {
                LeaveCriticalSection(&g_svc.Whitelist.Lock);
                continue;
            }

            BOOL matched = FALSE;
            WCHAR certSubject[256] = {};
            if (verify_cert_subject(path, certSubject, 256)) {
                if (whitelist_find_publisher(certSubject, exeName)) {
                    activate_process(pe.th32ProcessID, path, SEMANTICS_AR_TRUST_TIER_PUBLISHER);
                    matched = TRUE;
                }
            }

            if (!matched && whitelist_verify_hash(path))
                activate_process(pe.th32ProcessID, path, SEMANTICS_AR_TRUST_TIER_HASH);

            LeaveCriticalSection(&g_svc.Whitelist.Lock);
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
}