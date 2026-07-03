using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public enum RecoverySessionState
{
    Idle,
    Elevating,
    Browsing,
    Executing,
    Reported,
    Unavailable,
}

public sealed class RecoverySession
{
    private readonly Func<IElevatedControlChannel> _open;
    private IElevatedControlChannel? _channel;

    public RecoverySession(Func<IElevatedControlChannel> open)
    {
        _open = open;
    }

    public RecoverySessionState State { get; private set; } = RecoverySessionState.Idle;

    public IReadOnlyList<RecoverableItem> Items { get; private set; } = Array.Empty<RecoverableItem>();

    public IReadOnlyList<RecoveryOutcome> Report { get; private set; } = Array.Empty<RecoveryOutcome>();

    public ElevatedError LastError { get; private set; }

    public void Begin()
    {
        if (State is not (RecoverySessionState.Idle or RecoverySessionState.Unavailable))
            return;

        State = RecoverySessionState.Elevating;
        LastError = ElevatedError.None;
        Items = Array.Empty<RecoverableItem>();
        Report = Array.Empty<RecoveryOutcome>();

        IElevatedControlChannel channel;
        try
        {
            channel = _open();
        }
        catch (OperationCanceledException)
        {
            State = RecoverySessionState.Idle;
            return;
        }
        catch (System.Runtime.InteropServices.COMException comEx)
        {
            State = RecoverySessionState.Unavailable;
            LastError = ElevatedErrors.FromHResult(comEx.HResult);
            return;
        }
        catch
        {
            State = RecoverySessionState.Unavailable;
            LastError = ElevatedError.Unknown;
            return;
        }

        _channel = channel;

        ElevatedError err = channel.LoadCatalog(out IReadOnlyList<RecoverableItem> definitive);
        if (err != ElevatedError.None)
        {
            Fail(err);
            return;
        }

        err = channel.LoadPreserved(out IReadOnlyList<RecoverableItem> bounded);
        if (err != ElevatedError.None)
        {
            Fail(err);
            return;
        }

        List<RecoverableItem> all = new(definitive.Count + bounded.Count);
        all.AddRange(definitive);
        all.AddRange(bounded);
        Items = all;
        State = RecoverySessionState.Browsing;
    }

    public void Execute(IEnumerable<RecoverableItem> selection)
    {
        if (State != RecoverySessionState.Browsing || _channel is null)
            return;

        State = RecoverySessionState.Executing;
        List<RecoveryOutcome> outcomes = new();
        foreach (RecoverableItem item in selection)
            outcomes.Add(_channel.Recover(item));

        Report = outcomes;
        State = RecoverySessionState.Reported;
    }

    public void Close()
    {
        Cleanup();
        Items = Array.Empty<RecoverableItem>();
        Report = Array.Empty<RecoveryOutcome>();
        LastError = ElevatedError.None;
        State = RecoverySessionState.Idle;
    }

    private void Fail(ElevatedError err)
    {
        LastError = err;
        State = RecoverySessionState.Unavailable;
        Cleanup();
    }

    private void Cleanup()
    {
        _channel?.Dispose();
        _channel = null;
    }
}
