using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace SemanticsAr.App.ViewModels;

public partial class OnboardingViewModel : ObservableObject
{
    public const int StepCount = 4;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowWelcome))]
    [NotifyPropertyChangedFor(nameof(ShowAssets))]
    [NotifyPropertyChangedFor(nameof(ShowMode))]
    [NotifyPropertyChangedFor(nameof(ShowTray))]
    [NotifyPropertyChangedFor(nameof(IsFirstStep))]
    [NotifyPropertyChangedFor(nameof(ShowBack))]
    [NotifyPropertyChangedFor(nameof(IsLastStep))]
    [NotifyPropertyChangedFor(nameof(PrimaryLabel))]
    [NotifyPropertyChangedFor(nameof(StepLabel))]
    private int _step;

    [ObservableProperty]
    private bool _completed;

    public string CoverageLine => HomeViewModel.CoverageLine;

    public string PlatformPreempt =>
        "Windows may warn you when the protection driver loads, or flag this app as unrecognised. "
        + "That is expected: kernel-level protection needs a signed driver, and a newly published app "
        + "has not built reputation yet. Continue past those prompts — they are part of setup, not a fault.";

    public string TwoAssets =>
        "Protection rests on two recovery assets. When a destructive change is observed, its "
        + "transformation key is captured so the original can be rebuilt and checked byte-for-byte "
        + "(DEFINITIVE, no time limit). And a copy of the original bytes is kept for a bounded, expiring "
        + "window (BOUNDED). Between them, most destructive change can be undone.";

    public string ModeIntro =>
        "You are starting in AUDIT — attacks are recorded and stay recoverable, but are not blocked. "
        + "It is the calm way to learn which apps to trust before enforcing.";

    public string EnforceFuture =>
        "When you are ready to block automatically, adopt ENFORCE from the mode chip. ENFORCE blocks on "
        + "any of three triggers: proven encryption at the first instance, preservation-capacity exhaustion, "
        + "and phantom conviction. A conviction is cryptographic proof, never a false accusation — but "
        + "legitimate bulk encryption is behaviourally indistinguishable and will be blocked unless you "
        + "exempt that app.";

    public string TrayHelp =>
        "Pin the shield icon so the protection verdict is always one glance away: open the tray overflow "
        + "(the ⌃ chevron on the taskbar), then drag the shield onto the taskbar to keep it visible.";

    public bool ShowWelcome => Step == 0;
    public bool ShowAssets => Step == 1;
    public bool ShowMode => Step == 2;
    public bool ShowTray => Step == 3;

    public bool IsFirstStep => Step == 0;
    public bool ShowBack => Step > 0;
    public bool IsLastStep => Step == StepCount - 1;

    public string PrimaryLabel => IsLastStep ? "Finish" : "Next";
    public string StepLabel => $"Step {Step + 1} of {StepCount}";

    [RelayCommand]
    private void Next()
    {
        if (IsLastStep)
            Completed = true;
        else
            Step++;
    }

    [RelayCommand]
    private void Back()
    {
        if (!IsFirstStep)
            Step--;
    }
}
