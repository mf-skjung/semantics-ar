#include "service_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int journal_integrity_ok(const semantics_ar_journal_entry_t *entry) {
    (void)entry;
    return 1;
}

static BOOL is_target_pid(DWORD pid, const DWORD *targetPids, DWORD pidCount) {
    for (DWORD i = 0; i < pidCount; i++)
        if (targetPids[i] == pid)
            return TRUE;
    return FALSE;
}

static void send_shadow_space_freed(const WCHAR *volumeDosPrefix, uint64_t bytesFreed) {
    if (bytesFreed == 0 || !g_svc.Port)
        return;

    WCHAR driveStr[3];
    driveStr[0] = volumeDosPrefix[0];
    driveStr[1] = L':';
    driveStr[2] = L'\0';

    WCHAR deviceName[256];
    if (QueryDosDeviceW(driveStr, deviceName, 256) == 0)
        return;

    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_space_freed_payload_t payload;
    } msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.message_type = SEMANTICS_AR_MSG_SHADOW_SPACE_FREED;
    msg.payload.bytes_freed = bytesFreed;
    wcsncpy_s((WCHAR *)msg.payload.volume_nt_path, SEMANTICS_AR_PROTO_PATH_MAX,
              deviceName, _TRUNCATE);

    DWORD bytesReturned;
    FilterSendMessage(g_svc.Port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}

static void send_shadow_usage_set(const WCHAR *volumeDosPrefix, int64_t actualBytes) {
    if (!g_svc.Port)
        return;

    WCHAR driveStr[3];
    driveStr[0] = volumeDosPrefix[0];
    driveStr[1] = L':';
    driveStr[2] = L'\0';

    WCHAR deviceName[256];
    if (QueryDosDeviceW(driveStr, deviceName, 256) == 0)
        return;

    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_shadow_usage_set_payload_t payload;
    } msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.message_type = SEMANTICS_AR_MSG_SHADOW_USAGE_SET;
    msg.payload.actual_bytes_used = actualBytes;
    wcsncpy_s((WCHAR *)msg.payload.volume_nt_path, SEMANTICS_AR_PROTO_PATH_MAX,
              deviceName, _TRUNCATE);

    DWORD bytesReturned;
    FilterSendMessage(g_svc.Port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}

static uint64_t enumerate_shadow_dir_size(const WCHAR *dirPath, DWORD clusterSize) {
    WCHAR searchPath[MAX_PATH];
    _snwprintf_s(searchPath, MAX_PATH, _TRUNCATE, L"%s\\*", dirPath);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(searchPath, FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, NULL,
                                    FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    uint64_t total = 0;
    LONGLONG cs = (LONGLONG)clusterSize;
    if (cs == 0)
        cs = 4096;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        ULARGE_INTEGER sz;
        sz.LowPart = fd.nFileSizeLow;
        sz.HighPart = fd.nFileSizeHigh;
        LONGLONG aligned = ((LONGLONG)sz.QuadPart + cs - 1) & ~(cs - 1);
        total += (uint64_t)aligned;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return total;
}

static DWORD get_volume_cluster_size(const WCHAR *volumePrefix) {
    WCHAR rootPath[8];
    _snwprintf_s(rootPath, 8, _TRUNCATE, L"%s\\", volumePrefix);

    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    if (GetDiskFreeSpaceW(rootPath, &sectorsPerCluster, &bytesPerSector,
                          &freeClusters, &totalClusters)) {
        return sectorsPerCluster * bytesPerSector;
    }
    return 4096;
}

void svc_reconcile_shadow_usage(void) {
    WCHAR volumePrefix[3];
    for (WCHAR c = L'A'; c <= L'Z'; c++) {
        volumePrefix[0] = c;
        volumePrefix[1] = L':';
        volumePrefix[2] = L'\0';

        WCHAR shadowBase[MAX_PATH];
        _snwprintf_s(shadowBase, MAX_PATH, _TRUNCATE,
                     L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME, volumePrefix);
        if (GetFileAttributesW(shadowBase) == INVALID_FILE_ATTRIBUTES)
            continue;

        DWORD clusterSize = get_volume_cluster_size(volumePrefix);

        WCHAR overwritesDir[MAX_PATH];
        _snwprintf_s(overwritesDir, MAX_PATH, _TRUNCATE, L"%s\\overwrites", shadowBase);

        WCHAR deletesDir[MAX_PATH];
        _snwprintf_s(deletesDir, MAX_PATH, _TRUNCATE, L"%s\\deletes", shadowBase);

        uint64_t totalUsed = 0;
        totalUsed += enumerate_shadow_dir_size(overwritesDir, clusterSize);
        totalUsed += enumerate_shadow_dir_size(deletesDir, clusterSize);

        send_shadow_usage_set(volumePrefix, (int64_t)totalUsed);
    }
}

uint64_t svc_cleanup_shadow_files(const WCHAR *volumeDosPrefix,
                                  const DWORD *targetPids, DWORD pidCount) {
    if (!volumeDosPrefix || !targetPids || pidCount == 0)
        return 0;

    WCHAR journalPath[MAX_PATH];
    _snwprintf_s(journalPath, MAX_PATH, _TRUNCATE,
                 L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME L"\\journal.dat",
                 volumeDosPrefix);

    HANDLE hJournal = CreateFileW(journalPath, GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hJournal == INVALID_HANDLE_VALUE)
        return 0;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hJournal, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hJournal);
        return 0;
    }

    DWORD entrySize = (DWORD)sizeof(semantics_ar_journal_entry_t);
    LONGLONG validSize = (fileSize.QuadPart / entrySize) * entrySize;
    if (validSize == 0 || validSize > (LONGLONG)512 * 1024 * 1024) {
        CloseHandle(hJournal);
        return 0;
    }

    DWORD totalSize = (DWORD)validSize;
    uint8_t *buffer = (uint8_t *)malloc(totalSize);
    if (!buffer) {
        CloseHandle(hJournal);
        return 0;
    }

    DWORD bytesRead;
    if (!ReadFile(hJournal, buffer, totalSize, &bytesRead, NULL) || bytesRead != totalSize) {
        free(buffer);
        CloseHandle(hJournal);
        return 0;
    }
    CloseHandle(hJournal);

    DWORD entryCount = totalSize / entrySize;
    uint64_t totalFreed = 0;

    for (DWORD i = 0; i < entryCount; i++) {
        semantics_ar_journal_entry_t *entry =
            (semantics_ar_journal_entry_t *)(buffer + (size_t)i * entrySize);

        if (!journal_integrity_ok(entry))
            continue;
        if (!is_target_pid(entry->process_id, targetPids, pidCount))
            continue;

        WCHAR dosShadowPath[MAX_PATH];
        if (!convert_nt_to_dos_path((const WCHAR *)entry->shadow_file_path,
                                    dosShadowPath, MAX_PATH))
            continue;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(dosShadowPath, GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER sz;
            sz.LowPart = fad.nFileSizeLow;
            sz.HighPart = fad.nFileSizeHigh;
            if (DeleteFileW(dosShadowPath))
                totalFreed += sz.QuadPart;
        }
    }

    free(buffer);
    return totalFreed;
}

