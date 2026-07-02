#include "eventlog.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarEventLogCreate(_Outptr_ PSAR_EVENTLOG *EventLog)
{
    PSAR_EVENTLOG log;
    LARGE_INTEGER now;

    *EventLog = NULL;

    log = (PSAR_EVENTLOG)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(SAR_EVENTLOG),
                                         SAR_POOL_TAG_EVENTLOG);
    if (log == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(log, sizeof(*log));

    KeQuerySystemTime(&now);
    log->generation = (UINT64)now.QuadPart;
    if (log->generation == 0)
        log->generation = 1;
    log->next_sequence = 1;

    *EventLog = log;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarEventLogDestroy(_Inout_ PSAR_EVENTLOG EventLog)
{
    if (EventLog == NULL)
        return;
    ExFreePoolWithTag(EventLog, SAR_POOL_TAG_EVENTLOG);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarEventLogRecord(_Inout_opt_ PSAR_EVENTLOG EventLog, _In_ UINT32 EventClass,
                       _In_ UINT64 ActorStartKey)
{
    KIRQL oldIrql;
    LARGE_INTEGER now;
    PSAR_EVENT_RECORD slot;

    if (EventLog == NULL)
        return;

    KeQuerySystemTime(&now);

    oldIrql = ExAcquireSpinLockExclusive(&EventLog->lock);

    slot = &EventLog->records[EventLog->head];
    slot->generation = EventLog->generation;
    slot->sequence = EventLog->next_sequence;
    slot->timestamp_100ns = (UINT64)now.QuadPart;
    slot->actor_start_key = ActorStartKey;
    slot->event_class = EventClass;

    EventLog->next_sequence++;
    EventLog->head = (EventLog->head + 1) % SAR_EVENTLOG_CAPACITY;
    if (EventLog->count < SAR_EVENTLOG_CAPACITY)
        EventLog->count++;

    ExReleaseSpinLockExclusive(&EventLog->lock, oldIrql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarEventLogQuery(_In_opt_ PSAR_EVENTLOG EventLog, _In_ UINT64 QueryGeneration,
                      _In_ UINT64 QuerySequence, _Out_ PSAR_EVENT_RECORD Out,
                      _Out_ PBOOLEAN Valid, _Out_ PBOOLEAN Gap)
{
    KIRQL oldIrql;
    UINT64 oldestSequence;
    UINT64 newestSequence;
    ULONG  tail;

    *Valid = FALSE;
    *Gap = FALSE;
    RtlZeroMemory(Out, sizeof(*Out));

    if (EventLog == NULL)
        return;

    oldIrql = ExAcquireSpinLockShared(&EventLog->lock);

    if (EventLog->count == 0) {
        ExReleaseSpinLockShared(&EventLog->lock, oldIrql);
        return;
    }

    tail = (EventLog->head + SAR_EVENTLOG_CAPACITY - EventLog->count) % SAR_EVENTLOG_CAPACITY;
    oldestSequence = EventLog->records[tail].sequence;
    newestSequence = EventLog->next_sequence - 1;

    if (QueryGeneration == EventLog->generation &&
        !(QueryGeneration == 0 && QuerySequence == 0)) {
        UINT64 target = QuerySequence + 1;

        if (target > newestSequence) {
            ExReleaseSpinLockShared(&EventLog->lock, oldIrql);
            return;
        }

        if (target < oldestSequence) {
            *Out = EventLog->records[tail];
            *Gap = TRUE;
        } else {
            ULONG idx = (ULONG)((tail + (target - oldestSequence)) % SAR_EVENTLOG_CAPACITY);
            *Out = EventLog->records[idx];
        }
        *Valid = TRUE;
    } else {
        *Out = EventLog->records[tail];
        *Gap = (QueryGeneration != 0 || QuerySequence != 0);
        *Valid = TRUE;
    }

    ExReleaseSpinLockShared(&EventLog->lock, oldIrql);
}
