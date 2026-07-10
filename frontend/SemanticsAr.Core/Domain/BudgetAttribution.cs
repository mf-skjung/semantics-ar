namespace SemanticsAr.Core.Domain;

public enum BudgetRange
{
    Last24Hours,
    Last7Days,
    AllTime,
}

public readonly record struct TrendPoint(DateTimeOffset Day, ulong Bytes);

public sealed record BudgetAttribution
{
    public const double GroupingThreshold = 0.01;
    public const int TrendDays = 14;

    private static readonly int FileClassCount = Enum.GetValues<FileClass>().Length;

    public required BudgetRange Range { get; init; }
    public required int TotalCopyCount { get; init; }
    public required ulong TotalBytes { get; init; }
    public DateTimeOffset? OldestCopy { get; init; }
    public TimeSpan? AchievedWindow { get; init; }
    public required IReadOnlyList<AppImpact> Apps { get; init; }
    public required IReadOnlyList<TrendPoint> Trend { get; init; }

    public static BudgetAttribution Build(
        IReadOnlyList<RecoverableItem> preserved,
        IReadOnlyList<AppIdentity> identities,
        BudgetRange range,
        DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(preserved);
        ArgumentNullException.ThrowIfNull(identities);

        BudgetSnapshot overall = BudgetSnapshot.FromPreserve(preserved);

        Dictionary<ulong, AppIdentity> identityMap = new(identities.Count);
        foreach (AppIdentity identity in identities)
            identityMap[identity.AppIdentityId] = identity;

        DateTimeOffset? lowerBound = LowerBound(range, now);

        ulong unattributedBytes = 0;
        int unattributedCount = 0;
        Dictionary<ulong, List<RecoverableItem>> byApp = new();

        foreach (RecoverableItem item in preserved)
        {
            if (item.AppIdentityId == 0)
            {
                unattributedBytes += item.Size;
                unattributedCount++;
                continue;
            }

            if (!byApp.TryGetValue(item.AppIdentityId, out List<RecoverableItem>? bucket))
            {
                bucket = new List<RecoverableItem>();
                byApp[item.AppIdentityId] = bucket;
            }
            bucket.Add(item);
        }

        List<(ulong Id, List<RecoverableItem> Items, List<RecoverableItem> Current, ulong Bytes)> active = new();
        ulong attributedBytes = 0;
        foreach ((ulong id, List<RecoverableItem> items) in byApp)
        {
            List<RecoverableItem> current;
            ulong bytes = 0;
            if (lowerBound is null)
            {
                current = items;
                foreach (RecoverableItem item in items)
                    bytes += item.Size;
            }
            else
            {
                current = new List<RecoverableItem>();
                foreach (RecoverableItem item in items)
                {
                    if (!InRange(item, lowerBound, now))
                        continue;
                    current.Add(item);
                    bytes += item.Size;
                }
                if (current.Count == 0)
                    continue;
            }
            active.Add((id, items, current, bytes));
            attributedBytes += bytes;
        }

        ulong basis = unattributedBytes + attributedBytes;

        List<AppImpact> rows = new();
        if (unattributedCount > 0)
        {
            rows.Add(new AppImpact
            {
                Kind = AppImpactKind.Unattributed,
                AppIdentityId = 0,
                DisplayName = "Older activity — before per-app attribution",
                Signer = string.Empty,
                Signature = AppSignature.Unverifiable,
                CopyCount = unattributedCount,
                Bytes = unattributedBytes,
                WindowShare = Share(unattributedBytes, basis),
            });
        }

        ulong groupedBytes = 0;
        int groupedCopies = 0;
        int groupedApps = 0;
        foreach ((ulong id, List<RecoverableItem> items, List<RecoverableItem> current, ulong bytes) in active)
        {
            double share = Share(bytes, basis);
            if (share < GroupingThreshold)
            {
                groupedBytes += bytes;
                groupedCopies += current.Count;
                groupedApps++;
                continue;
            }

            identityMap.TryGetValue(id, out AppIdentity? identity);
            rows.Add(new AppImpact
            {
                Kind = AppImpactKind.Attributed,
                AppIdentityId = id,
                DisplayName = DisplayName(identity),
                ImagePath = identity?.ImagePath ?? string.Empty,
                Signer = identity?.CertSubject ?? string.Empty,
                Signature = AppImpact.SignatureOf(identity?.Verdict ?? 4),
                CopyCount = current.Count,
                Bytes = bytes,
                WindowShare = share,
                DeltaBytes = Delta(items, range, overall.OldestCopy, now, bytes),
                Classes = Classify(current),
            });
        }

        rows.Sort((a, b) => b.Bytes.CompareTo(a.Bytes));

        if (groupedApps > 0)
        {
            rows.Add(new AppImpact
            {
                Kind = AppImpactKind.Grouped,
                AppIdentityId = 0,
                DisplayName = "Everything else",
                Signer = string.Empty,
                Signature = AppSignature.Unverifiable,
                CopyCount = groupedCopies,
                Bytes = groupedBytes,
                WindowShare = Share(groupedBytes, basis),
                GroupedAppCount = groupedApps,
            });
        }

        return new BudgetAttribution
        {
            Range = range,
            TotalCopyCount = overall.CopyCount,
            TotalBytes = overall.CopyBytes,
            OldestCopy = overall.OldestCopy,
            AchievedWindow = overall.AchievedWindow(now),
            Apps = rows,
            Trend = BuildTrend(preserved, now),
        };
    }

