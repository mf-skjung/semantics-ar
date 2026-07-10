using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class BudgetSessionTests
{
    private static readonly DateTimeOffset Now = new(2026, 7, 10, 12, 0, 0, TimeSpan.Zero);

    private sealed class FakeChannel : IElevatedControlChannel
    {
        public IReadOnlyList<RecoverableItem> Preserved { get; init; } = Array.Empty<RecoverableItem>();
        public IReadOnlyList<AppIdentity> Identities { get; init; } = Array.Empty<AppIdentity>();
        public ElevatedError PreservedError { get; init; } = ElevatedError.None;
        public ElevatedError IdentityError { get; init; } = ElevatedError.None;
        public bool Disposed { get; private set; }

        public ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items)
        {
            items = Array.Empty<RecoverableItem>();
            return ElevatedError.None;
        }

        public ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items)
        {
            items = Preserved;
            return PreservedError;
        }

        public ElevatedError LoadAppIdentities(out IReadOnlyList<AppIdentity> items)
        {
            items = Identities;
            return IdentityError;
        }

        public ElevatedError LoadExemptions(out IReadOnlyList<Exemption> items)
        {
            items = Array.Empty<Exemption>();
            return ElevatedError.None;
        }

        public ElevatedError ResolveIdentity(string imagePath, out ResolvedIdentity? identity)
        {
            identity = null;
            return ElevatedError.None;
        }

        public ExemptionAdd WhitelistAdd(string imagePath) =>
            new(ExemptionAddResult.Added, 0, ElevatedError.None);

        public ElevatedError WhitelistRemove(string imagePath, out uint verdict)
        {
            verdict = 0;
            return ElevatedError.None;
        }

        public RecoveryOutcome Recover(RecoverableItem item, string targetPath) =>
            new(item, RecoveryOutcomeKind.RestoredVerified, 0, ElevatedError.None);

        public ElevatedError SetMode(uint mode) => ElevatedError.None;

        public ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes) => ElevatedError.None;

        public void Dispose() => Disposed = true;
    }

    private static RecoverableItem Copy(ulong appId, ulong size, DateTimeOffset when) => new()
    {
        Rung = CertaintyRung.Bounded,
        ProvenancePath = @"\Device\v\f.docx",
        Size = size,
        CaptureTime = (ulong)when.ToFileTime(),
        AppIdentityId = appId,
    };

    [Fact]
    public void Begin_FetchesBothListsInOneElevation_AndReleases()
    {
        int opens = 0;
        FakeChannel channel = new()
        {
            Preserved = [Copy(10, 100, Now.AddHours(-1))],
            Identities = [new AppIdentity { AppIdentityId = 10, ImagePath = @"C:\a\app.exe", CertSubject = "Acme" }],
        };
        BudgetSession session = new(() => { opens++; return channel; });

        session.Begin();

        Assert.Equal(1, opens);
        Assert.Equal(BudgetSessionState.Loaded, session.State);
        Assert.Single(session.Preserved);
        Assert.Single(session.Identities);
        Assert.True(channel.Disposed);
    }

    [Fact]
    public void Compute_AcrossRanges_DoesNotReElevate()
    {
        int opens = 0;
        BudgetSession session = new(() =>
        {
            opens++;
            return new FakeChannel
            {
                Preserved = [Copy(10, 100, Now.AddHours(-1)), Copy(10, 400, Now.AddDays(-3))],
                Identities = [new AppIdentity { AppIdentityId = 10, ImagePath = @"C:\a\app.exe", CertSubject = "Acme" }],
            };
        });
        session.Begin();

        BudgetAttribution day = session.Compute(BudgetRange.Last24Hours, Now);
        BudgetAttribution week = session.Compute(BudgetRange.Last7Days, Now);

        Assert.Equal(1, opens);
        Assert.Equal(100UL, day.Apps.Single(x => x.AppIdentityId == 10).Bytes);
        Assert.Equal(500UL, week.Apps.Single(x => x.AppIdentityId == 10).Bytes);
    }

    [Fact]
    public void Begin_PreservedError_IsUnavailable()
    {
        FakeChannel channel = new() { PreservedError = ElevatedError.AccessDenied };
        BudgetSession session = new(() => channel);

        session.Begin();

        Assert.Equal(BudgetSessionState.Unavailable, session.State);
        Assert.Equal(ElevatedError.AccessDenied, session.LastError);
        Assert.True(channel.Disposed);
    }

    [Fact]
    public void Begin_IdentityError_IsUnavailable()
    {
        FakeChannel channel = new() { IdentityError = ElevatedError.Transport };
        BudgetSession session = new(() => channel);

        session.Begin();

        Assert.Equal(BudgetSessionState.Unavailable, session.State);
        Assert.Equal(ElevatedError.Transport, session.LastError);
    }

    [Fact]
    public void Begin_ConsentDeclined_ReturnsToIdle()
    {
        BudgetSession session = new(() => throw new OperationCanceledException());

        session.Begin();

        Assert.Equal(BudgetSessionState.Idle, session.State);
        Assert.Equal(ElevatedError.None, session.LastError);
    }

    [Fact]
    public void Close_Resets()
    {
        BudgetSession session = new(() => new FakeChannel { Preserved = [Copy(10, 100, Now)] });
        session.Begin();

        session.Close();

        Assert.Equal(BudgetSessionState.Idle, session.State);
        Assert.Empty(session.Preserved);
    }
}
