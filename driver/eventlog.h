#ifndef SEMANTICS_AR_DRIVER_EVENTLOG_H
#define SEMANTICS_AR_DRIVER_EVENTLOG_H

#include <fltKernel.h>

#include "driver.h"

#define SAR_EVENTLOG_CAPACITY 256u

typedef struct _SAR_EVENT_RECORD {
    UINT64 generation;
    UINT64 sequence;
    UINT64 timestamp_100ns;
    UINT64 actor_start_key;
    UINT32 event_class;
} SAR_EVENT_RECORD, *PSAR_EVENT_RECORD;

typedef struct _SAR_EVENTLOG {
    EX_SPIN_LOCK lock;
    UINT64       generation;
    UINT64       next_sequence;
    ULONG        head;
    ULONG        count;
    SAR_EVENT_RECORD records[SAR_EVENTLOG_CAPACITY];
} SAR_EVENTLOG, *PSAR_EVENTLOG;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS SarEventLogCreate(_Outptr_ PSAR_EVENTLOG *EventLog);

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID SarEventLogDestroy(_Inout_ PSAR_EVENTLOG EventLog);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarEventLogRecord(_Inout_opt_ PSAR_EVENTLOG EventLog, _In_ UINT32 EventClass,
                       _In_ UINT64 ActorStartKey);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID SarEventLogQuery(_In_opt_ PSAR_EVENTLOG EventLog, _In_ UINT64 QueryGeneration,
                      _In_ UINT64 QuerySequence, _Out_ PSAR_EVENT_RECORD Out,
                      _Out_ PBOOLEAN Valid, _Out_ PBOOLEAN Gap);

#endif
