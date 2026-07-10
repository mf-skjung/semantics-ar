namespace SemanticsAr.Core.Domain;

public sealed record RecoverableItem : IIncidentSource
{
    public required CertaintyRung Rung { get; init; }
    public required string ProvenancePath { get; init; }
    public byte[]? KeyId { get; init; }
    public uint Algorithm { get; init; }
    public uint Mode { get; init; }
    public ulong Offset { get; init; }
    public ulong Length { get; init; }
    public ulong Size { get; init; }
    public ulong CaptureTime { get; init; }
    public ulong ActorStartKey { get; init; }
    public PreservePool Pool { get; init; }
    public ulong AppIdentityId { get; init; }

    public DateTimeOffset Timestamp => RestorePlanner.Anchor(CaptureTime) ?? DateTimeOffset.MinValue;
}
