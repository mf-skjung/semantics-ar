#include "service_internal.h"
#include <string.h>

static void send_restore_complete(DWORD targetPid) {
    if (!g_svc.Port)
        return;
    struct {
        semantics_ar_msg_header_t hdr;
        semantics_ar_confirm_payload_t payload;
    } msg;
    msg.hdr.message_type = SEMANTICS_AR_MSG_RESTORE_COMPLETE;
    msg.payload.target_pid = targetPid;
    DWORD bytesReturned;
    FilterSendMessage(g_svc.Port, &msg, sizeof(msg), NULL, 0, &bytesReturned);
}

void svc_add_pending_restore(DWORD rootPid, const DWORD *treePids, DWORD treeCount) {
    AcquireSRWLockExclusive(&g_svc.PendingRestoreLock);
    for (DWORD i = 0; i < SEMANTICS_AR_MAX_PENDING_RESTORES; i++) {
        if (!g_svc.PendingRestores[i].Active) {
            g_svc.PendingRestores[i].Active = TRUE;
            g_svc.PendingRestores[i].RootPid = rootPid;
            DWORD count = treeCount;
            if (count > SEMANTICS_AR_MAX_ACTIVE_PROCESSES)
                count = SEMANTICS_AR_MAX_ACTIVE_PROCESSES;
            memcpy(g_svc.PendingRestores[i].TreePids, treePids, count * sizeof(DWORD));
            g_svc.PendingRestores[i].TreeCount = count;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_svc.PendingRestoreLock);
}

void svc_restore_all_volumes(const DWORD *targetPids, DWORD pidCount) {
    WCHAR prefixes[SEMANTICS_AR_MAX_VOLUME_MAPPINGS][MAX_PATH];
    DWORD prefixCount = 0;

    AcquireSRWLockShared(&g_svc.VolumeCache.Lock);
    for (DWORD i = 0; i < g_svc.VolumeCache.Count; i++) {
        if (!g_svc.VolumeCache.Entries[i].Valid)
            continue;
        BOOL dup = FALSE;
        for (DWORD j = 0; j < prefixCount; j++) {
            if (_wcsicmp(prefixes[j], g_svc.VolumeCache.Entries[i].DosPrefix) == 0) {
                dup = TRUE;
                break;
            }
        }
        if (dup)
            continue;
        if (prefixCount < SEMANTICS_AR_MAX_VOLUME_MAPPINGS) {
            wcsncpy_s(prefixes[prefixCount], MAX_PATH,
                      g_svc.VolumeCache.Entries[i].DosPrefix, _TRUNCATE);
            prefixCount++;
        }
    }
    ReleaseSRWLockShared(&g_svc.VolumeCache.Lock);

    for (DWORD i = 0; i < prefixCount; i++)
        svc_journal_restore(prefixes[i], targetPids, pidCount);
}

int svc_execute_restore(DWORD rootPid) {
    semantics_ar_pending_restore_t entry;
    BOOL found = FALSE;

    AcquireSRWLockExclusive(&g_svc.PendingRestoreLock);
    for (DWORD i = 0; i < SEMANTICS_AR_MAX_PENDING_RESTORES; i++) {
        if (g_svc.PendingRestores[i].Active &&
            g_svc.PendingRestores[i].RootPid == rootPid) {
            memcpy(&entry, &g_svc.PendingRestores[i], sizeof(entry));
            g_svc.PendingRestores[i].Active = FALSE;
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_svc.PendingRestoreLock);

    if (!found)
        return -1;

    svc_restore_all_volumes(entry.TreePids, entry.TreeCount);
    svc_cleanup_all_volumes(entry.TreePids, entry.TreeCount);
    send_restore_complete(rootPid);

    return 0;
}