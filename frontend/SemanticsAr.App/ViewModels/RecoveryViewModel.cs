using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Data;
using System.Windows.Threading;
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
    private readonly IFileProbe _probe = new Win32FileProbe();
    private RecoverySession? _session;
    private bool _busy;

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
    [NotifyPropertyChangedFor(nameof(FolderModeChosen))]
    private RestoreDestination _destination = RestoreDestination.OriginalLocations;

    [ObservableProperty]
    private string _unavailableText = string.Empty;

    public bool FolderModeChosen => Destination == RestoreDestination.CopyToFolder;

    public string FolderHint =>
        $"Copies go to {RestorePlanner.DefaultFolderRoot(DateTimeOffset.Now)} — your originals are never touched. "
        + "Verified reconstructions (DEFINITIVE) restore to their original location only and are left out of a folder copy.";

    public RecoveryViewModel(PostureService posture, Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
        Summary = BuildSummary(posture.Current);

        ICollectionView view = CollectionViewSource.GetDefaultView(Items);
        view.GroupDescriptions.Add(new PropertyGroupDescription(nameof(RecoverableItemViewModel.GroupLabel)));

        posture.PostureChanged += (_, e) =>
        {
            Dispatcher? dispatcher = Application.Current?.Dispatcher;
            if (dispatcher is null || dispatcher.HasShutdownStarted)
                return;
            dispatcher.Invoke(() =>
            {
                if (Stage == RecoveryStage.PreElevation)
                    Summary = BuildSummary(e.Verdict);
            });
        };
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
        if (_busy)
            return;

        _busy = true;
        try
        {
            _session = new RecoverySession(_channelFactory);
            _session.Begin();
            SyncFromSession();
        }
        finally
        {
            _busy = false;
        }
    }

    [RelayCommand]
    private void SelectAll()
    {
        if (Stage != RecoveryStage.Browsing)
            return;

        foreach (RecoverableItemViewModel item in Items)
            if (item.CanSelect)
                item.IsSelected = true;
    }

    [RelayCommand]
    private void ClearSelection()
    {
        if (Stage != RecoveryStage.Browsing)
            return;

        foreach (RecoverableItemViewModel item in Items)
            item.IsSelected = false;
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
        if (_session is null || Stage != RecoveryStage.Preview || _busy)
            return;

        _busy = true;
        try
        {
            HashSet<string> reserved = new(StringComparer.OrdinalIgnoreCase);
            string folderRoot = RestorePlanner.DefaultFolderRoot(DateTimeOffset.Now);
            List<RestoreRequest> plan = new();
            List<RecoveryOutcome> unplannable = new();

            foreach (RecoverableItemViewModel item in PreviewItems)
            {
                switch (RestorePlanner.Decide(item.Model.Rung, item.Disposition, Destination))
                {
                    case PlanDecision.InPlace:
                        plan.Add(new RestoreRequest(item.Model, item.Model.ProvenancePath));
                        break;

                    case PlanDecision.ToFolder:
                        try
                        {
                            string target = RestorePlanner.FolderTargetPath(folderRoot, item.Model.ProvenancePath, reserved, _probe);
                            plan.Add(new RestoreRequest(item.Model, target));
                        }
                        catch (Exception ex) when (ex is PathTooLongException or ArgumentException)
                        {
                            unplannable.Add(Decline(item.Model, RecoveryDeclineReason.PathUnavailable));
                        }
                        break;

                    case PlanDecision.DeclineDefinitiveFolder:
                        unplannable.Add(Decline(item.Model, RecoveryDeclineReason.DefinitiveFolderOnly));
                        break;

                    case PlanDecision.DeclineBlocked:
                        unplannable.Add(Decline(item.Model, RecoveryDeclineReason.PathBlocked));
                        break;
                }
            }

            bool executed = plan.Count > 0;
            if (executed)
                _session.Execute(plan);

            if (executed && _session.State == RecoverySessionState.Browsing)
                return;

            if (executed && _session.State == RecoverySessionState.Unavailable)
            {
                UnavailableText = DescribeError(_session.LastError);
                Stage = RecoveryStage.Unavailable;
                return;
            }

            Report.Clear();
            foreach (RecoveryOutcome outcome in unplannable)
                Report.Add(new RecoveryOutcomeViewModel(outcome));
            if (executed)
                foreach (RecoveryOutcome outcome in _session.Report)
                    Report.Add(new RecoveryOutcomeViewModel(outcome));
            Stage = RecoveryStage.Report;
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
                BuildItems(_session.Items);
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

    private void BuildItems(IReadOnlyList<RecoverableItem> snapshot)
    {
        Items.Clear();

        IEnumerable<RecoverableItem> grouped = snapshot.Where(i => i.ActorStartKey != 0);

        foreach (Incident incident in IncidentGrouper.Group(grouped, TimeSpan.MaxValue))
        {
            string label = $"Incident · {incident.FirstSeen.LocalDateTime:g}";
            foreach (RecoverableItem member in incident.Members.Cast<RecoverableItem>())
                Items.Add(MakeItem(member, label));
        }

        foreach (RecoverableItem item in snapshot.Where(i => i.ActorStartKey == 0))
            Items.Add(MakeItem(item, "Held copies"));
    }

    private RecoverableItemViewModel MakeItem(RecoverableItem model, string label)
    {
        RestoreDisposition disposition = RestorePlanner.Classify(model, _probe);
        return new RecoverableItemViewModel(model, disposition, label);
    }

    private static RecoveryOutcome Decline(RecoverableItem item, RecoveryDeclineReason reason) =>
        new(item, RecoveryOutcomeKind.DeclinedLeftIntact, 0, ElevatedError.None) { DeclineReason = reason };

    private static string BuildSummary(PostureVerdict? verdict)
    {
        if (verdict is not { } v || !v.ManagementAvailable)
            return "Recovery management is unavailable right now. Open Home for the current status.";

        return v.CapturedKeyCount > 0
            ? $"{v.CapturedKeyCount} files are recoverable by a captured key. "
              + "Elevate to view every recoverable item and restore."
            : "No captured-key recoveries yet. Elevate to view held copies and restore.";
    }

    private static string DescribeError(ElevatedError error) =>
        ElevatedErrorText.Describe(error, "view and restore recoverable files");
}
