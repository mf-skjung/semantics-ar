namespace SemanticsAr.Core.Domain;

public enum RecoveryOutcomeKind
{
    RestoredVerified,
    DeclinedLeftIntact,
    ChannelError,
}

public sealed record RecoveryOutcome(
    RecoverableItem Item,
    RecoveryOutcomeKind Kind,
    int KernelResult,
    ElevatedError Error);
