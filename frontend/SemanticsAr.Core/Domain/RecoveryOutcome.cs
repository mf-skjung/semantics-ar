namespace SemanticsAr.Core.Domain;

public enum RecoveryOutcomeKind
{
    RestoredVerified,
    DeclinedLeftIntact,
    ChannelError,
}

public enum RecoveryDeclineReason
{
    None,
    DefinitiveFolderOnly,
    PathBlocked,
    PathUnavailable,
}

public sealed record RecoveryOutcome(
    RecoverableItem Item,
    RecoveryOutcomeKind Kind,
    int KernelResult,
    ElevatedError Error)
{
    public string TargetPath { get; init; } = string.Empty;

    public RecoveryDeclineReason DeclineReason { get; init; } = RecoveryDeclineReason.None;
}
