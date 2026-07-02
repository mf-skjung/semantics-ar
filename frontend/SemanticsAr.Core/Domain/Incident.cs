namespace SemanticsAr.Core.Domain;

public sealed record Incident(
    ulong ActorStartKey,
    DateTimeOffset FirstSeen,
    DateTimeOffset LastSeen,
    IReadOnlyList<IIncidentSource> Members);
