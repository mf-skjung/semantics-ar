namespace SemanticsAr.Core.Domain;

public sealed record AppIdentity
{
    public required ulong AppIdentityId { get; init; }
    public required string ImagePath { get; init; }
    public required string CertSubject { get; init; }
    public byte[]? ContentHash { get; init; }
    public uint Verdict { get; init; }
}
