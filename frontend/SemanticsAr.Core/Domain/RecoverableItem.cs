namespace SemanticsAr.Core.Domain;

public sealed record RecoverableItem
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
}
