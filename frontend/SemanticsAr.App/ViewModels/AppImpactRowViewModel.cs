using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.ViewModels;

public sealed class FileClassRow(FileClassBucket bucket)
{
    public string Label { get; } = FileClassifier.Label(bucket.Class);

    public string Detail { get; } =
        $"{(bucket.CopyCount == 1 ? "1 item" : $"{bucket.CopyCount:N0} items")} · {BudgetFormat.Bytes(bucket.Bytes)}";
}

public sealed partial class AppImpactRowViewModel : ObservableObject
{
    private readonly AppImpact _impact;
    private readonly TimeSpan? _window;
    private readonly BudgetRange _range;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ChevronGlyph))]
    private bool _isExpanded;

    public AppImpactRowViewModel(AppImpact impact, TimeSpan? window, BudgetRange range)
    {
        _impact = impact;
        _window = window;
        _range = range;
        Classes = impact.Classes.Select(c => new FileClassRow(c)).ToList();
    }

    public bool IsUnattributed => _impact.Kind == AppImpactKind.Unattributed;
    public bool IsGrouped => _impact.Kind == AppImpactKind.Grouped;
    public bool IsAttributed => _impact.Kind == AppImpactKind.Attributed;

    public string Name => _impact.DisplayName;

    public string Monogram => _impact.Kind switch
    {
        AppImpactKind.Unattributed => "–",
        AppImpactKind.Grouped => "…",
        _ => Initials(_impact.DisplayName),
    };

    public string Sub => _impact.Kind switch
    {
        AppImpactKind.Unattributed => "Unattributed kept copies · only drains as copies age out",
        AppImpactKind.Grouped => $"{(_impact.GroupedAppCount == 1 ? "1 app" : $"{_impact.GroupedAppCount:N0} apps")}, each under 1% of the window",
        _ => SignerLine(),
    };

    public int SharePercent => (int)Math.Round(_impact.WindowShare * 100, MidpointRounding.AwayFromZero);

    public string ShareText => $"{SharePercent}%";

    public string ImpactText
    {
        get
        {
            if (_impact.TimeImpact(_window) is not { } impact)
                return $"{SharePercent}% of kept copies in view";
            double days = impact.TotalDays;
            return days < 1
                ? "under a day of your window"
                : $"{BudgetFormat.Days(impact)} of your window";
        }
    }

    public string DeltaText
    {
        get
        {
            if (_impact.DeltaBytes is not { } delta || PriorLabel() is not { } prior)
                return string.Empty;
            if (delta == 0)
                return $"— no change vs {prior}";
            string arrow = delta > 0 ? "▲ +" : "▼ −";
            return $"{arrow}{BudgetFormat.Bytes((ulong)Math.Abs(delta))} vs {prior}";
        }
    }

    public string CostText =>
        IsAttributed ? $"Keeping {BudgetFormat.Copies(_impact.CopyCount)} · {BudgetFormat.Bytes(_impact.Bytes)} from this app" : string.Empty;

    public string Explanation => _impact.Kind switch
    {
        AppImpactKind.Unattributed =>
            "These copies were kept before per-app attribution shipped, so they can't be traced to an app. "
            + "The bucket only drains as copies age out — newer copies carry attribution, so the per-app rows account for more of your window over time.",
        AppImpactKind.Grouped =>
            "Apps with a very small share are grouped here to keep the list legible.",
        _ => string.Empty,
    };

    public bool HasClasses => Classes.Count > 0;

    public bool HasDelta => DeltaText.Length > 0;

    public bool ShowExplanation => Explanation.Length > 0;

    public string ChevronGlyph => IsExpanded ? "▾" : "▸";

    public IReadOnlyList<FileClassRow> Classes { get; }

    [RelayCommand]
    private void ToggleExpand() => IsExpanded = !IsExpanded;

    private string SignerLine() => _impact.Signature switch
    {
        AppSignature.Verified => _impact.Signer.Length > 0 ? $"{_impact.Signer} · signature valid" : "Signed",
        AppSignature.Unsigned => "Unsigned publisher",
        _ => "Signature could not be verified",
    };

    private string? PriorLabel() => _range switch
    {
        BudgetRange.Last24Hours => "the prior 24 h",
        BudgetRange.Last7Days => "the prior 7 days",
        _ => null,
    };

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