void svc_cleanup_all_volumes(const DWORD *targetPids, DWORD pidCount) {
    WCHAR volumePrefix[3];
    for (WCHAR c = L'A'; c <= L'Z'; c++) {
        volumePrefix[0] = c;
        volumePrefix[1] = L':';
        volumePrefix[2] = L'\0';

        WCHAR journalPath[MAX_PATH];
        _snwprintf_s(journalPath, MAX_PATH, _TRUNCATE,
                     L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME L"\\journal.dat",
                     volumePrefix);
        if (GetFileAttributesW(journalPath) == INVALID_FILE_ATTRIBUTES)
            continue;

        uint64_t freed = svc_cleanup_shadow_files(volumePrefix, targetPids, pidCount);
        send_shadow_space_freed(volumePrefix, freed);
    }
}

static int determine_eviction_priority(DWORD pid) {
    if (svc_whitelist_check_and_verify(pid) == SEMANTICS_AR_WL_VERIFIED)
        return SEMANTICS_AR_EVICT_PRIORITY_WHITELISTED;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc)
        return SEMANTICS_AR_EVICT_PRIORITY_TERMINATED;
    CloseHandle(hProc);

    return SEMANTICS_AR_EVICT_PRIORITY_ACTIVE;
}

typedef struct {
    DWORD entryIndex;
    int priority;
    int64_t timestamp;
} eviction_candidate_t;

static int eviction_compare(const void *a, const void *b) {
    const eviction_candidate_t *ca = (const eviction_candidate_t *)a;
    const eviction_candidate_t *cb = (const eviction_candidate_t *)b;
    if (ca->priority != cb->priority)
        return ca->priority - cb->priority;
    if (ca->timestamp < cb->timestamp) return -1;
    if (ca->timestamp > cb->timestamp) return 1;
    return 0;
}

