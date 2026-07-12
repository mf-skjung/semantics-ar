namespace SemanticsAr.Core.Domain;

public readonly record struct PostureVerdict(
    PostureLevel Level,
    PostureReason Reason,
    PostureMode Mode,
    bool ManagementAvailable,
    ulong CapturedKeyCount,
    bool IsStale,
    PostureDescent Descents,
    PreserveHealth PreserveHealth,
    PreserveExpiry OldestProtectedExpiry,
    bool IntegrityHalt = false);
