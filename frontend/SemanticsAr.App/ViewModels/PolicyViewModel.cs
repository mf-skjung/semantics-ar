using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public enum PolicyStage
{
    PreElevation,
    List,
    ConfirmExempt,
    Unavailable,
}

public partial class PolicyViewModel : ObservableObject
{
    private readonly Func<IElevatedControlChannel> _channelFactory;
    private readonly Func<string?> _pickApp;
    private ExemptionSession? _session;
    private string _pendingPath = string.Empty;
    private ResolvedIdentity? _pendingIdentity;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(BeginCommand))]
    [NotifyCanExecuteChangedFor(nameof(AddByBrowseCommand))]
    [NotifyCanExecuteChangedFor(nameof(ConfirmExemptActionCommand))]
    [NotifyCanExecuteChangedFor(nameof(RemoveCommand))]
    private bool _isBusy;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowPreElevation))]
    [NotifyPropertyChangedFor(nameof(ShowList))]
    [NotifyPropertyChangedFor(nameof(ShowConfirm))]
    [NotifyPropertyChangedFor(nameof(ShowUnavailable))]
    [NotifyPropertyChangedFor(nameof(ShowEmpty))]
    private PolicyStage _stage = PolicyStage.PreElevation;

    [ObservableProperty]
    private string _statusText = string.Empty;

    [ObservableProperty]
    private string _unavailableText = string.Empty;

    [ObservableProperty]
    private string _confirmName = string.Empty;

    [ObservableProperty]
    private string _confirmPath = string.Empty;

    [ObservableProperty]
    private string _confirmSigner = string.Empty;

    [ObservableProperty]
    private string _confirmHash = string.Empty;

    [ObservableProperty]
    private string _confirmCostText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanConfirm))]
    private bool _confirmVerified;

    [ObservableProperty]
    private string _confirmSignatureText = string.Empty;

    public PolicyViewModel(Func<IElevatedControlChannel> channelFactory, Func<string?> pickApp)
    {
        _channelFactory = channelFactory;
        _pickApp = pickApp;
    }

    public ObservableCollection<ExemptionRowViewModel> Exemptions { get; } = new();

    public string Summary =>
        "Apps you have exempted are no longer versioned — the system keeps no recoverable copies of what they change. "
        + "Exempting is a deliberate trade-off, gated by permission each time. Opening this view reads the current "
        + "exemptions over the elevated channel, so it asks for permission once per visit.";

    public string ConfirmConsequence =>
        "Files this app changes while it is exempt become UNRECOVERABLE, and that cannot be undone for changes made while it is exempt.";

    public bool ShowPreElevation => Stage == PolicyStage.PreElevation;
    public bool ShowList => Stage == PolicyStage.List;
    public bool ShowConfirm => Stage == PolicyStage.ConfirmExempt;
    public bool ShowUnavailable => Stage == PolicyStage.Unavailable;
    public bool ShowEmpty => Stage == PolicyStage.List && Exemptions.Count == 0;
    public bool CanConfirm => ConfirmVerified;

    private bool NotBusy => !IsBusy;

    [RelayCommand(CanExecute = nameof(NotBusy))]
    private void Begin()
    {
        IsBusy = true;
        try
        {
            StatusText = string.Empty;
            _session ??= new ExemptionSession(_channelFactory);
            _session.Begin();
            SyncFromSession();
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private void Close()
    {
        if (IsBusy)
            return;
        _session?.Close();
        _session = null;
        Exemptions.Clear();
        StatusText = string.Empty;
        UnavailableText = string.Empty;
        Stage = PolicyStage.PreElevation;
    }

    [RelayCommand(CanExecute = nameof(NotBusy))]
    private void AddByBrowse()
    {
        IsBusy = true;
        try
        {
            string? path = _pickApp();
            if (string.IsNullOrEmpty(path))
                return;
            StartExempt(path, string.Empty);
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private void CancelExempt()
    {
        if (IsBusy)
            return;
        _pendingPath = string.Empty;
        _pendingIdentity = null;
        StatusText = string.Empty;
        EnsureListStage();
    }

    [RelayCommand(CanExecute = nameof(NotBusy))]
    private void ConfirmExemptAction()
    {
        if (_session is null || _pendingPath.Length == 0 || _pendingIdentity is null)
            return;

        IsBusy = true;
        try
        {
            ExemptionAdd add = _session.Add(_pendingPath);
            StatusText = DescribeAdd(add);
            if (add.Result == ExemptionAddResult.Added)
            {
                Exemptions.Add(new ExemptionRowViewModel(new Exemption
                {
                    ImagePath = _pendingPath,
                    CertSubject = _pendingIdentity.CertSubject,
                    ContentHash = _pendingIdentity.ContentHash,
                    FirstSeen = DateTimeOffset.Now,
                    MatchState = ExemptionMatchState.Matching,
                }));
                OnPropertyChanged(nameof(ShowEmpty));
                Stage = PolicyStage.List;
            }
            else
            {
                EnsureListStage();
            }
            _pendingPath = string.Empty;
            _pendingIdentity = null;
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand(CanExecute = nameof(NotBusy))]
    private void Remove(ExemptionRowViewModel? row)
    {
        if (_session is null || row is null || !row.CanRemove)
            return;

        IsBusy = true;
        try
        {
            ElevatedError err = _session.Remove(row.ImagePath);
            if (err != ElevatedError.None)
            {
                StatusText = ElevatedErrorText.Describe(err, "remove this exemption");
                return;
            }
            Exemptions.Remove(row);
            OnPropertyChanged(nameof(ShowEmpty));
            StatusText = $"Exemption for {row.Name} removed.";
        }
        finally
        {
            IsBusy = false;
        }
    }

    public void BeginExempt(string imagePath, string costText)
    {
        if (IsBusy)
            return;
        IsBusy = true;
        try
        {
            _session ??= new ExemptionSession(_channelFactory);
            StartExempt(imagePath, costText);
        }
        finally
        {
            IsBusy = false;
        }
    }

    private void StartExempt(string imagePath, string costText)
    {
        if (_session is null)
            return;

        ResolvedIdentity? identity = _session.Resolve(imagePath, out ElevatedError err);
        if (err != ElevatedError.None)
        {
            StatusText = ElevatedErrorText.Describe(err, "read this app's identity");
            EnsureListStage();
            return;
        }
        if (identity is null)
        {
            StatusText = "This app's identity could not be read.";
            EnsureListStage();
            return;
        }

        _pendingPath = imagePath;
        _pendingIdentity = identity;
        ConfirmName = LeafName(imagePath);
        ConfirmPath = imagePath;
        ConfirmSigner = identity.CertSubject.Length > 0 ? identity.CertSubject : "Unknown publisher";
        ConfirmHash = identity.Verdict == 0 ? HashPrefix(identity.ContentHash) : string.Empty;
        ConfirmCostText = costText;
        ConfirmVerified = identity.Verdict == 0;
        ConfirmSignatureText = identity.Verdict switch
        {
            0 => "Signature valid.",
            1 => "This app is unsigned and cannot be exempted.",
            _ => "This app's signature could not be verified and it cannot be exempted.",
        };
        Stage = PolicyStage.ConfirmExempt;
    }

    private void EnsureListStage()
    {
        Stage = _session?.State == ExemptionSessionState.Loaded ? PolicyStage.List : PolicyStage.PreElevation;
    }

    private void SyncFromSession()
    {
        if (_session is null)
        {
            Stage = PolicyStage.PreElevation;
            return;
        }

        switch (_session.State)
        {
            case ExemptionSessionState.Loaded:
                Exemptions.Clear();
                foreach (Exemption e in _session.Exemptions)
                    Exemptions.Add(new ExemptionRowViewModel(e));
                OnPropertyChanged(nameof(ShowEmpty));
                Stage = PolicyStage.List;
                break;

            case ExemptionSessionState.Unavailable:
                UnavailableText = ElevatedErrorText.Describe(_session.LastError, "read your exemptions");
                Stage = PolicyStage.Unavailable;
                break;

            default:
                Stage = PolicyStage.PreElevation;
                break;
        }
    }

    private static string DescribeAdd(ExemptionAdd add) => add.Result switch
    {
        ExemptionAddResult.Added => "Exemption added.",
        ExemptionAddResult.RefusedInterpreter => "This is a script host and can never be exempted.",
        ExemptionAddResult.NotVerified => "This app is not signed by a verified publisher, so it cannot be exempted.",
        _ => ElevatedErrorText.Describe(add.Error, "add this exemption"),
    };

    private static string HashPrefix(byte[] hash)
    {
        if (hash.Length == 0)
            return string.Empty;
        int n = Math.Min(4, hash.Length);
        return "sha256 " + Convert.ToHexString(hash, 0, n).ToLowerInvariant() + "…";
    }

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
}
