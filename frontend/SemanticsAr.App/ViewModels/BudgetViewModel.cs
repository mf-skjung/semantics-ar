using System.Collections.ObjectModel;
using System.Windows.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public enum BudgetStage
{
    PreElevation,
    Loaded,
    Unavailable,
}

public partial class BudgetViewModel : ObservableObject
{
    private const double TrendWidth = 560;
    private const double TrendHeight = 64;
    private const double TrendPad = 6;

    private readonly Func<IElevatedControlChannel> _channelFactory;
    private BudgetSession? _session;
    private bool _busy;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowPreElevation))]
    [NotifyPropertyChangedFor(nameof(ShowLoaded))]
    [NotifyPropertyChangedFor(nameof(ShowUnavailable))]
    [NotifyPropertyChangedFor(nameof(ShowApps))]
    private BudgetStage _stage = BudgetStage.PreElevation;

    [ObservableProperty]
    private BudgetRange _selectedRange = BudgetRange.Last7Days;

    [ObservableProperty]
    private string _achievedWindowText = string.Empty;

    [ObservableProperty]
    private string _heldText = string.Empty;

    [ObservableProperty]
    private string _oldestText = string.Empty;

    [ObservableProperty]
    private string _unavailableText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowApps))]
    private bool _isEmpty;

    [ObservableProperty]
    private string _emptyText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowTrend))]
    private PointCollection _trendPoints = new();

    public BudgetViewModel(Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
    }

    public ObservableCollection<AppImpactRowViewModel> Apps { get; } = new();

    public string Summary =>
        "How far back you can restore, and which activity is spending that window. "
        + "Resource hygiene — no app here has done anything wrong. Opening this view reads your "
        + "kept copies over the elevated channel, so it asks for permission once per visit.";

    public bool ShowPreElevation => Stage == BudgetStage.PreElevation;
    public bool ShowLoaded => Stage == BudgetStage.Loaded;
    public bool ShowUnavailable => Stage == BudgetStage.Unavailable;
    public bool ShowApps => Stage == BudgetStage.Loaded && !IsEmpty;
    public bool ShowTrend => TrendPoints.Count > 1;

    [RelayCommand]
    private void Begin()
    {
        if (_busy)
            return;

        _busy = true;
        try
        {
            _session = new BudgetSession(_channelFactory);
            _session.Begin();
            SyncFromSession();
        }
        finally
        {
            _busy = false;
        }
    }

    [RelayCommand]
    private void Close()
    {
        if (_busy)
            return;

        _session?.Close();
        _session = null;
        Apps.Clear();
        TrendPoints = new();
        AchievedWindowText = string.Empty;
        HeldText = string.Empty;
        OldestText = string.Empty;
        UnavailableText = string.Empty;
        EmptyText = string.Empty;
        IsEmpty = false;
        Stage = BudgetStage.PreElevation;
    }

    partial void OnSelectedRangeChanged(BudgetRange value)
    {
        if (_session?.State == BudgetSessionState.Loaded)
            Recompute();
    }

    private void SyncFromSession()
    {
        if (_session is null)
        {
            Stage = BudgetStage.PreElevation;
            return;
        }

        switch (_session.State)
        {
            case BudgetSessionState.Loaded:
                Recompute();
                Stage = BudgetStage.Loaded;
                break;

            case BudgetSessionState.Unavailable:
                UnavailableText = ElevatedErrorText.Describe(_session.LastError, "read your recovery budget");
                Stage = BudgetStage.Unavailable;
                break;

            default:
                Stage = BudgetStage.PreElevation;
                break;
        }
    }

    private void Recompute()
    {
        if (_session is null)
            return;

        BudgetAttribution attribution = _session.Compute(SelectedRange, DateTimeOffset.Now);

        IsEmpty = attribution.Apps.Count == 0;
        EmptyText = attribution.TotalCopyCount == 0
            ? "No kept copies are standing by yet. When apps replace read originals, the copies held for you will show up here, grouped by the app that caused them."
            : "No attributed activity in this range. Widen the range to see older kept copies.";

        AchievedWindowText = attribution.AchievedWindow is { } window
            ? BudgetFormat.Days(window)
            : attribution.TotalCopyCount > 0
                ? "unknown — kept copies carry no readable capture time yet"
                : "no kept copies yet";

        HeldText = $"{BudgetFormat.Copies(attribution.TotalCopyCount)} · {BudgetFormat.Bytes(attribution.TotalBytes)} held";

        OldestText = attribution.OldestCopy is { } oldest
            ? $"Oldest kept copy: {oldest.LocalDateTime:MMM d, yyyy}"
            : string.Empty;

        Apps.Clear();
        foreach (AppImpact impact in attribution.Apps)
            Apps.Add(new AppImpactRowViewModel(impact, attribution.AchievedWindow, attribution.Range));

        TrendPoints = BuildTrend(attribution.Trend);
    }

    private static PointCollection BuildTrend(IReadOnlyList<TrendPoint> trend)
    {
        ulong max = 0;
        foreach (TrendPoint point in trend)
            max = Math.Max(max, point.Bytes);
        if (max == 0 || trend.Count < 2)
            return new PointCollection();

        PointCollection points = new(trend.Count);
        double usable = TrendHeight - 2 * TrendPad;
        for (int i = 0; i < trend.Count; i++)
        {
            double x = i / (double)(trend.Count - 1) * TrendWidth;
            double y = TrendPad + (1 - trend[i].Bytes / (double)max) * usable;
            points.Add(new System.Windows.Point(x, y));
        }
        points.Freeze();
        return points;
    }
}