void svc_evict_shadow_if_needed(const WCHAR *volumeDosPrefix) {
    WCHAR shadowBase[MAX_PATH];
    _snwprintf_s(shadowBase, MAX_PATH, _TRUNCATE,
                 L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME, volumeDosPrefix);
    if (GetFileAttributesW(shadowBase) == INVALID_FILE_ATTRIBUTES)
        return;

    DWORD clusterSize = get_volume_cluster_size(volumeDosPrefix);

    WCHAR overwritesDir[MAX_PATH];
    _snwprintf_s(overwritesDir, MAX_PATH, _TRUNCATE, L"%s\\overwrites", shadowBase);

    WCHAR deletesDir[MAX_PATH];
    _snwprintf_s(deletesDir, MAX_PATH, _TRUNCATE, L"%s\\deletes", shadowBase);

    uint64_t currentUsage = 0;
    currentUsage += enumerate_shadow_dir_size(overwritesDir, clusterSize);
    currentUsage += enumerate_shadow_dir_size(deletesDir, clusterSize);

    WCHAR rootPath[8];
    _snwprintf_s(rootPath, 8, _TRUNCATE, L"%s\\", volumeDosPrefix);
    ULARGE_INTEGER freeBytes, totalBytes, totalFree;
    if (!GetDiskFreeSpaceExW(rootPath, &freeBytes, &totalBytes, &totalFree))
        return;

    int64_t budget = (int64_t)(totalBytes.QuadPart * SEMANTICS_AR_SHADOW_PERCENT / 100);
    if (budget < SEMANTICS_AR_SHADOW_MIN_BYTES)
        budget = SEMANTICS_AR_SHADOW_MIN_BYTES;
    if (budget > SEMANTICS_AR_SHADOW_MAX_BYTES)
        budget = SEMANTICS_AR_SHADOW_MAX_BYTES;

    int64_t highMark = budget * SEMANTICS_AR_EVICTION_HIGH_PERCENT / 100;
    int64_t lowMark = budget * SEMANTICS_AR_EVICTION_LOW_PERCENT / 100;

    if ((int64_t)currentUsage <= highMark)
        return;

    WCHAR journalPath[MAX_PATH];
    _snwprintf_s(journalPath, MAX_PATH, _TRUNCATE,
                 L"%s\\journal.dat", shadowBase);

    HANDLE hJournal = CreateFileW(journalPath, GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hJournal == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hJournal, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hJournal);
        return;
    }

    DWORD entrySize = (DWORD)sizeof(semantics_ar_journal_entry_t);
    LONGLONG validSize = (fileSize.QuadPart / entrySize) * entrySize;
    if (validSize == 0 || validSize > (LONGLONG)512 * 1024 * 1024) {
        CloseHandle(hJournal);
        return;
    }

    DWORD totalSize = (DWORD)validSize;
    uint8_t *buffer = (uint8_t *)malloc(totalSize);
    if (!buffer) {
        CloseHandle(hJournal);
        return;
    }

    DWORD bytesRead;
    if (!ReadFile(hJournal, buffer, totalSize, &bytesRead, NULL) || bytesRead != totalSize) {
        free(buffer);
        CloseHandle(hJournal);
        return;
    }
    CloseHandle(hJournal);

    DWORD entryCount = totalSize / entrySize;
    eviction_candidate_t *candidates =
        (eviction_candidate_t *)malloc(entryCount * sizeof(eviction_candidate_t));
    if (!candidates) {
        free(buffer);
        return;
    }

    DWORD candidateCount = 0;
    for (DWORD i = 0; i < entryCount; i++) {
        semantics_ar_journal_entry_t *entry =
            (semantics_ar_journal_entry_t *)(buffer + (size_t)i * entrySize);
        if (!journal_integrity_ok(entry))
            continue;

        candidates[candidateCount].entryIndex = i;
        candidates[candidateCount].priority = determine_eviction_priority(entry->process_id);
        candidates[candidateCount].timestamp = entry->timestamp;
        candidateCount++;
    }

    qsort(candidates, candidateCount, sizeof(eviction_candidate_t), eviction_compare);

    uint64_t totalFreed = 0;
    for (DWORD c = 0; c < candidateCount; c++) {
        if ((int64_t)(currentUsage - totalFreed) <= lowMark)
            break;

        semantics_ar_journal_entry_t *entry =
            (semantics_ar_journal_entry_t *)(buffer + (size_t)candidates[c].entryIndex * entrySize);

        WCHAR dosShadowPath[MAX_PATH];
        if (!convert_nt_to_dos_path((const WCHAR *)entry->shadow_file_path,
                                    dosShadowPath, MAX_PATH))
            continue;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(dosShadowPath, GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER sz;
            sz.LowPart = fad.nFileSizeLow;
            sz.HighPart = fad.nFileSizeHigh;
            if (DeleteFileW(dosShadowPath))
                totalFreed += sz.QuadPart;
        }
    }

    free(candidates);
    free(buffer);

    send_shadow_space_freed(volumeDosPrefix, totalFreed);
}

DWORD WINAPI svc_eviction_thread(LPVOID param) {
    (void)param;

    while (WaitForSingleObject(g_svc.StopEvent, SEMANTICS_AR_EVICTION_POLL_MS) == WAIT_TIMEOUT) {
        WCHAR volumePrefix[3];
        for (WCHAR c = L'A'; c <= L'Z'; c++) {
            if (WaitForSingleObject(g_svc.StopEvent, 0) == WAIT_OBJECT_0)
                return 0;

            volumePrefix[0] = c;
            volumePrefix[1] = L':';
            volumePrefix[2] = L'\0';

            WCHAR shadowBase[MAX_PATH];
            _snwprintf_s(shadowBase, MAX_PATH, _TRUNCATE,
                         L"%s\\" SEMANTICS_AR_SVC_SHADOW_DIR_NAME, volumePrefix);
            if (GetFileAttributesW(shadowBase) == INVALID_FILE_ATTRIBUTES)
                continue;

            svc_evict_shadow_if_needed(volumePrefix);
        }
    }

    return 0;
}