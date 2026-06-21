#include "driver_internal.h"

semantics_ar_process_monitor_t *semantics_ar_monitor_find(ULONG Pid)
{
    if (!semantics_ar_globals.ProcessMonitors)
        return NULL;
    for (LONG i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid == Pid)
            return &semantics_ar_globals.ProcessMonitors[i];
    }
    return NULL;
}

VOID semantics_ar_monitor_release(semantics_ar_process_monitor_t *Monitor)
{
    if (Monitor->CreatedFileRing)
        ExFreePoolWithTag(Monitor->CreatedFileRing, SEMANTICS_AR_POOL_TAG_PG);
    RtlZeroMemory(Monitor, sizeof(*Monitor));
}

static VOID monitor_init_slot(semantics_ar_process_monitor_t *Slot, ULONG Pid)
{
    RtlZeroMemory(Slot, sizeof(*Slot));
    Slot->Pid = Pid;
    Slot->State = SEMANTICS_AR_PROC_TRACKED;
    KeQuerySystemTimePrecise(&Slot->FirstDetectionTime);
    Slot->CreatedFileRing = (PUINT64)ExAllocatePoolZero(
        PagedPool, SEMANTICS_AR_CREATED_FILE_RING_SIZE * sizeof(UINT64),
        SEMANTICS_AR_POOL_TAG_PG);
}

semantics_ar_process_monitor_t *semantics_ar_monitor_allocate(ULONG Pid)
{
    LONG i;
    LONG oldestIdx = -1;

    for (i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid == Pid)
            return &semantics_ar_globals.ProcessMonitors[i];
    }

    for (i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid == 0) {
            monitor_init_slot(&semantics_ar_globals.ProcessMonitors[i], Pid);
            return &semantics_ar_globals.ProcessMonitors[i];
        }
    }

    for (i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].State == SEMANTICS_AR_PROC_TRACKED) {
            if (oldestIdx < 0 ||
                semantics_ar_globals.ProcessMonitors[i].FirstDetectionTime.QuadPart 
                semantics_ar_globals.ProcessMonitors[oldestIdx].FirstDetectionTime.QuadPart)
                oldestIdx = i;
        }
    }
    if (oldestIdx >= 0) {
        semantics_ar_process_monitor_t *slot = &semantics_ar_globals.ProcessMonitors[oldestIdx];
        semantics_ar_monitor_release(slot);
        monitor_init_slot(slot, Pid);
        return slot;
    }

    return NULL;
}

BOOLEAN semantics_ar_file_self_created(ULONG Pid, UINT64 FileId)
{
    if (Pid == 0 || FileId == 0 || !semantics_ar_globals.CreateRingEntries)
        return FALSE;

    BOOLEAN found = FALSE;
    for (LONG i = 0; i < SEMANTICS_AR_CREATE_RING_SIZE; i++) {
        semantics_ar_create_ring_entry_t *e = &semantics_ar_globals.CreateRingEntries[i];
        if (e->FileId == FileId && e->Pid == Pid) {
            found = TRUE;
            break;
        }
    }
    return found;
}

BOOLEAN semantics_ar_is_confirmed_process(ULONG Pid)
{
    if (!semantics_ar_globals.ProcessMonitors)
        return FALSE;
    for (LONG i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid == Pid &&
            semantics_ar_globals.ProcessMonitors[i].State == SEMANTICS_AR_PROC_CONFIRMED)
            return TRUE;
    }
    return FALSE;
}

static VOID propagate_confirmed_locked(VOID)
{
    BOOLEAN changed;
    do {
        changed = FALSE;
        for (LONG i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
            semantics_ar_process_monitor_t *s = &semantics_ar_globals.ProcessMonitors[i];
            if (s->Pid != 0 && s->State != SEMANTICS_AR_PROC_INACTIVE &&
                s->State < SEMANTICS_AR_PROC_CONFIRMED && s->ParentPid != 0) {
                semantics_ar_process_monitor_t *p = semantics_ar_monitor_find(s->ParentPid);
                if (p && p->State == SEMANTICS_AR_PROC_CONFIRMED) {
                    s->State = SEMANTICS_AR_PROC_CONFIRMED;
                    changed = TRUE;
                }
            }
        }
    } while (changed);
}

VOID semantics_ar_confirm_process_tree(ULONG TargetPid)
{
    ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);

    semantics_ar_process_monitor_t *slot = semantics_ar_monitor_find(TargetPid);
    if (!slot)
        slot = semantics_ar_monitor_allocate(TargetPid);
    if (slot)
        slot->State = SEMANTICS_AR_PROC_CONFIRMED;

    propagate_confirmed_locked();

    ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    InterlockedExchange(&semantics_ar_globals.ConfirmedActive, 1);
}

