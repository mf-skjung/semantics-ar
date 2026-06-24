#include "state.h"
#include "driver.h"

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

    buckets = (LIST_ENTRY *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                            sizeof(LIST_ENTRY) * SAR_IDENTITY_BUCKET_COUNT,
                                            SAR_POOL_TAG_IDENTITY);
    if (buckets == NULL) {
        ExFreePoolWithTag(storage, SAR_POOL_TAG_WHITELIST);
        ExFreePoolWithTag(state, SAR_POOL_TAG_STATE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    state->mode = SEMANTICS_AR_MODE_AUDIT;
    state->captured_key_count = 0;
    FltInitializePushLock(&state->whitelist_lock);
    FltInitializePushLock(&state->identity_lock);
    state->whitelist_storage = storage;
    sar_whitelist_init(&state->whitelist, storage, SAR_WHITELIST_CAPACITY);
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
    return TRUE;
}

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistAdd(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity)
{
    sar_wl_status_t status;

    FltAcquirePushLockExclusive(&State->whitelist_lock);
    status = sar_whitelist_add(&State->whitelist, Identity);
    FltReleasePushLock(&State->whitelist_lock);
    return status;
}

_IRQL_requires_max_(APC_LEVEL)
sar_wl_status_t SarStateWhitelistRemove(_Inout_ PSAR_STATE State, _In_ const sar_identity_t *Identity)
{
    sar_wl_status_t status;

    FltAcquirePushLockExclusive(&State->whitelist_lock);
    status = sar_whitelist_remove(&State->whitelist, Identity);
    FltReleasePushLock(&State->whitelist_lock);
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

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS SarStateIdentityInsert(_Inout_ PSAR_STATE State,
                                _In_ HANDLE ProcessId,
                                _In_ PEPROCESS Process,
                                _In_opt_ const sar_identity_t *Identity,
                                _In_ BOOLEAN IdentityValid,
                                _In_ BOOLEAN SubsystemProcess)
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
    entry->identity_valid = IdentityValid;
    entry->subsystem_process = SubsystemProcess;
    RtlZeroMemory(&entry->identity, sizeof(entry->identity));
    if (Identity != NULL)
        RtlCopyMemory(&entry->identity, Identity, sizeof(entry->identity));

    if (IdentityValid && Identity != NULL)
        whitelist_hit = SarStateWhitelistMatch(State, Identity) ? 1 : 0;
    entry->id_state = sar_identity_resolve(IdentityValid ? 1 : 0, whitelist_hit);

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
VOID SarStateIdentityApplyVerdict(_Inout_ PSAR_STATE State,
                                  _In_ HANDLE ProcessId,
                                  _In_ const sar_identity_t *VerifiedIdentity)
{
    PSAR_IDENTITY_ENTRY entry;
    int whitelist_hit;

    whitelist_hit = SarStateWhitelistMatch(State, VerifiedIdentity) ? 1 : 0;

    FltAcquirePushLockExclusive(&State->identity_lock);
    entry = SarIdentityFindLocked(State, ProcessId);
    if (entry != NULL) {
        RtlCopyMemory(&entry->identity, VerifiedIdentity, sizeof(entry->identity));
        entry->identity_valid = TRUE;
        entry->id_state = sar_identity_resolve(1, whitelist_hit);
    }
    FltReleasePushLock(&State->identity_lock);
}
