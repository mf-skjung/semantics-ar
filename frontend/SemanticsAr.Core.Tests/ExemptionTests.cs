using System.Buffers.Binary;
using System.Text;
using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class ExemptionTests
{
    private static byte[] Entry(string image, string subject, byte hash0, ulong firstSeen, uint matchState)
    {
        byte[] e = new byte[RecoveryLadder.WhitelistEntrySize];
        Encoding.Unicode.GetBytes(image).CopyTo(e, 0);
        Encoding.Unicode.GetBytes(subject).CopyTo(e, 520);
        e[1032] = hash0;
        BinaryPrimitives.WriteUInt64LittleEndian(e.AsSpan(1064, 8), firstSeen);
        BinaryPrimitives.WriteUInt32LittleEndian(e.AsSpan(1072, 4), matchState);
        return e;
    }

    [Fact]
    public void ParseExemptions_ReadsAllFields()
    {
        DateTimeOffset seen = new(2026, 7, 1, 0, 0, 0, TimeSpan.Zero);
        byte[] blob = Entry(@"C:\a\app.exe", "Acme Co", 0xAB, (ulong)seen.ToFileTime(), 1);

        Exemption ex = Assert.Single(RecoveryLadder.ParseExemptions(blob, 1));
        Assert.Equal(@"C:\a\app.exe", ex.ImagePath);
        Assert.Equal("Acme Co", ex.CertSubject);
        Assert.Equal(0xAB, ex.ContentHash[0]);
        Assert.Equal(ExemptionMatchState.LapsedSameSigner, ex.MatchState);
        Assert.Equal(seen, ex.FirstSeen);
    }

    [Fact]
    public void ParseExemptions_ClampsCountToBuffer()
    {
        byte[] blob = new byte[RecoveryLadder.WhitelistEntrySize];
        Assert.Single(RecoveryLadder.ParseExemptions(blob, 5));
    }

    [Fact]
    public void ParseExemptions_ZeroFirstSeen_IsNull()
    {
        byte[] blob = Entry(@"C:\a\app.exe", "Acme Co", 0x01, 0, 0);
        Exemption ex = Assert.Single(RecoveryLadder.ParseExemptions(blob, 1));
        Assert.Null(ex.FirstSeen);
        Assert.Equal(ExemptionMatchState.Matching, ex.MatchState);
    }

    [Theory]
    [InlineData(0u, ExemptionMatchState.Matching)]
    [InlineData(1u, ExemptionMatchState.LapsedSameSigner)]
    [InlineData(2u, ExemptionMatchState.LapsedChangedSigner)]
    [InlineData(99u, ExemptionMatchState.LapsedChangedSigner)]
    public void MatchStateOf_MapsValues(uint value, ExemptionMatchState expected)
    {
        Assert.Equal(expected, Exemption.MatchStateOf(value));
    }

    [Fact]
    public void ParseIdentity_ReadsFields()
    {
        byte[] blob = new byte[RecoveryLadder.IdentitySize];
        Encoding.Unicode.GetBytes(@"C:\a\app.exe").CopyTo(blob, 0);
        Encoding.Unicode.GetBytes("Acme Co").CopyTo(blob, 520);
        blob[1032] = 0xCD;

        ResolvedIdentity? id = RecoveryLadder.ParseIdentity(blob, 3);
        Assert.NotNull(id);
        Assert.Equal(@"C:\a\app.exe", id!.ImagePath);
        Assert.Equal("Acme Co", id.CertSubject);
        Assert.Equal(0xCD, id.ContentHash[0]);
        Assert.Equal(3u, id.Verdict);
    }

    [Fact]
    public void ParseIdentity_ShortBlob_IsNull()
    {
        Assert.Null(RecoveryLadder.ParseIdentity(new byte[16], 0));
    }
}
