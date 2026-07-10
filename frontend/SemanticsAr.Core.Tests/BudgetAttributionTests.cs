using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class BudgetAttributionTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 10, 12, 0, 0, TimeSpan.Zero);

    private static RecoverableItem Copy(ulong appId, ulong size, DateTimeOffset when, string path = @"\Device\v\f.docx") => new()
    {
        Rung = CertaintyRung.Bounded,
        ProvenancePath = path,
        Size = size,
        CaptureTime = (ulong)when.ToFileTime(),
        AppIdentityId = appId,
    };

    private static AppIdentity Ident(ulong id, string image, string subject = "Acme Co", uint verdict = 0) => new()
    {
        AppIdentityId = id,
        ImagePath = image,
        CertSubject = subject,
        Verdict = verdict,
    };

    [Fact]
    public void Build_Empty_HasNoAppsNoWindow()
    {
        BudgetAttribution a = BudgetAttribution.Build([], [], BudgetRange.AllTime, Now);

        Assert.Empty(a.Apps);
        Assert.Equal(0, a.TotalCopyCount);
        Assert.Null(a.AchievedWindow);
        Assert.Equal(BudgetAttribution.TrendDays, a.Trend.Count);
    }

    [Fact]
    public void Build_UnattributedOnly_ProducesSingleDrainingBucket()
    {
        BudgetAttribution a = BudgetAttribution.Build(
            [Copy(0, 1000, Now.AddDays(-9)), Copy(0, 500, Now.AddDays(-5))],
            [],
            BudgetRange.AllTime,
            Now);

        AppImpact row = Assert.Single(a.Apps);
        Assert.Equal(AppImpactKind.Unattributed, row.Kind);
        Assert.Equal(2, row.CopyCount);
        Assert.Equal(1500UL, row.Bytes);
        Assert.Equal(1.0, row.WindowShare, 3);
        Assert.Empty(row.Classes);
    }

    [Fact]
    public void Build_JoinsIdentityAndRanksByBytes()
    {
        BudgetAttribution a = BudgetAttribution.Build(
            [
                Copy(0, 1000, Now.AddDays(-8)),
                Copy(10, 300, Now.AddHours(-2)),
                Copy(20, 700, Now.AddHours(-3)),
            ],
            [
                Ident(10, @"C:\Program Files\Google\Chrome\Application\chrome.exe", "Google LLC"),
                Ident(20, @"C:\Program Files\Adobe\Photoshop.exe", "Adobe Inc.", verdict: 1),
            ],
            BudgetRange.AllTime,
            Now);

        Assert.Equal(3, a.Apps.Count);
        Assert.Equal(AppImpactKind.Unattributed, a.Apps[0].Kind);

        AppImpact photoshop = a.Apps[1];
        Assert.Equal("Photoshop", photoshop.DisplayName);
        Assert.Equal("Adobe Inc.", photoshop.Signer);
        Assert.Equal(AppSignature.Unsigned, photoshop.Signature);
        Assert.Equal(700UL, photoshop.Bytes);

        AppImpact chrome = a.Apps[2];
        Assert.Equal("chrome", chrome.DisplayName);
        Assert.Equal(AppSignature.Verified, chrome.Signature);

        double total = a.Apps.Sum(x => x.WindowShare);
        Assert.Equal(1.0, total, 3);
    }

    [Fact]
    public void Build_RangeScopesAttributedCopiesButNotBacklog()
    {
        RecoverableItem[] preserved =
        [
            Copy(0, 1000, Now.AddDays(-9)),
            Copy(10, 100, Now.AddHours(-12)),
            Copy(10, 400, Now.AddDays(-3)),
        ];
        AppIdentity[] ids = [Ident(10, @"C:\a\app.exe")];

        AppImpact In(BudgetRange r) => BudgetAttribution.Build(preserved, ids, r, Now).Apps.Single(x => x.AppIdentityId == 10);

        Assert.Equal(100UL, In(BudgetRange.Last24Hours).Bytes);
        Assert.Equal(500UL, In(BudgetRange.Last7Days).Bytes);
        Assert.Equal(500UL, In(BudgetRange.AllTime).Bytes);
    }

    [Fact]
    public void Build_CopiesExistButNoneInRange_ProducesNoRowsButNonZeroTotal()
    {
        RecoverableItem[] preserved = [Copy(10, 500, Now.AddDays(-3))];
        BudgetAttribution a = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.Last24Hours, Now);

        Assert.Empty(a.Apps);
        Assert.Equal(1, a.TotalCopyCount);
    }

    [Fact]
    public void Build_SmallAppsFoldIntoEverythingElse()
    {
        List<RecoverableItem> preserved = [Copy(1, 10000, Now.AddHours(-1))];
        List<AppIdentity> ids = [Ident(1, @"C:\a\big.exe")];
        for (ulong id = 2; id <= 6; id++)
        {
            preserved.Add(Copy(id, 10, Now.AddHours(-1)));
            ids.Add(Ident(id, $@"C:\a\small{id}.exe"));
        }

        BudgetAttribution a = BudgetAttribution.Build(preserved, ids, BudgetRange.AllTime, Now);

        Assert.Equal(2, a.Apps.Count);
        AppImpact grouped = a.Apps.Single(x => x.Kind == AppImpactKind.Grouped);
        Assert.Equal("Everything else", grouped.DisplayName);
        Assert.Equal(5, grouped.GroupedAppCount);
        Assert.Equal(50UL, grouped.Bytes);
        Assert.Equal(AppImpactKind.Grouped, a.Apps[^1].Kind);
    }

    [Fact]
    public void Build_SingleSubThresholdApp_GroupsAsOne()
    {
        RecoverableItem[] preserved =
        [
            Copy(0, 100_000, Now.AddDays(-9)),
            Copy(9, 50, Now.AddHours(-1)),
        ];
        BudgetAttribution a = BudgetAttribution.Build(preserved, [Ident(9, @"C:\a\small.exe")], BudgetRange.AllTime, Now);

        AppImpact grouped = a.Apps.Single(x => x.Kind == AppImpactKind.Grouped);
        Assert.Equal(1, grouped.GroupedAppCount);
    }

    [Fact]
    public void Build_Trend_BucketsByUtcDayNotNowOffset()
    {
        DateTimeOffset now = new(2026, 7, 10, 12, 0, 0, TimeSpan.FromHours(-5));
        RecoverableItem[] preserved = [Copy(10, 500, new DateTimeOffset(2026, 7, 10, 3, 0, 0, TimeSpan.Zero))];

        BudgetAttribution a = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.AllTime, now);

        Assert.Equal(500UL, a.Trend[^1].Bytes);
        Assert.Equal(new DateTimeOffset(2026, 7, 10, 0, 0, 0, TimeSpan.Zero), a.Trend[^1].Day);
    }

    [Fact]
    public void Build_Delta_MeasuredWhenPriorPeriodVisible()
    {
        RecoverableItem[] preserved =
        [
            Copy(0, 1000, Now.AddDays(-10)),
            Copy(10, 300, Now.AddHours(-12)),
            Copy(10, 100, Now.AddHours(-36)),
        ];
        AppImpact app = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.Last24Hours, Now)
            .Apps.Single(x => x.AppIdentityId == 10);

        Assert.Equal(200L, app.DeltaBytes);
    }

    [Fact]
    public void Build_Delta_SuppressedWhenPriorPeriodNotVisible()
    {
        RecoverableItem[] preserved = [Copy(10, 300, Now.AddHours(-12))];
        AppImpact app = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.Last24Hours, Now)
            .Apps.Single(x => x.AppIdentityId == 10);

        Assert.Null(app.DeltaBytes);
    }

    [Fact]
    public void Build_Delta_NullForAllTime()
    {
        RecoverableItem[] preserved =
        [
            Copy(0, 1000, Now.AddDays(-10)),
            Copy(10, 300, Now.AddHours(-12)),
        ];
        AppImpact app = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.AllTime, Now)
            .Apps.Single(x => x.AppIdentityId == 10);

        Assert.Null(app.DeltaBytes);
    }

    [Fact]
    public void Build_ClassifiesDrilldownByFileClassSortedByBytes()
    {
        RecoverableItem[] preserved =
        [
            Copy(10, 100, Now.AddHours(-1), @"\Device\v\a.docx"),
            Copy(10, 300, Now.AddHours(-1), @"\Device\v\b.jpg"),
        ];
        AppImpact app = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.AllTime, Now)
            .Apps.Single(x => x.AppIdentityId == 10);

        Assert.Equal(2, app.Classes.Count);
        Assert.Equal(FileClass.Image, app.Classes[0].Class);
        Assert.Equal(300UL, app.Classes[0].Bytes);
        Assert.Equal(FileClass.Document, app.Classes[1].Class);
    }

    [Fact]
    public void Build_TrendBucketsAttributedCopiesByDayExcludingBacklog()
    {
        RecoverableItem[] preserved =
        [
            Copy(0, 9999, Now.AddDays(-1)),
            Copy(10, 500, Now.AddDays(-1)),
            Copy(10, 700, Now),
        ];
        BudgetAttribution a = BudgetAttribution.Build(preserved, [Ident(10, @"C:\a\app.exe")], BudgetRange.AllTime, Now);

        Assert.Equal(BudgetAttribution.TrendDays, a.Trend.Count);
        Assert.Equal(700UL, a.Trend[^1].Bytes);
        Assert.Equal(500UL, a.Trend[^2].Bytes);
    }

    [Fact]
    public void Build_AchievedWindowMeasuredFromOldestStandingCopy()
    {
        BudgetAttribution a = BudgetAttribution.Build([Copy(10, 64, Now.AddDays(-6))], [Ident(10, @"C:\a\app.exe")], BudgetRange.AllTime, Now);
        Assert.Equal(TimeSpan.FromDays(6), a.AchievedWindow);
    }
}
