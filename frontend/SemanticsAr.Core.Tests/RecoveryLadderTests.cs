using System.Buffers.Binary;
using System.Text;
using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class RecoveryLadderTests
{
    private static byte[] CatalogEntry(byte[] keyId, uint algorithm, uint mode, string path,
        ulong captureTime = 0, ulong actorStartKey = 0)
    {
        byte[] e = new byte[RecoveryLadder.CatalogEntrySize];
        keyId.CopyTo(e, 0);
        BinaryPrimitives.WriteUInt32LittleEndian(e.AsSpan(32), algorithm);
        BinaryPrimitives.WriteUInt32LittleEndian(e.AsSpan(36), mode);
        Encoding.Unicode.GetBytes(path).CopyTo(e, 40);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(560), captureTime);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(568), actorStartKey);
        return e;
    }

    private static byte[] PreserveEntry(string path, ulong offset, ulong length, ulong capture, ulong size)
    {
        byte[] e = new byte[RecoveryLadder.PreserveEntrySize];
        Encoding.Unicode.GetBytes(path).CopyTo(e, 0);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(520), offset);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(528), length);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(536), capture);
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(544), size);
        return e;
    }

    [Fact]
    public void ParseCatalog_ReadsAllFields()
    {
        byte[] key0 = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
        byte[] key1 = Enumerable.Range(100, 32).Select(i => (byte)i).ToArray();
        byte[] blob = [.. CatalogEntry(key0, 3, 1, @"\Device\HarddiskVolume3\docs\a.txt",
                          133000000000000000, 0xAABBCCDD11223344),
                       .. CatalogEntry(key1, 5, 0, @"\Device\HarddiskVolume3\docs\b.txt")];

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParseCatalog(blob, 2);

        Assert.Equal(2, items.Count);
        Assert.All(items, i => Assert.Equal(CertaintyRung.Definitive, i.Rung));
        Assert.Equal(key0, items[0].KeyId);
        Assert.Equal(3u, items[0].Algorithm);
        Assert.Equal(1u, items[0].Mode);
        Assert.Equal(@"\Device\HarddiskVolume3\docs\a.txt", items[0].ProvenancePath);
        Assert.Equal(133000000000000000ul, items[0].CaptureTime);
        Assert.Equal(0xAABBCCDD11223344ul, items[0].ActorStartKey);
        Assert.Equal(key1, items[1].KeyId);
        Assert.Equal(@"\Device\HarddiskVolume3\docs\b.txt", items[1].ProvenancePath);
    }

    [Fact]
    public void ParsePreserve_ReadsAllFields()
    {
        byte[] blob = PreserveEntry(@"\Device\HarddiskVolume3\img\c.raw", 4096, 65536, 133000000000000000, 262144);

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParsePreserve(blob, 1);

        Assert.Single(items);
        Assert.Equal(CertaintyRung.Bounded, items[0].Rung);
        Assert.Equal(@"\Device\HarddiskVolume3\img\c.raw", items[0].ProvenancePath);
        Assert.Equal(4096ul, items[0].Offset);
        Assert.Equal(65536ul, items[0].Length);
        Assert.Equal(133000000000000000ul, items[0].CaptureTime);
        Assert.Equal(262144ul, items[0].Size);
        Assert.Null(items[0].KeyId);
    }

    [Fact]
    public void ParseCatalog_CountExceedingBlobLength_ReturnsOnlyWhatFits()
    {
        byte[] key0 = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
        byte[] blob = CatalogEntry(key0, 3, 1, @"\Device\HarddiskVolume3\docs\a.txt");

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParseCatalog(blob, 5);

        Assert.Single(items);
        Assert.Equal(key0, items[0].KeyId);
    }

    [Fact]
    public void ParseCatalog_EmptyBlobWithPositiveCount_ReturnsEmpty()
    {
        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParseCatalog(ReadOnlySpan<byte>.Empty, 3);

        Assert.Empty(items);
    }

    [Fact]
    public void ParseCatalog_NegativeCount_ReturnsEmpty()
    {
        byte[] key0 = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
        byte[] blob = CatalogEntry(key0, 3, 1, @"\Device\HarddiskVolume3\docs\a.txt");

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParseCatalog(blob, -1);

        Assert.Empty(items);
    }

    [Fact]
    public void ParseCatalog_TruncatedTrailingEntry_IgnoresPartialEntry()
    {
        byte[] key0 = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
        byte[] full = CatalogEntry(key0, 3, 1, @"\Device\HarddiskVolume3\docs\a.txt");
        byte[] blob = [.. full, .. full.AsSpan(0, RecoveryLadder.CatalogEntrySize / 2)];

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParseCatalog(blob, 2);

        Assert.Single(items);
    }

    [Fact]
    public void ParsePreserve_CountExceedingBlobLength_ReturnsOnlyWhatFits()
    {
        byte[] blob = PreserveEntry(@"\Device\HarddiskVolume3\img\c.raw", 4096, 65536, 133000000000000000, 262144);

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParsePreserve(blob, 4);

        Assert.Single(items);
    }

    [Fact]
    public void ParsePreserve_EmptyBlobWithPositiveCount_ReturnsEmpty()
    {
        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParsePreserve(ReadOnlySpan<byte>.Empty, 2);

        Assert.Empty(items);
    }

    [Fact]
    public void ParsePreserve_NegativeCount_ReturnsEmpty()
    {
        byte[] blob = PreserveEntry(@"\Device\HarddiskVolume3\img\c.raw", 4096, 65536, 133000000000000000, 262144);

        IReadOnlyList<RecoverableItem> items = RecoveryLadder.ParsePreserve(blob, -3);

        Assert.Empty(items);
    }

    [Fact]
    public void MapResult_ZeroIsRestoredVerified()
    {
        RecoverableItem item = new() { Rung = CertaintyRung.Definitive, ProvenancePath = "x" };
        RecoveryOutcome outcome = RecoveryLadder.MapResult(item, 0);
        Assert.Equal(RecoveryOutcomeKind.RestoredVerified, outcome.Kind);
        Assert.Equal(0, outcome.KernelResult);
    }

    [Theory]
    [InlineData(-8)]
    [InlineData(-1)]
    public void MapResult_NonZeroIsDeclinedLeftIntact(int kernelResult)
    {
        RecoverableItem item = new() { Rung = CertaintyRung.Definitive, ProvenancePath = "x" };
        RecoveryOutcome outcome = RecoveryLadder.MapResult(item, kernelResult);
        Assert.Equal(RecoveryOutcomeKind.DeclinedLeftIntact, outcome.Kind);
        Assert.Equal(kernelResult, outcome.KernelResult);
    }
}
