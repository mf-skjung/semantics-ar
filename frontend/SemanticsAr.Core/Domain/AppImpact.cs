namespace SemanticsAr.Core.Domain;

public enum AppImpactKind
{
    Attributed,
    Unattributed,
    Grouped,
}

public enum AppSignature
{
    Verified,
    Unsigned,
    Unverifiable,
}

public sealed record FileClassBucket
{
    public required FileClass Class { get; init; }
    public required int CopyCount { get; init; }
    public required ulong Bytes { get; init; }
}

public sealed record AppImpact
{
    public required AppImpactKind Kind { get; init; }
    public required ulong AppIdentityId { get; init; }
    public required string DisplayName { get; init; }
    public string ImagePath { get; init; } = string.Empty;
    public required string Signer { get; init; }
    public required AppSignature Signature { get; init; }
    public required int CopyCount { get; init; }
    public required ulong Bytes { get; init; }
    public required double WindowShare { get; init; }
    public long? DeltaBytes { get; init; }
    public int GroupedAppCount { get; init; }
    public IReadOnlyList<FileClassBucket> Classes { get; init; } = Array.Empty<FileClassBucket>();

    public TimeSpan? TimeImpact(TimeSpan? achievedWindow) =>
        achievedWindow is { } window ? window * WindowShare : null;

    public static AppSignature SignatureOf(uint verdict) => verdict switch
    {
        0 => AppSignature.Verified,
        1 => AppSignature.Unsigned,
        _ => AppSignature.Unverifiable,
    };
}