    private static DateTimeOffset? LowerBound(BudgetRange range, DateTimeOffset now) => range switch
    {
        BudgetRange.Last24Hours => now - TimeSpan.FromDays(1),
        BudgetRange.Last7Days => now - TimeSpan.FromDays(7),
        _ => null,
    };

    private static bool InRange(RecoverableItem item, DateTimeOffset? lowerBound, DateTimeOffset now)
    {
        if (lowerBound is null)
            return true;
        if (RestorePlanner.Anchor(item.CaptureTime) is not { } when)
            return false;
        return when >= lowerBound && when <= now;
    }

    private static double Share(ulong bytes, ulong basis) =>
        basis == 0 ? 0.0 : (double)bytes / basis;

    private static long? Delta(
        List<RecoverableItem> appItems,
        BudgetRange range,
        DateTimeOffset? oldest,
        DateTimeOffset now,
        ulong currentBytes)
    {
        TimeSpan span = range switch
        {
            BudgetRange.Last24Hours => TimeSpan.FromDays(1),
            BudgetRange.Last7Days => TimeSpan.FromDays(7),
            _ => TimeSpan.Zero,
        };
        if (span == TimeSpan.Zero)
            return null;

        DateTimeOffset priorHigh = now - span;
        DateTimeOffset priorLow = priorHigh - span;
        if (oldest is null || oldest > priorLow)
            return null;

        ulong priorBytes = 0;
        foreach (RecoverableItem item in appItems)
        {
            if (RestorePlanner.Anchor(item.CaptureTime) is not { } when)
                continue;
            if (when >= priorLow && when < priorHigh)
                priorBytes += item.Size;
        }

        return (long)currentBytes - (long)priorBytes;
    }

    private static IReadOnlyList<FileClassBucket> Classify(List<RecoverableItem> items)
    {
        Span<int> counts = stackalloc int[FileClassCount];
        Span<ulong> sizes = stackalloc ulong[FileClassCount];
        foreach (RecoverableItem item in items)
        {
            int i = (int)FileClassifier.Classify(item.ProvenancePath);
            counts[i]++;
            sizes[i] += item.Size;
        }

        List<FileClassBucket> buckets = new();
        for (int i = 0; i < FileClassCount; i++)
        {
            if (counts[i] == 0)
                continue;
            buckets.Add(new FileClassBucket { Class = (FileClass)i, CopyCount = counts[i], Bytes = sizes[i] });
        }

        buckets.Sort((a, b) => b.Bytes.CompareTo(a.Bytes));
        return buckets;
    }

    private static IReadOnlyList<TrendPoint> BuildTrend(IReadOnlyList<RecoverableItem> preserved, DateTimeOffset now)
    {
        ulong[] daily = new ulong[TrendDays];
        DateTime today = now.UtcDateTime.Date;

        foreach (RecoverableItem item in preserved)
        {
            if (item.AppIdentityId == 0)
                continue;
            if (RestorePlanner.Anchor(item.CaptureTime) is not { } when)
                continue;
            int index = TrendDays - 1 - (int)(today - when.UtcDateTime.Date).TotalDays;
            if (index >= 0 && index < TrendDays)
                daily[index] += item.Size;
        }

        List<TrendPoint> points = new(TrendDays);
        for (int i = 0; i < TrendDays; i++)
            points.Add(new TrendPoint(new DateTimeOffset(today.AddDays(i - (TrendDays - 1)), TimeSpan.Zero), daily[i]));
        return points;
    }

    private static string DisplayName(AppIdentity? identity)
    {
        if (identity is null || identity.ImagePath.Length == 0)
            return "Unknown application";

        ReadOnlySpan<char> span = identity.ImagePath.AsSpan();
        int slash = span.LastIndexOfAny('\\', '/');
        ReadOnlySpan<char> leaf = slash >= 0 ? span[(slash + 1)..] : span;
        int dot = leaf.LastIndexOf('.');
        if (dot > 0)
            leaf = leaf[..dot];
        return leaf.Length == 0 ? "Unknown application" : leaf.ToString();
    }
}
