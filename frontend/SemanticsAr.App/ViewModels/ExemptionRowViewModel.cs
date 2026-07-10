using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.ViewModels;

public sealed class ExemptionRowViewModel
{
    private readonly Exemption _exemption;

    public ExemptionRowViewModel(Exemption exemption)
    {
        _exemption = exemption;
    }

    public string ImagePath => _exemption.ImagePath;

    public string Name => LeafName(_exemption.ImagePath);

    public string Monogram => Initials(Name);

    public string Signer => _exemption.CertSubject.Length > 0
        ? _exemption.CertSubject
        : "Unknown publisher";

    public string FirstSeenText => _exemption.FirstSeen is { } seen
        ? $"Exempted {seen.LocalDateTime:MMM d, yyyy}"
        : string.Empty;

    public bool IsMatching => _exemption.MatchState == ExemptionMatchState.Matching;

    public bool IsChangedSigner => _exemption.MatchState == ExemptionMatchState.LapsedChangedSigner;

    public bool CanRemove => _exemption.MatchState == ExemptionMatchState.Matching;

    public string StateText => _exemption.MatchState switch
    {
        ExemptionMatchState.Matching =>
            "Exempt — writes by this app are not kept.",
        ExemptionMatchState.LapsedSameSigner =>
            "Updated by the same publisher — the exemption has lapsed and this app is being monitored again. Re-affirm to exempt the new version.",
        _ =>
            "This app can no longer be confirmed as the same signed publisher — the exemption has lapsed and this app is being monitored again. Review before re-exempting.",
    };

    private static string LeafName(string path)
    {
        ReadOnlySpan<char> span = path.AsSpan();
        int slash = span.LastIndexOfAny('\\', '/');
        ReadOnlySpan<char> leaf = slash >= 0 ? span[(slash + 1)..] : span;
        int dot = leaf.LastIndexOf('.');
        if (dot > 0)
            leaf = leaf[..dot];
        return leaf.Length == 0 ? "Unknown application" : leaf.ToString();
    }

    private static string Initials(string name)
    {
        Span<char> buffer = stackalloc char[2];
        int n = 0;
        foreach (char c in name)
        {
            if (!char.IsLetterOrDigit(c))
                continue;
            buffer[n++] = char.ToUpperInvariant(c);
            if (n == 2)
                break;
        }
        return n == 0 ? "?" : new string(buffer[..n]);
    }
}