VOID semantics_ar_release_confirmed_tree_locked(ULONG TargetPid)
{
    ULONG pidsToRelease[SEMANTICS_AR_MAX_PROCESS_MONITORS];
    LONG releaseCount = 0;
    LONG i, j;

    for (i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
        if (semantics_ar_globals.ProcessMonitors[i].Pid == TargetPid &&
            semantics_ar_globals.ProcessMonitors[i].State == SEMANTICS_AR_PROC_CONFIRMED) {
            pidsToRelease[releaseCount++] = TargetPid;
            break;
        }
    }
    if (releaseCount == 0)
        return;

    BOOLEAN changed;
    do {
        changed = FALSE;
        for (i = 0; i < SEMANTICS_AR_MAX_PROCESS_MONITORS; i++) {
            semantics_ar_process_monitor_t *s = &semantics_ar_globals.ProcessMonitors[i];
            if (s->Pid == 0 || s->State != SEMANTICS_AR_PROC_CONFIRMED || s->ParentPid == 0)
                continue;
            BOOLEAN parentInList = FALSE;
            for (j = 0; j < releaseCount; j++)
                if (s->ParentPid == pidsToRelease[j]) { parentInList = TRUE; break; }
            if (!parentInList)
                continue;
            BOOLEAN alreadyInList = FALSE;
            for (j = 0; j < releaseCount; j++)
                if (pidsToRelease[j] == s->Pid) { alreadyInList = TRUE; break; }
            if (!alreadyInList && releaseCount < SEMANTICS_AR_MAX_PROCESS_MONITORS) {
                pidsToRelease[releaseCount++] = s->Pid;
                changed = TRUE;
            }
        }
    } while (changed);

    for (j = 0; j < releaseCount; j++) {
        semantics_ar_process_monitor_t *s = semantics_ar_monitor_find(pidsToRelease[j]);
        if (s)
            semantics_ar_monitor_release(s);
    }
}

VOID semantics_ar_associate_child_locked(ULONG ChildPid, ULONG OriginatorPid)
{
    if (ChildPid == 0 || OriginatorPid == 0 || ChildPid == OriginatorPid)
        return;

    semantics_ar_process_monitor_t *originator = semantics_ar_monitor_find(OriginatorPid);
    if (!originator || originator->State == SEMANTICS_AR_PROC_INACTIVE)
        return;

    ULONG originState = originator->State;
    semantics_ar_process_monitor_t *child = semantics_ar_monitor_find(ChildPid);
    if (child && child->Pid != 0) {
        if (child->State < originState) {
            child->State = originState;
            child->ParentPid = OriginatorPid;
        }
    } else {
        child = semantics_ar_monitor_allocate(ChildPid);
        if (child) {
            child->State = originState;
            child->ParentPid = OriginatorPid;
        }
    }

    if (originState == SEMANTICS_AR_PROC_CONFIRMED) {
        propagate_confirmed_locked();
        InterlockedExchange(&semantics_ar_globals.ConfirmedActive, 1);
    }
}

VOID semantics_ar_process_notify(
    PEPROCESS Process,
    HANDLE ProcessId,
    PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    ULONG pid = HandleToULong(ProcessId);
    UNREFERENCED_PARAMETER(Process);

    if (CreateInfo != NULL) {
        ULONG parentPid = HandleToULong(CreateInfo->ParentProcessId);
        ULONG creatorPid = HandleToULong(CreateInfo->CreatingThreadId.UniqueProcess);

        ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);

        semantics_ar_process_monitor_t *origin = NULL;
        if (creatorPid != parentPid && creatorPid > 4) {
            origin = semantics_ar_monitor_find(creatorPid);
            if (origin && origin->State == SEMANTICS_AR_PROC_INACTIVE)
                origin = NULL;
        }
        if (!origin) {
            origin = semantics_ar_monitor_find(parentPid);
            if (origin && origin->State == SEMANTICS_AR_PROC_INACTIVE)
                origin = NULL;
        }

        if (origin) {
            semantics_ar_process_monitor_t *child = semantics_ar_monitor_allocate(pid);
            if (child) {
                child->State = origin->State;
                child->ParentPid = origin->Pid;
            }
        }

        ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    } else {
        ExAcquireFastMutex(&semantics_ar_globals.ProcessMonitorLock);
        semantics_ar_process_monitor_t *slot = semantics_ar_monitor_find(pid);
        if (slot) {
            if (slot->State == SEMANTICS_AR_PROC_TRACKED)
                semantics_ar_monitor_release(slot);
            else if (slot->State == SEMANTICS_AR_PROC_CONFIRMED)
                slot->Flags |= SEMANTICS_AR_MONITOR_FLAG_PROCESS_EXITED;
        }
        ExReleaseFastMutex(&semantics_ar_globals.ProcessMonitorLock);
    }
}