using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public enum ModeStage
{
    Confirm,
    Applied,
    Unavailable,
}

public partial class ModeControlViewModel : ObservableObject
{
    private readonly Func<IElevatedControlChannel> _channelFactory;
    private readonly uint _targetMode;
    private bool _busy;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowConfirm))]
    [NotifyPropertyChangedFor(nameof(ShowApplied))]
    [NotifyPropertyChangedFor(nameof(ShowUnavailable))]
    private ModeStage _stage = ModeStage.Confirm;

    [ObservableProperty]
    private string _resultText = string.Empty;

    public ModeControlViewModel(PostureMode current, Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
        Current = current;
        Target = current == PostureMode.Enforce ? PostureMode.Audit : PostureMode.Enforce;
        _targetMode = Target == PostureMode.Enforce ? 1u : 0u;
    }

    public PostureMode Current { get; }

    public PostureMode Target { get; }

    public bool IsEnforceAdoption => Target == PostureMode.Enforce;

    public string Title => IsEnforceAdoption ? "Switch to ENFORCE?" : "Switch to AUDIT?";

    public string Consequence => IsEnforceAdoption
        ? "ENFORCE blocks destructive encryption autonomously. A block fires on any of three triggers: "
          + "proven encryption at the first instance, preservation-capacity exhaustion, and phantom conviction. "
          + "A conviction is cryptographic proof — never a false accusation — but legitimate bulk encryption is "
          + "behaviourally indistinguishable and will be blocked unless you exempt that app. This is reversible."
        : "AUDIT records attacks and keeps them recoverable, but does not block them. It is the posture for "
          + "discovering which apps to trust before enforcing. Capture and recovery continue; nothing is disarmed.";

    public string ConfirmLabel => IsEnforceAdoption ? "Adopt ENFORCE" : "Switch to AUDIT";

    public bool ShowConfirm => Stage == ModeStage.Confirm;
    public bool ShowApplied => Stage == ModeStage.Applied;
    public bool ShowUnavailable => Stage == ModeStage.Unavailable;

    [RelayCommand]
    private void Confirm()
    {
        if (_busy || Stage != ModeStage.Confirm)
            return;

        _busy = true;
        try
        {
            (bool cancelled, ElevatedError err) = Apply();
            if (cancelled)
                return;

            if (err == ElevatedError.None)
            {
                ResultText = IsEnforceAdoption
                    ? "ENFORCE is on. Destructive encryption is now blocked at the first instance."
                    : "AUDIT is on. Attacks are recorded and recoverable; blocking is off.";
                Stage = ModeStage.Applied;
            }
            else
            {
                ResultText = DescribeError(err);
                Stage = ModeStage.Unavailable;
            }
        }
        finally
        {
            _busy = false;
        }
    }

    private (bool Cancelled, ElevatedError Error) Apply()
    {
        try
        {
            using IElevatedControlChannel channel = _channelFactory();
            return (false, channel.SetMode(_targetMode));
        }
        catch (OperationCanceledException)
        {
            return (true, ElevatedError.None);
        }
        catch (COMException comEx)
        {
            return (false, ElevatedErrors.FromHResult(comEx.HResult));
        }
        catch
        {
            return (false, ElevatedError.Unknown);
        }
    }

    private static string DescribeError(ElevatedError error) => error switch
    {
        ElevatedError.AccessDenied =>
            "Elevation is required to change the protection mode.",
        ElevatedError.PipeUnavailable =>
            "The protection service cannot be reached right now.",
        ElevatedError.ServerUntrusted =>
            "The control channel could not be verified as genuine. This action was refused.",
        ElevatedError.VersionMismatch =>
            "The app and the protection components are different versions.",
        ElevatedError.Transport =>
            "The control channel failed while changing the mode.",
        _ =>
            "The protection mode could not be changed right now.",
    };
}
