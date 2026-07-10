namespace SemanticsAr.Core.Domain;

public sealed record BudgetSnapshot
{
    public required int CopyCount { get; init; }

    public required ulong CopyBytes { get; init; }

    public DateTimeOffset? OldestCopy { get; init; }

    public DateTimeOffset? NewestCopy { get; init; }

    public static BudgetSnapshot FromPreserve(IReadOnlyList<RecoverableItem> preserved)
    {
        ArgumentNullException.ThrowIfNull(preserved);

        ulong bytes = 0;
        DateTimeOffset? oldest = null;
        DateTimeOffset? newest = null;

        foreach (RecoverableItem item in preserved)
        {
            bytes += item.Size;
            if (RestorePlanner.Anchor(item.CaptureTime) is not { } when)
                continue;
            if (oldest is null || when < oldest)
                oldest = when;
            if (newest is null || when > newest)
                newest = when;
        }

        return new BudgetSnapshot
        {
            CopyCount = preserved.Count,
            CopyBytes = bytes,
            OldestCopy = oldest,
            NewestCopy = newest,
        };
    }

    public TimeSpan? AchievedWindow(DateTimeOffset now) =>
        OldestCopy is { } oldest ? now - oldest : null;
}
