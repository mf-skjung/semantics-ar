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
    public void SideBySide_InsertsStampBeforeExtension()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string path = RestorePlanner.SideBySidePath(@"\Device\v\docs\a.txt", Anchor, reserved, probe);
        Assert.Equal(@"\Device\v\docs\a_RESTORED_20260701_000000.txt", path);
    }

    [Fact]
    public void SideBySide_SameSecondCollision_Uniquifies()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string first = RestorePlanner.SideBySidePath(@"\Device\v\a.txt", Anchor, reserved, probe);
        string second = RestorePlanner.SideBySidePath(@"\Device\v\a.txt", Anchor, reserved, probe);
        Assert.NotEqual(first, second);
        Assert.EndsWith(@"(1).txt", second);
    }

    [Fact]
    public void SideBySide_ExistingTargetOnDisk_Uniquifies()
    {
        FakeProbe probe = new FakeProbe().Set(@"\Device\v\a_RESTORED_20260701_000000.txt",
            new FileState(true, false, false, true, default));
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string path = RestorePlanner.SideBySidePath(@"\Device\v\a.txt", Anchor, reserved, probe);
        Assert.EndsWith(@"(1).txt", path);
    }

    [Fact]
    public void SideBySide_NoExtension_AppendsSuffix()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string path = RestorePlanner.SideBySidePath(@"\Device\v\README", Anchor, reserved, probe);
        Assert.Equal(@"\Device\v\README_RESTORED_20260701_000000", path);
    }

    [Fact]
    public void SideBySide_LongPath_TrimsNameToBudget()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        string dir = @"\Device\HarddiskVolume3\" + new string('d', 180);
        string original = dir + @"\" + new string('n', 60) + ".txt";
        string path = RestorePlanner.SideBySidePath(original, Anchor, reserved, probe);
        Assert.True(path.Length <= 259);
        Assert.Contains("_RESTORED_20260701_000000.txt", path);
    }

    [Fact]
    public void SideBySide_RootlessPath_Throws()
    {
        FakeProbe probe = new();
        HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
        Assert.Throws<ArgumentException>(() =>
            RestorePlanner.SideBySidePath("a.txt", Anchor, reserved, probe));
    }
}
