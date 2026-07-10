using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class ExemptionSessionTests
{
    private sealed class FakeChannel : IElevatedControlChannel
    {
        public IReadOnlyList<Exemption> Exemptions { get; init; } = Array.Empty<Exemption>();
        public ElevatedError ExemptionsError { get; init; } = ElevatedError.None;
        public ExemptionAdd AddResult { get; init; } = new(ExemptionAddResult.Added, 0, ElevatedError.None);
        public ResolvedIdentity? Resolved { get; init; }
        public bool Disposed { get; private set; }

        public ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items) { items = Array.Empty<RecoverableItem>(); return ElevatedError.None; }
        public ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items) { items = Array.Empty<RecoverableItem>(); return ElevatedError.None; }
        public ElevatedError LoadAppIdentities(out IReadOnlyList<AppIdentity> items) { items = Array.Empty<AppIdentity>(); return ElevatedError.None; }

        public ElevatedError LoadExemptions(out IReadOnlyList<Exemption> items)
        {
            items = Exemptions;
            return ExemptionsError;
        }

        public ElevatedError ResolveIdentity(string imagePath, out ResolvedIdentity? identity)
        {
            identity = Resolved;
            return ElevatedError.None;
        }

        public ExemptionAdd WhitelistAdd(string imagePath) => AddResult;
        public ElevatedError WhitelistRemove(string imagePath, out uint verdict) { verdict = 0; return ElevatedError.None; }

        public RecoveryOutcome Recover(RecoverableItem item, string targetPath) => new(item, RecoveryOutcomeKind.RestoredVerified, 0, ElevatedError.None);
        public ElevatedError SetMode(uint mode) => ElevatedError.None;
        public ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes) => ElevatedError.None;
        public void Dispose() => Disposed = true;
    }

    private static Exemption Ex(string path, ExemptionMatchState state) => new()
    {
        ImagePath = path,
        CertSubject = "Acme",
        ContentHash = new byte[32],
        MatchState = state,
    };

    [Fact]
    public void Begin_LoadsExemptions_OneElevation_AndReleases()
    {
        int opens = 0;
        FakeChannel channel = new() { Exemptions = [Ex(@"C:\a\app.exe", ExemptionMatchState.Matching)] };
        ExemptionSession session = new(() => { opens++; return channel; });

        session.Begin();

        Assert.Equal(1, opens);
        Assert.Equal(ExemptionSessionState.Loaded, session.State);
        Assert.Single(session.Exemptions);
        Assert.True(channel.Disposed);
    }

    [Fact]
    public void Begin_LoadError_IsUnavailable()
    {
        ExemptionSession session = new(() => new FakeChannel { ExemptionsError = ElevatedError.AccessDenied });
        session.Begin();
        Assert.Equal(ExemptionSessionState.Unavailable, session.State);
        Assert.Equal(ElevatedError.AccessDenied, session.LastError);
    }

    [Fact]
    public void Begin_ConsentDeclined_ReturnsToIdle()
    {
        ExemptionSession session = new(() => throw new OperationCanceledException());
        session.Begin();
        Assert.Equal(ExemptionSessionState.Idle, session.State);
    }

    [Fact]
    public void Add_IsAFreshElevation_ReturningResult()
    {
        int opens = 0;
        ExemptionSession session = new(() =>
        {
            opens++;
            return new FakeChannel { AddResult = new(ExemptionAddResult.RefusedInterpreter, 0, ElevatedError.None) };
        });

        ExemptionAdd r = session.Add(@"C:\Windows\System32\cmd.exe");

        Assert.Equal(1, opens);
        Assert.Equal(ExemptionAddResult.RefusedInterpreter, r.Result);
    }

    [Fact]
    public void Add_ConsentDeclined_IsChannelError()
    {
        ExemptionSession session = new(() => throw new OperationCanceledException());
        ExemptionAdd r = session.Add(@"C:\a\app.exe");
        Assert.Equal(ExemptionAddResult.ChannelError, r.Result);
        Assert.Equal(ElevatedError.AccessDenied, r.Error);
    }

    [Fact]
    public void Resolve_ReturnsIdentity()
    {
        ResolvedIdentity ident = new() { ImagePath = @"C:\a\app.exe", CertSubject = "Acme", ContentHash = new byte[32], Verdict = 0 };
        ExemptionSession session = new(() => new FakeChannel { Resolved = ident });

        ResolvedIdentity? got = session.Resolve(@"C:\a\app.exe", out ElevatedError err);

        Assert.Equal(ElevatedError.None, err);
        Assert.Equal(@"C:\a\app.exe", got?.ImagePath);
    }

    [Fact]
    public void Remove_ReturnsNone_OnSuccess()
    {
        ExemptionSession session = new(() => new FakeChannel());
        Assert.Equal(ElevatedError.None, session.Remove(@"C:\a\app.exe"));
    }
}
