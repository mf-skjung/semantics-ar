#include "state.h"
#include "driver.h"
#include "eventlog.h"

static ULONG SarIdentityHash(_In_ HANDLE ProcessId, _In_ ULONG BucketCount)
{
    ULONG_PTR value = (ULONG_PTR)ProcessId;
    value ^= value >> 13;
    value *= 0x9E3779B1u;
    return (ULONG)(value % BucketCount);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarStateCreate(_Outptr_ PSAR_STATE *State)
{
    PSAR_STATE state;
    sar_identity_t *storage;
    uint64_t *first_seen;
    LIST_ENTRY *buckets;
    ULONG i;

    *State = NULL;

    state = (PSAR_STATE)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_STATE), SAR_POOL_TAG_STATE);
    if (state == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    storage = (sar_identity_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                sizeof(sar_identity_t) * SAR_WHITELIST_CAPACITY,
                                                SAR_POOL_TAG_WHITELIST);
    if (storage == NULL) {
        ExFreePoolWithTag(state, SAR_POOL_TAG_STATE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    first_seen = (uint64_t *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                             sizeof(uint64_t) * SAR_WHITELIST_CAPACITY,
                                             SAR_POOL_TAG_WHITELIST);
    if (first_seen == NULL) {
        ExFreePoolWithTag(storage, SAR_POOL_TAG_WHITELIST);
        ExFreePoolWithTag(state, SAR_POOL_TAG_STATE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    buckets = (LIST_ENTRY *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                            sizeof(LIST_ENTRY) * SAR_IDENTITY_BUCKET_COUNT,
                                            SAR_POOL_TAG_IDENTITY);
    if (buckets == NULL) {
        ExFreePoolWithTag(first_seen, SAR_POOL_TAG_WHITELIST);
        ExFreePoolWithTag(storage, SAR_POOL_TAG_WHITELIST);
        ExFreePoolWithTag(state, SAR_POOL_TAG_STATE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    state->mode = SEMANTICS_AR_MODE_AUDIT;
    state->captured_key_count = 0;
    FltInitializePushLock(&state->whitelist_lock);
    FltInitializePushLock(&state->identity_lock);
    state->whitelist_storage = storage;
    state->whitelist_first_seen = first_seen;
    sar_whitelist_init(&state->whitelist, storage, first_seen, SAR_WHITELIST_CAPACITY);
    state->identity_buckets = buckets;
    state->identity_bucket_count = SAR_IDENTITY_BUCKET_COUNT;
    for (i = 0; i < SAR_IDENTITY_BUCKET_COUNT; i++)
        InitializeListHead(&buckets[i]);

    *State = state;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarStateDestroy(_Inout_ PSAR_STATE State)
{
    ULONG i;

    if (State == NULL)
        return;

    FltAcquirePushLockExclusive(&State->identity_lock);
    for (i = 0; i < State->identity_bucket_count; i++) {
        while (!IsListEmpty(&State->identity_buckets[i])) {
            PLIST_ENTRY entry = RemoveHeadList(&State->identity_buckets[i]);
            PSAR_IDENTITY_ENTRY id = CONTAINING_RECORD(entry, SAR_IDENTITY_ENTRY, link);
            if (id->process != NULL)
                ObDereferenceObject(id->process);
            ExFreePoolWithTag(id, SAR_POOL_TAG_IDENTITY);
        }
    }
    FltReleasePushLock(&State->identity_lock);

    FltDeletePushLock(&State->identity_lock);
    FltDeletePushLock(&State->whitelist_lock);

    ExFreePoolWithTag(State->identity_buckets, SAR_POOL_TAG_IDENTITY);
    ExFreePoolWithTag(State->whitelist_first_seen, SAR_POOL_TAG_WHITELIST);
    ExFreePoolWithTag(State->whitelist_storage, SAR_POOL_TAG_WHITELIST);
    ExFreePoolWithTag(State, SAR_POOL_TAG_STATE);
}

_IRQL_requires_max_(APC_LEVEL)
ULONG SarStateModeGet(_In_ PSAR_STATE State)
{
    return (ULONG)InterlockedCompareExchange(&State->mode, 0, 0);
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateModeSet(_Inout_ PSAR_STATE State, _In_ ULONG RequestedMode)
{
    sar_mode_state_t local;

    local.mode = (uint32_t)InterlockedCompareExchange(&State->mode, 0, 0);
    if (!sar_mode_set(&local, RequestedMode))
        return FALSE;
    InterlockedExchange(&State->mode, (LONG)local.mode);
    SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_MODE_CHANGED, 0);
    return TRUE;
}

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistAdd(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity)
{
    sar_wl_status_t status;
    LARGE_INTEGER now;

    KeQuerySystemTimePrecise(&now);

    FltAcquirePushLockExclusive(&State->whitelist_lock);
    status = sar_whitelist_add(&State->whitelist, Identity, (uint64_t)now.QuadPart);
    FltReleasePushLock(&State->whitelist_lock);
    if (status == SAR_WL_OK)
        SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_WHITELIST_ADDED, 0);
    return status;
}

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistEnumerate(_In_ PSAR_STATE State, _In_ uint32_t Index,
                                           _Out_ sar_identity_t *OutId,
                                           _Out_ UINT64 *OutFirstSeen,
                                           _Out_ uint32_t *OutTotal)
{
    sar_wl_status_t status;
    uint64_t first_seen = 0;

    FltAcquirePushLockShared(&State->whitelist_lock);
    *OutTotal = State->whitelist.count;
    status = sar_whitelist_enumerate(&State->whitelist, Index, OutId, &first_seen);
    FltReleasePushLock(&State->whitelist_lock);

    *OutFirstSeen = first_seen;
    return status;
}

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistRemove(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity)
{
    sar_wl_status_t status;

    FltAcquirePushLockExclusive(&State->whitelist_lock);
    status = sar_whitelist_remove(&State->whitelist, Identity);
    FltReleasePushLock(&State->whitelist_lock);
    if (status == SAR_WL_OK)
        SarEventLogRecord(g_sar.eventlog, SAR_EVENT_CLASS_WHITELIST_REMOVED, 0);
    return status;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateWhitelistMatch(_In_ PSAR_STATE State, _In_ const sar_identity_t *Identity)
{
    int hit;

    FltAcquirePushLockShared(&State->whitelist_lock);
    hit = sar_whitelist_match(&State->whitelist, Identity);
    FltReleasePushLock(&State->whitelist_lock);
    return hit != 0 ? TRUE : FALSE;
}

static PSAR_IDENTITY_ENTRY SarIdentityFindLocked(_In_ PSAR_STATE State, _In_ HANDLE ProcessId)
{
    ULONG bucket = SarIdentityHash(ProcessId, State->identity_bucket_count);
    PLIST_ENTRY head = &State->identity_buckets[bucket];
    PLIST_ENTRY cursor;

    for (cursor = head->Flink; cursor != head; cursor = cursor->Flink) {
        PSAR_IDENTITY_ENTRY id = CONTAINING_RECORD(cursor, SAR_IDENTITY_ENTRY, link);
        if (id->process_id == ProcessId)
            return id;
    }
    return NULL;
}

static PSAR_IDENTITY_ENTRY SarIdentityFindByProcessLocked(_In_ PSAR_STATE State, _In_ PEPROCESS Process)
{
    HANDLE pid = PsGetProcessId(Process);
    ULONG bucket = SarIdentityHash(pid, State->identity_bucket_count);
    PLIST_ENTRY head = &State->identity_buckets[bucket];
    PLIST_ENTRY cursor;

    for (cursor = head->Flink; cursor != head; cursor = cursor->Flink) {
        PSAR_IDENTITY_ENTRY id = CONTAINING_RECORD(cursor, SAR_IDENTITY_ENTRY, link);
        if (id->process == Process)
            return id;
    }
    return NULL;
}

static BOOLEAN SarIdentityIsInterpreter(_In_ const sar_identity_t *Identity)
{
    return sar_identity_is_interpreter(Identity->image_path) ? TRUE : FALSE;
}

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS SarStateIdentityInsert(_Inout_ PSAR_STATE State,
                                _In_ HANDLE ProcessId,
                                _In_ PEPROCESS Process,
                                _In_opt_ const sar_identity_t *Identity,
                                _In_ BOOLEAN IdentityValid,
                                _In_ BOOLEAN SubsystemProcess,
                                _In_ BOOLEAN ProtectedTrusted)
{
    PSAR_IDENTITY_ENTRY entry;
    ULONG bucket;
    int whitelist_hit = 0;

    entry = (PSAR_IDENTITY_ENTRY)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_IDENTITY_ENTRY),
                                                 SAR_POOL_TAG_IDENTITY);
    if (entry == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    entry->process_id = ProcessId;
    entry->process = Process;
    entry->start_key = PsGetProcessStartKey(Process);
    entry->identity_valid = IdentityValid;
    entry->subsystem_process = SubsystemProcess;
    entry->protected_trusted = ProtectedTrusted;
    RtlZeroMemory(&entry->identity, sizeof(entry->identity));
    if (Identity != NULL)
        RtlCopyMemory(&entry->identity, Identity, sizeof(entry->identity));

    entry->phantom_evidence = 0;

    if (IdentityValid && Identity != NULL)
        whitelist_hit = SarStateWhitelistMatch(State, Identity) ? 1 : 0;
    entry->id_state = ProtectedTrusted ? SAR_IDSTATE_EXEMPT
                                       : sar_identity_resolve(IdentityValid ? 1 : 0, whitelist_hit);

    FltAcquirePushLockExclusive(&State->identity_lock);
    if (SarIdentityFindLocked(State, ProcessId) != NULL) {
        FltReleasePushLock(&State->identity_lock);
        ExFreePoolWithTag(entry, SAR_POOL_TAG_IDENTITY);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    bucket = SarIdentityHash(ProcessId, State->identity_bucket_count);
    InsertHeadList(&State->identity_buckets[bucket], &entry->link);
    FltReleasePushLock(&State->identity_lock);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(APC_LEVEL)
VOID SarStateIdentityRemove(_Inout_ PSAR_STATE State, _In_ HANDLE ProcessId)
{
    PSAR_IDENTITY_ENTRY entry;

    FltAcquirePushLockExclusive(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL)
        RemoveEntryList(&entry->link);
    FltReleasePushLock(&State->identity_lock);

    if (entry != NULL) {
        if (entry->process != NULL)
            ObDereferenceObject(entry->process);
        ExFreePoolWithTag(entry, SAR_POOL_TAG_IDENTITY);
    }
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateIdentityQuery(_In_ PSAR_STATE State, _In_ HANDLE ProcessId,
                             _Out_ UINT64 *StartKey, _Out_ sar_id_state_t *IdState)
{
    PSAR_IDENTITY_ENTRY entry;
    BOOLEAN found = FALSE;

    *StartKey = 0;
    *IdState = SAR_IDSTATE_OBSERVE_PENDING;

    FltAcquirePushLockShared(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL) {
        *StartKey = entry->start_key;
        *IdState = entry->id_state;
        found = TRUE;
    }
    FltReleasePushLock(&State->identity_lock);
    return found;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateImageByStartKey(_In_ PSAR_STATE State, _In_ UINT64 StartKey,
                                _Out_writes_(SEMANTICS_AR_PROTO_PATH_MAX) PUINT16 OutPath)
{
    ULONG b;
    BOOLEAN found = FALSE;

    RtlZeroMemory(OutPath, SEMANTICS_AR_PROTO_PATH_MAX * sizeof(UINT16));
    if (StartKey == 0)
        return FALSE;

    FltAcquirePushLockShared(&State->identity_lock);
    for (b = 0; b < State->identity_bucket_count && !found; b++) {
        PLIST_ENTRY head = &State->identity_buckets[b];
        PLIST_ENTRY it;
        for (it = head->Flink; it != head; it = it->Flink) {
            PSAR_IDENTITY_ENTRY e = CONTAINING_RECORD(it, SAR_IDENTITY_ENTRY, link);
            if (e->start_key == StartKey && e->identity_valid) {
                RtlCopyMemory(OutPath, e->identity.image_path,
                              SEMANTICS_AR_PROTO_PATH_MAX * sizeof(UINT16));
                found = TRUE;
                break;
            }
        }
    }
    FltReleasePushLock(&State->identity_lock);
    return found;
}

_IRQL_requires_max_(APC_LEVEL)
sar_id_state_t SarStateIdentityLookup(_In_ PSAR_STATE State, _In_ HANDLE ProcessId)
{
    PSAR_IDENTITY_ENTRY entry;
    sar_id_state_t result = SAR_IDSTATE_OBSERVE_PENDING;

    FltAcquirePushLockShared(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL)
        result = entry->id_state;
    FltReleasePushLock(&State->identity_lock);
    return result;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateIdentityApplyVerdict(_Inout_ PSAR_STATE State,
                                  _In_ HANDLE ProcessId,
                                  _In_ UINT64 StartKey,
                                  _In_ const sar_identity_t *VerifiedIdentity)
{
    PSAR_IDENTITY_ENTRY entry;
    BOOLEAN applied = FALSE;

    if (StartKey == 0 || !SarStateWhitelistMatch(State, VerifiedIdentity) ||
        SarIdentityIsInterpreter(VerifiedIdentity))
        return FALSE;

    FltAcquirePushLockExclusive(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL && entry->start_key == StartKey) {
        RtlCopyMemory(&entry->identity, VerifiedIdentity, sizeof(entry->identity));
        entry->identity_valid = TRUE;
        entry->id_state = SAR_IDSTATE_EXEMPT;
        applied = TRUE;
    }
    FltReleasePushLock(&State->identity_lock);

    return applied;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateProtectedTarget(_In_ PSAR_STATE State, _In_ PEPROCESS Target,
                                _Out_opt_ PUINT64 StartKey)
{
    PSAR_IDENTITY_ENTRY entry;
    BOOLEAN result = FALSE;

    if (StartKey != NULL)
        *StartKey = 0;

    FltAcquirePushLockShared(&State->identity_lock);
    entry = SarIdentityFindByProcessLocked(State, Target);
    if (entry != NULL && entry->id_state == SAR_IDSTATE_EXEMPT) {
        if (StartKey != NULL)
            *StartKey = entry->start_key;
        result = TRUE;
    }
    FltReleasePushLock(&State->identity_lock);
    return result;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateOpenerTrusted(_In_ PSAR_STATE State, _In_ PEPROCESS Opener)
{
    PSAR_IDENTITY_ENTRY entry;
    BOOLEAN trusted = FALSE;

    FltAcquirePushLockShared(&State->identity_lock);
    entry = SarIdentityFindByProcessLocked(State, Opener);
    if (entry != NULL && entry->protected_trusted)
        trusted = TRUE;
    FltReleasePushLock(&State->identity_lock);
    return trusted;
}

_IRQL_requires_max_(APC_LEVEL)
BOOLEAN SarStateRevokeExemption(_Inout_ PSAR_STATE State, _In_ HANDLE ProcessId,
                               _Out_opt_ PUINT64 StartKey)
{
    PSAR_IDENTITY_ENTRY entry;
    BOOLEAN revoked = FALSE;

    if (StartKey != NULL)
        *StartKey = 0;

    FltAcquirePushLockExclusive(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL && entry->id_state == SAR_IDSTATE_EXEMPT && !entry->protected_trusted) {
        entry->id_state = SAR_IDSTATE_OBSERVE;
        if (StartKey != NULL)
            *StartKey = entry->start_key;
        revoked = TRUE;
    }
    FltReleasePushLock(&State->identity_lock);
    return revoked;
}
