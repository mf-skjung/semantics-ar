namespace SemanticsAr.Core.Domain;

public readonly record struct JournalEvent(
    JournalEventClass EventClass,
    ulong Generation,
    ulong Sequence,
    DateTimeOffset Timestamp,
    ulong ActorStartKey,
    bool Gap) : IIncidentSource;
