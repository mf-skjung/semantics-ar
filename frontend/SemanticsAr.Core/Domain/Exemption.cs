namespace SemanticsAr.Core.Domain;

public enum ExemptionMatchState
{
    Matching,
    LapsedSameSigner,
    LapsedChangedSigner,
}

public sealed record Exemption
{
    public required string ImagePath { get; init; }
    public required string CertSubject { get; init; }
    public required byte[] ContentHash { get; init; }
    public DateTimeOffset? FirstSeen { get; init; }
    public required ExemptionMatchState MatchState { get; init; }

    public static ExemptionMatchState MatchStateOf(uint value) => value switch
    {
        0 => ExemptionMatchState.Matching,
        1 => ExemptionMatchState.LapsedSameSigner,
        _ => ExemptionMatchState.LapsedChangedSigner,
    };
}

public sealed record ResolvedIdentity
{
    public required string ImagePath { get; init; }
    public required string CertSubject { get; init; }
    public required byte[] ContentHash { get; init; }
    public required uint Verdict { get; init; }
}

public enum ExemptionAddResult
{
    Added,
    RefusedInterpreter,
    NotVerified,
    ChannelError,
}

public readonly record struct ExemptionAdd(ExemptionAddResult Result, uint Verdict, ElevatedError Error);
