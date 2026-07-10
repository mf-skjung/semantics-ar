using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class RestorePlannerTests
{
    private sealed class FakeProbe : IFileProbe
    {
        private readonly Dictionary<string, FileState> _states = new(StringComparer.OrdinalIgnoreCase);

        public FakeProbe Set(string ntPath, FileState state)
        {
            _states[ntPath] = state;
            return this;
        }

        public FileState Probe(string ntPath) =>
            _states.TryGetValue(ntPath, out FileState s) ? s : new FileState(false, false, false, true, default);
    }

    private static RecoverableItem Item(string path, ulong capture) =>
        new() { Rung = CertaintyRung.Bounded, ProvenancePath = path, CaptureTime = capture };

    private static readonly DateTimeOffset Anchor = new(2026, 7, 1, 0, 0, 0, TimeSpan.Zero);
    private static ulong AnchorFileTime => (ulong)Anchor.ToFileTime();

    [Fact]
    public void ToWin32_PrefixesExtendedGlobalRootForNtPath()
    {
        Assert.Equal(@"\\?\GLOBALROOT\Device\HarddiskVolume3\a.txt",
            RestorePlanner.ToWin32(@"\Device\HarddiskVolume3\a.txt"));
    }

    [Fact]
    public void Anchor_ZeroAndOutOfRange_ReturnNull()
    {
        Assert.Null(RestorePlanner.Anchor(0));
        Assert.Null(RestorePlanner.Anchor(ulong.MaxValue));
    }

    [Fact]
    public void Anchor_ValidFileTime_RoundTrips()
    {
        Assert.Equal(Anchor, RestorePlanner.Anchor(AnchorFileTime));
    }

    [Fact]
    public void Anchor_MaxRimFileTime_AbsorbsInsteadOfThrowing()
    {
        Assert.NotNull(RestorePlanner.Anchor(2650467743999999999UL));
        Assert.Null(RestorePlanner.Anchor(2650467744000000000UL));
    }

    [Fact]
    public void Classify_UnchangedSinceCapture_IsAdditive()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(true, false, false, true, Anchor.AddMinutes(-5)));
        Assert.Equal(RestoreDisposition.Additive,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_ModifiedAfterCapture_IsModifiedSince()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(true, false, false, true, Anchor.AddMinutes(5)));
        Assert.Equal(RestoreDisposition.ModifiedSince,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_Absent_IsDeletedSince()
    {
        FakeProbe probe = new();
        Assert.Equal(RestoreDisposition.DeletedSince,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_Directory_IsBlocked()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(true, true, false, true, default));
        Assert.Equal(RestoreDisposition.Blocked,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_ReparsePoint_IsModifiedSinceRegardlessOfTime()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(true, false, true, true, Anchor.AddMinutes(-5)));
        Assert.Equal(RestoreDisposition.ModifiedSince,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_AccessDenied_IsModifiedSinceConservatively()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(false, false, false, false, default));
        Assert.Equal(RestoreDisposition.ModifiedSince,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", AnchorFileTime), probe));
    }

    [Fact]
    public void Classify_GarbageCaptureTime_IsModifiedSince()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a.txt",
            new FileState(true, false, false, true, Anchor));
        Assert.Equal(RestoreDisposition.ModifiedSince,
            RestorePlanner.Classify(Item(@"\Device\v\a.txt", 0), probe));
    }

    [Fact]
    public void DefaultFolderRoot_IsDatedRecoveredFolder()
    {
        Assert.Equal(@"C:\Recovered\2026-07-01", RestorePlanner.DefaultFolderRoot(Anchor));
    }

    [Theory]
    [InlineData(CertaintyRung.Definitive, RestoreDisposition.Additive, RestoreDestination.OriginalLocations, PlanDecision.InPlace)]
    [InlineData(CertaintyRung.Bounded, RestoreDisposition.Additive, RestoreDestination.OriginalLocations, PlanDecision.InPlace)]
    [InlineData(CertaintyRung.Bounded, RestoreDisposition.Blocked, RestoreDestination.OriginalLocations, PlanDecision.DeclineBlocked)]
    [InlineData(CertaintyRung.Bounded, RestoreDisposition.ModifiedSince, RestoreDestination.CopyToFolder, PlanDecision.ToFolder)]
    [InlineData(CertaintyRung.Bounded, RestoreDisposition.Blocked, RestoreDestination.CopyToFolder, PlanDecision.ToFolder)]
    [InlineData(CertaintyRung.Definitive, RestoreDisposition.Additive, RestoreDestination.CopyToFolder, PlanDecision.DeclineDefinitiveFolder)]
    public void Decide_ResolvesDestinationAndBackendLimits(
        CertaintyRung rung, RestoreDisposition disposition, RestoreDestination destination, PlanDecision expected)
    {
        Assert.Equal(expected, RestorePlanner.Decide(rung, disposition, destination));
    }

    [Fact]
    public void FolderTarget_FlattensLeafUnderFolderRoot()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string path = RestorePlanner.FolderTargetPath(@"C:\Recovered\2026-07-01", @"\Device\v\docs\a.txt", reserved, probe);
        Assert.Equal(@"C:\Recovered\2026-07-01\a.txt", path);
    }

    [Fact]
    public void FolderTarget_SameLeafCollision_Uniquifies()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string first = RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\one\a.txt", reserved, probe);
        string second = RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\two\a.txt", reserved, probe);
        Assert.Equal(@"C:\Recovered\d\a.txt", first);
        Assert.Equal(@"C:\Recovered\d\a (1).txt", second);
    }

    [Fact]
    public void FolderTarget_ExistingTargetOnDisk_Uniquifies()
    {
        FakeProbe probe = new FakeProbe().Set(@"C:\Recovered\d\a.txt",
            new FileState(true, false, false, true, default));
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string path = RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\a.txt", reserved, probe);
        Assert.Equal(@"C:\Recovered\d\a (1).txt", path);
    }

    [Fact]
    public void FolderTarget_NoExtension_UniquifiesWithoutDot()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string first = RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\README", reserved, probe);
        string second = RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\sub\README", reserved, probe);
        Assert.Equal(@"C:\Recovered\d\README", first);
        Assert.Equal(@"C:\Recovered\d\README (1)", second);
    }

    [Fact]
    public void FolderTarget_TrailingSeparator_Throws()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        Assert.Throws<ArgumentException>(() =>
            RestorePlanner.FolderTargetPath(@"C:\Recovered\d", @"\Device\v\docs\", reserved, probe));
    }

    [Fact]
    public void FolderTarget_LongLeaf_TrimsNameToBudget()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string original = @"\Device\v\" + new string('n', 300) + ".txt";
        string path = RestorePlanner.FolderTargetPath(@"C:\Recovered\2026-07-01", original, reserved, probe);
        Assert.True(path.Length <= 259);
        Assert.EndsWith(".txt", path);
    }
}
