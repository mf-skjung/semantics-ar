#include "service_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void svc_volume_cache_init(void) {
    InitializeSRWLock(&g_svc.VolumeCache.Lock);
    g_svc.VolumeCache.Count = 0;
    memset(g_svc.VolumeCache.Entries, 0, sizeof(g_svc.VolumeCache.Entries));
}

void svc_volume_cache_build(void) {
    AcquireSRWLockExclusive(&g_svc.VolumeCache.Lock);
    g_svc.VolumeCache.Count = 0;

    WCHAR driveStr[3] = L"A:";
    WCHAR deviceName[256];

    for (WCHAR c = L'A'; c <= L'Z'; c++) {
        if (g_svc.VolumeCache.Count >= SEMANTICS_AR_MAX_VOLUME_MAPPINGS)
            break;
        driveStr[0] = c;
        if (QueryDosDeviceW(driveStr, deviceName, 256) == 0)
            continue;
        DWORD idx = g_svc.VolumeCache.Count;
        wcsncpy_s(g_svc.VolumeCache.Entries[idx].NtDevicePath, 256, deviceName, _TRUNCATE);
        _snwprintf_s(g_svc.VolumeCache.Entries[idx].DosPrefix, MAX_PATH, _TRUNCATE,
                     L"%s", driveStr);
        g_svc.VolumeCache.Entries[idx].Valid = TRUE;
        g_svc.VolumeCache.Count++;
    }

    WCHAR volumeGuid[MAX_PATH];
    HANDLE hFind = FindFirstVolumeW(volumeGuid, MAX_PATH);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (g_svc.VolumeCache.Count >= SEMANTICS_AR_MAX_VOLUME_MAPPINGS)
                break;
            size_t guidLen = wcslen(volumeGuid);
            if (guidLen < 5)
                continue;
            WCHAR stripped[256];
            wcsncpy_s(stripped, 256, volumeGuid + 4, _TRUNCATE);
            size_t sLen = wcslen(stripped);
            if (sLen > 0 && stripped[sLen - 1] == L'\\')
                stripped[sLen - 1] = L'\0';

            WCHAR ntDev[256];
            if (QueryDosDeviceW(stripped, ntDev, 256) == 0)
                continue;

            BOOL alreadyMapped = FALSE;
            for (DWORD i = 0; i < g_svc.VolumeCache.Count; i++) {
                if (g_svc.VolumeCache.Entries[i].Valid &&
                    _wcsicmp(g_svc.VolumeCache.Entries[i].NtDevicePath, ntDev) == 0) {
                    alreadyMapped = TRUE;
                    break;
                }
            }
            if (alreadyMapped)
                continue;

            WCHAR mountPaths[1024];
            DWORD returnLen = 0;
            if (!GetVolumePathNamesForVolumeNameW(volumeGuid, mountPaths, 1024, &returnLen))
                continue;
            if (mountPaths[0] == L'\0')
                continue;

            DWORD idx = g_svc.VolumeCache.Count;
            wcsncpy_s(g_svc.VolumeCache.Entries[idx].NtDevicePath, 256, ntDev, _TRUNCATE);
            wcsncpy_s(g_svc.VolumeCache.Entries[idx].DosPrefix, MAX_PATH, mountPaths, _TRUNCATE);
            size_t prefixLen = wcslen(g_svc.VolumeCache.Entries[idx].DosPrefix);
            if (prefixLen > 0 &&
                g_svc.VolumeCache.Entries[idx].DosPrefix[prefixLen - 1] == L'\\')
                g_svc.VolumeCache.Entries[idx].DosPrefix[prefixLen - 1] = L'\0';
            g_svc.VolumeCache.Entries[idx].Valid = TRUE;
            g_svc.VolumeCache.Count++;
        } while (FindNextVolumeW(hFind, volumeGuid, MAX_PATH));
        FindVolumeClose(hFind);
    }

    ReleaseSRWLockExclusive(&g_svc.VolumeCache.Lock);
}

BOOL convert_nt_to_dos_path(const WCHAR *ntPath, WCHAR *dosPath, DWORD maxChars) {
    AcquireSRWLockShared(&g_svc.VolumeCache.Lock);
    for (DWORD i = 0; i < g_svc.VolumeCache.Count; i++) {
        if (!g_svc.VolumeCache.Entries[i].Valid)
            continue;
        size_t deviceLen = wcslen(g_svc.VolumeCache.Entries[i].NtDevicePath);
        if (deviceLen == 0)
            continue;
        if (_wcsnicmp(ntPath, g_svc.VolumeCache.Entries[i].NtDevicePath, deviceLen) == 0 &&
            ntPath[deviceLen] == L'\\') {
            int r = _snwprintf_s(dosPath, maxChars, _TRUNCATE, L"%s%s",
                                 g_svc.VolumeCache.Entries[i].DosPrefix, ntPath + deviceLen);
            ReleaseSRWLockShared(&g_svc.VolumeCache.Lock);
            return (r > 0);
        }
    }
    ReleaseSRWLockShared(&g_svc.VolumeCache.Lock);
    return FALSE;
}