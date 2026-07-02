namespace SemanticsAr.Core.Domain;

public interface IIncidentSource
{
    ulong ActorStartKey { get; }

    DateTimeOffset Timestamp { get; }
}
