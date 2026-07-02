using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class RecoverySessionTests
{
    private sealed class FakeChannel : IElevatedControlChannel
    {
        public IReadOnlyList<RecoverableItem> Catalog { get; init; } = Array.Empty<RecoverableItem>();
        public IReadOnlyList<RecoverableItem> Preserved { get; init; } = Array.Empty<RecoverableItem>();
        public ElevatedError CatalogError { get; init; } = ElevatedError.None;
        public ElevatedError PreservedError { get; init; } = ElevatedError.None;
        public Func<RecoverableItem, RecoveryOutcome>? OnRecover { get; init; }
        public bool Disposed { get; private set; }

        public ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items)
        {
            items = Catalog;
            return CatalogError;
        }

        public ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items)
        {
            items = Preserved;
            return PreservedError;
        }

        public RecoveryOutcome Recover(RecoverableItem item)
        {
            return OnRecover?.Invoke(item)
                ?? new RecoveryOutcome(item, RecoveryOutcomeKind.RestoredVerified, 0, ElevatedError.None);
        }

        public ElevatedError SetMode(uint mode) => ElevatedError.None;

        public ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes) => ElevatedError.None;

        public void Dispose() => Disposed = true;
    }

    private static RecoverableItem Definitive(string path) =>
        new() { Rung = CertaintyRung.Definitive, ProvenancePath = path, KeyId = new byte[32] };

    private static RecoverableItem Bounded(string path) =>
        new() { Rung = CertaintyRung.Bounded, ProvenancePath = path, Offset = 0, Length = 16 };

    [Fact]
    public void Begin_AggregatesDefinitiveThenBounded()
    {
        FakeChannel channel = new()
        {
            Catalog = [Definitive("a"), Definitive("b")],
            Preserved = [Bounded("c")],
        };
        RecoverySession session = new(() => channel);

        session.Begin();

        Assert.Equal(RecoverySessionState.Browsing, session.State);
        Assert.Equal(3, session.Items.Count);
        Assert.Equal(CertaintyRung.Definitive, session.Items[0].Rung);
        Assert.Equal(CertaintyRung.Definitive, session.Items[1].Rung);
        Assert.Equal(CertaintyRung.Bounded, session.Items[2].Rung);
    }

    [Fact]
    public void Execute_ProducesReport()
    {
        FakeChannel channel = new()
        {
            Catalog = [Definitive("a")],
            OnRecover = item => new RecoveryOutcome(item, RecoveryOutcomeKind.RestoredVerified, 0, ElevatedError.None),
        };
        RecoverySession session = new(() => channel);
        session.Begin();

        session.Execute(session.Items);

        Assert.Equal(RecoverySessionState.Reported, session.State);
        Assert.Single(session.Report);
        Assert.Equal(RecoveryOutcomeKind.RestoredVerified, session.Report[0].Kind);
    }

    [Fact]
    public void Begin_AccessDenied_IsUnavailableAndDisposes()
    {
        FakeChannel channel = new() { CatalogError = ElevatedError.AccessDenied };
        RecoverySession session = new(() => channel);

        session.Begin();

        Assert.Equal(RecoverySessionState.Unavailable, session.State);
        Assert.Equal(ElevatedError.AccessDenied, session.LastError);
        Assert.True(channel.Disposed);
    }

    [Fact]
    public void Begin_ConsentDeclined_ReturnsToIdle()
    {
        RecoverySession session = new(() => throw new OperationCanceledException());

        session.Begin();

        Assert.Equal(RecoverySessionState.Idle, session.State);
        Assert.Equal(ElevatedError.None, session.LastError);
    }

    [Fact]
    public void Close_DisposesAndResets()
    {
        FakeChannel channel = new() { Catalog = [Definitive("a")] };
        RecoverySession session = new(() => channel);
        session.Begin();

        session.Close();

        Assert.Equal(RecoverySessionState.Idle, session.State);
        Assert.True(channel.Disposed);
        Assert.Empty(session.Items);
    }
}
