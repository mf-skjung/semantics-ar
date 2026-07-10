using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class BudgetSnapshotTests
{
    private static readonly DateTimeOffset Newest = new(2026, 7, 8, 0, 0, 0, TimeSpan.Zero);
    private static readonly DateTimeOffset Oldest = new(2026, 7, 1, 0, 0, 0, TimeSpan.Zero);

    private static RecoverableItem Copy(DateTimeOffset when, ulong size) => new()
    {
        Rung = CertaintyRung.Bounded,
        ProvenancePath = @"\Device\v\a.dat",
        CaptureTime = (ulong)when.ToFileTime(),
        Size = size,
    };

    [Fact]
    public void FromPreserve_Empty_HasNoWindowAndZeroBytes()
    {
        BudgetSnapshot snapshot = BudgetSnapshot.FromPreserve(Array.Empty<RecoverableItem>());
        Assert.Equal(0, snapshot.CopyCount);
        Assert.Equal(0UL, snapshot.CopyBytes);
        Assert.Null(snapshot.OldestCopy);
        Assert.Null(snapshot.AchievedWindow(Newest));
    }

    [Fact]
    public void FromPreserve_SumsBytesAndBracketsCaptureTimes()
    {
        BudgetSnapshot snapshot = BudgetSnapshot.FromPreserve(
        [
            Copy(Newest, 1024),
            Copy(Oldest, 2048),
            Copy(Oldest.AddDays(3), 512),
        ]);

        Assert.Equal(3, snapshot.CopyCount);
        Assert.Equal(3584UL, snapshot.CopyBytes);
        Assert.Equal(Oldest, snapshot.OldestCopy);
        Assert.Equal(Newest, snapshot.NewestCopy);
    }

    [Fact]
    public void AchievedWindow_MeasuredFromOldestCopyToNow()
    {
        BudgetSnapshot snapshot = BudgetSnapshot.FromPreserve([Copy(Oldest, 64)]);
        Assert.Equal(TimeSpan.FromDays(7), snapshot.AchievedWindow(Oldest.AddDays(7)));
    }

    [Fact]
    public void FromPreserve_GarbageCaptureTime_IgnoredForWindowButCountedForBytes()
    {
        BudgetSnapshot snapshot = BudgetSnapshot.FromPreserve(
        [
            new RecoverableItem { Rung = CertaintyRung.Bounded, ProvenancePath = @"\Device\v\b.dat", CaptureTime = 0, Size = 100 },
            Copy(Oldest, 200),
        ]);

        Assert.Equal(2, snapshot.CopyCount);
        Assert.Equal(300UL, snapshot.CopyBytes);
        Assert.Equal(Oldest, snapshot.OldestCopy);
    }
}
