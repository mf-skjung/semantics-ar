using System.Globalization;
using System.Windows;
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
    private readonly Func<IElevatedControlChannel> _channelFactory;
    private BudgetSession? _session;
    private bool _busy;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowPreElevation))]
    [NotifyPropertyChangedFor(nameof(ShowLoaded))]
    [NotifyPropertyChangedFor(nameof(ShowUnavailable))]
    private BudgetStage _stage = BudgetStage.PreElevation;

    [ObservableProperty]
    private string _achievedWindowText = string.Empty;

    [ObservableProperty]
    private string _cacheText = string.Empty;

    [ObservableProperty]
    private string _oldestText = string.Empty;

    [ObservableProperty]
    private string _unavailableText = string.Empty;

    public BudgetViewModel(Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
    }

    public string Summary =>
        "How far back you can restore, and which activity is spending that window. "
        + "Resource hygiene — no app here has done anything wrong. Opening this view reads your "
        + "kept copies over the elevated channel, so it asks for permission once per visit.";

    public bool ShowPreElevation => Stage == BudgetStage.PreElevation;
    public bool ShowLoaded => Stage == BudgetStage.Loaded;
    public bool ShowUnavailable => Stage == BudgetStage.Unavailable;

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
        AchievedWindowText = string.Empty;
        CacheText = string.Empty;
        OldestText = string.Empty;
        UnavailableText = string.Empty;
        Stage = BudgetStage.PreElevation;
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
            case BudgetSessionState.Loaded when _session.Snapshot is { } snapshot:
                Apply(snapshot);
                Stage = BudgetStage.Loaded;
                break;

            case BudgetSessionState.Unavailable:
                UnavailableText = DescribeError(_session.LastError);
                Stage = BudgetStage.Unavailable;
                break;

            default:
                Stage = BudgetStage.PreElevation;
                break;
        }
    }

    private static readonly string[] ByteUnits = ["bytes", "KB", "MB", "GB", "TB"];

    private void Apply(BudgetSnapshot snapshot)
    {
        AchievedWindowText = snapshot.AchievedWindow(DateTimeOffset.Now) is { } window
            ? DescribeWindow(window)
            : snapshot.CopyCount > 0
                ? "Recovery window unknown — kept copies carry no readable capture time yet."
                : "No kept copies are standing by yet.";

        CacheText = snapshot.CopyCount == 1
            ? $"1 kept copy · {DescribeBytes(snapshot.CopyBytes)} held"
            : $"{snapshot.CopyCount:N0} kept copies · {DescribeBytes(snapshot.CopyBytes)} held";

        OldestText = snapshot.OldestCopy is { } oldest
            ? $"Oldest kept copy: {oldest.LocalDateTime:MMM d, yyyy}"
            : string.Empty;
    }

    private static string DescribeWindow(TimeSpan window)
    {
        if (window < TimeSpan.Zero)
            window = TimeSpan.Zero;
        double days = window.TotalDays;
        if (days < 1)
            return "less than a day";
        long whole = (long)Math.Round(days);
        return whole == 1 ? "about 1 day" : $"about {whole} days";
    }

    private static string DescribeBytes(ulong bytes)
    {
        double value = bytes;
        int unit = 0;
        while (unit < ByteUnits.Length - 1
            && Math.Round(value, unit == 0 ? 0 : 1, MidpointRounding.AwayFromZero) >= 1024)
        {
            value /= 1024;
            unit++;
        }

        string number = unit == 0
            ? value.ToString("N0", CultureInfo.CurrentCulture)
            : value.ToString("N1", CultureInfo.CurrentCulture);
        return $"{number} {ByteUnits[unit]}";
    }

    private static string DescribeError(ElevatedError error) => error switch
    {
        ElevatedError.AccessDenied =>
            "Elevation is required to read your recovery budget.",
        ElevatedError.PipeUnavailable =>
            "The protection service cannot be reached right now.",
        ElevatedError.ServerUntrusted =>
            "The control channel could not be verified as genuine. This action was refused.",
        ElevatedError.VersionMismatch =>
            "The app and the protection components are different versions.",
        ElevatedError.Transport =>
            "The control channel failed while reading your recovery budget.",
        _ =>
            "Your recovery budget is unavailable right now.",
    };
}
