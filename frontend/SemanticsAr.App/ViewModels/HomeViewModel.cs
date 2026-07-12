using System.Windows;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.App.Controls;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public partial class HomeViewModel : ObservableObject
{
    public const string CoverageLine =
        "Key-capture recovery covers encryption whose key is present on this PC. "
        + "Encryption driven from another computer, and data-theft-only extortion, "
        + "are outside its reach.";

    private readonly PostureService _posture;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AutomationText))]
    private PostureLevel _level = PostureLevel.Red;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AutomationText))]
    private string _headline = "Checking protection…";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AutomationText))]
    private string _detail = string.Empty;

    [ObservableProperty]
    private string _glyph = "―";

    [ObservableProperty]
    private VerdictTone _sealTone = VerdictTone.Unknown;

    [ObservableProperty]
    private bool _isStale;

    [ObservableProperty]
    private string _modeText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowModePill))]
    private bool _isAudit;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowModePill))]
    [NotifyPropertyChangedFor(nameof(IsEnforcing))]
    private string _modePillText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCaptured))]
    private string _capturedText = string.Empty;

    public HomeViewModel(PostureService posture)
    {
        _posture = posture;
        _posture.PostureChanged += OnPostureChanged;

        if (posture.Current is PostureVerdict current)
            Apply(current);
    }

    public string AutomationText => $"{Headline}. {Detail}";

    public bool ShowModePill => ModePillText.Length > 0;

    public bool IsEnforcing => !IsAudit && ShowModePill;

    public bool HasCaptured => CapturedText.Length > 0;

    private void OnPostureChanged(object? sender, PostureChangedEventArgs e)
    {
        Dispatcher? dispatcher = Application.Current?.Dispatcher;
        if (dispatcher is null || dispatcher.HasShutdownStarted)
            return;
        dispatcher.Invoke(() => Apply(e.Verdict));
    }

    private void Apply(PostureVerdict v)
    {
        Level = v.Level;
        IsStale = v.IsStale;
        Glyph = GlyphFor(v.Level);
        SealTone = ToneFor(v.Reason, v.Level);
        (Headline, Detail) = Describe(v.Reason);
        ModeText = ModeTextFor(v.Mode);
        IsAudit = v.Mode == PostureMode.Audit;
        ModePillText = ModePillTextFor(v.Mode);
        CapturedText = v.CapturedKeyCount > 0
            ? $"{v.CapturedKeyCount} files recoverable by captured key"
            : string.Empty;
    }

    private static VerdictTone ToneFor(PostureReason reason, PostureLevel level) => reason switch
    {
        PostureReason.ServiceUnreachable => VerdictTone.Unknown,
        PostureReason.ServerUntrusted => VerdictTone.Unknown,
        PostureReason.VersionMismatch => VerdictTone.Unknown,
        PostureReason.AccessDenied => VerdictTone.Unknown,
        _ => level switch
        {
            PostureLevel.Green => VerdictTone.Healthy,
            PostureLevel.Amber => VerdictTone.Attention,
            _ => VerdictTone.Alarm,
        },
    };

    private static string ModePillTextFor(PostureMode mode) => mode switch
    {
        PostureMode.Enforce => "Enforcing — blocks encryption",
        PostureMode.Audit => "Audit — recording, not blocking",
        _ => string.Empty,
    };

    private static string GlyphFor(PostureLevel level) => level switch
    {
        PostureLevel.Green => "✓",
        PostureLevel.Amber => "!",
        PostureLevel.Red => "✕",
        _ => "―",
    };

    private static string ModeTextFor(PostureMode mode) => mode switch
    {
        PostureMode.Enforce => "Mode: ENFORCE",
        PostureMode.Audit => "Mode: AUDIT",
        _ => string.Empty,
    };

    private static (string Headline, string Detail) Describe(PostureReason reason) => reason switch
    {
        PostureReason.Protected => (
            "Protected",
            "Enforcing. Destructive encryption is captured and blocked at the first instance."),
        PostureReason.IntegrityHalt => (
            "Protection may be compromised",
            "The protected recovery store failed its integrity check — signs of tampering or "
            + "rollback. Recovery from it can no longer be guaranteed. Investigate this device."),
        PostureReason.AuditMode => (
            "Recording, not blocking",
            "You are in AUDIT. Attacks are recorded and recoverable, but not blocked. "
            + "Switch to ENFORCE to block."),
        PostureReason.DriverDisconnected => (
            "Not protecting",
            "The protection driver is not attached. New attacks are not being observed."),
        PostureReason.ServiceNotRunning => (
            "Not protecting",
            "The protection service is not running."),
        PostureReason.ServiceUnreachable => (
            "Status unavailable",
            "The protection service cannot be reached right now."),
        PostureReason.ServerUntrusted => (
            "Status unavailable",
            "The status channel could not be verified as genuine. This reading is not trusted."),
        PostureReason.VersionMismatch => (
            "Management unavailable",
            "The app and the protection components are different versions. Update to manage."),
        PostureReason.AccessDenied => (
            "Status unavailable",
            "Access to the status channel was denied."),
        _ => ("Status unavailable", string.Empty),
    };
}
