using System.Collections.ObjectModel;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public enum RecoveryStage
{
    PreElevation,
    Browsing,
    Preview,
    Report,
    Unavailable,
}

public partial class RecoveryViewModel : ObservableObject
{
    private readonly Func<IElevatedControlChannel> _channelFactory;
    private RecoverySession? _session;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowPreElevation))]
    [NotifyPropertyChangedFor(nameof(ShowBrowsing))]
    [NotifyPropertyChangedFor(nameof(ShowPreview))]
    [NotifyPropertyChangedFor(nameof(ShowReport))]
    [NotifyPropertyChangedFor(nameof(ShowUnavailable))]
    private RecoveryStage _stage = RecoveryStage.PreElevation;

    [ObservableProperty]
    private string _summary = string.Empty;

    [ObservableProperty]
    private string _unavailableText = string.Empty;

    public RecoveryViewModel(PostureService posture, Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
        Summary = BuildSummary(posture.Current);
        posture.PostureChanged += (_, e) =>
            Application.Current.Dispatcher.Invoke(() =>
            {
                if (Stage == RecoveryStage.PreElevation)
                    Summary = BuildSummary(e.Verdict);
            });
    }

    public ObservableCollection<RecoverableItemViewModel> Items { get; } = new();

    public ObservableCollection<RecoverableItemViewModel> PreviewItems { get; } = new();

    public ObservableCollection<RecoveryOutcomeViewModel> Report { get; } = new();

    public bool ShowPreElevation => Stage == RecoveryStage.PreElevation;
    public bool ShowBrowsing => Stage == RecoveryStage.Browsing;
    public bool ShowPreview => Stage == RecoveryStage.Preview;
    public bool ShowReport => Stage == RecoveryStage.Report;
    public bool ShowUnavailable => Stage == RecoveryStage.Unavailable;

    [RelayCommand]
    private void Begin()
    {
        _session = new RecoverySession(_channelFactory);
        _session.Begin();
        SyncFromSession();
    }

    [RelayCommand]
    private void Preview()
    {
        if (Stage != RecoveryStage.Browsing)
            return;

        PreviewItems.Clear();
        foreach (RecoverableItemViewModel item in Items)
            if (item.IsSelected)
                PreviewItems.Add(item);

        if (PreviewItems.Count > 0)
            Stage = RecoveryStage.Preview;
    }

    [RelayCommand]
    private void BackToBrowsing()
    {
        if (Stage == RecoveryStage.Preview)
            Stage = RecoveryStage.Browsing;
    }

    [RelayCommand]
    private void Confirm()
    {
        if (_session is null || Stage != RecoveryStage.Preview)
            return;

        List<RecoverableItem> selection = new();
        foreach (RecoverableItemViewModel item in PreviewItems)
            selection.Add(item.Model);

        _session.Execute(selection);

        Report.Clear();
        foreach (RecoveryOutcome outcome in _session.Report)
            Report.Add(new RecoveryOutcomeViewModel(outcome));

        Stage = RecoveryStage.Report;
    }

    [RelayCommand]
    private void Close()
    {
        _session?.Close();
        _session = null;
        Items.Clear();
        PreviewItems.Clear();
        Report.Clear();
        UnavailableText = string.Empty;
        Stage = RecoveryStage.PreElevation;
    }

    private void SyncFromSession()
    {
        if (_session is null)
        {
            Stage = RecoveryStage.PreElevation;
            return;
        }

        switch (_session.State)
        {
            case RecoverySessionState.Browsing:
                Items.Clear();
                foreach (RecoverableItem item in _session.Items)
                    Items.Add(new RecoverableItemViewModel(item));
                Stage = RecoveryStage.Browsing;
                break;

            case RecoverySessionState.Unavailable:
                UnavailableText = DescribeError(_session.LastError);
                Stage = RecoveryStage.Unavailable;
                break;

            default:
                Stage = RecoveryStage.PreElevation;
                break;
        }
    }

    private static string BuildSummary(PostureVerdict? verdict)
    {
        if (verdict is not { } v || !v.ManagementAvailable)
            return "Recovery management is unavailable right now. Open Home for the current status.";

        return v.CapturedKeyCount > 0
            ? $"{v.CapturedKeyCount} files are recoverable by a captured key. "
              + "Elevate to view every recoverable item and restore."
            : "No captured-key recoveries yet. Elevate to view held copies and restore.";
    }

    private static string DescribeError(ElevatedError error) => error switch
    {
        ElevatedError.AccessDenied =>
            "Elevation is required to view and restore recoverable files.",
        ElevatedError.PipeUnavailable =>
            "The protection service cannot be reached right now.",
        ElevatedError.ServerUntrusted =>
            "The control channel could not be verified as genuine. This action was refused.",
        ElevatedError.VersionMismatch =>
            "The app and the protection components are different versions.",
        ElevatedError.Transport =>
            "The control channel failed while reading recoverable files.",
        _ =>
            "Recovery is unavailable right now.",
    };
}
