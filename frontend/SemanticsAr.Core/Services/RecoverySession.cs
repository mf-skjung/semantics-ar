using System.Runtime.InteropServices;
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

        if (!TryOpen(RecoverySessionState.Idle, out IElevatedControlChannel channel))
            return;

        try
        {
            using (channel)
            {
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
            }
        }
        catch (OperationCanceledException)
        {
            State = RecoverySessionState.Idle;
            return;
        }
        catch (COMException comEx)
        {
            Fail(ElevatedErrors.FromHResult(comEx.HResult));
            return;
        }
        catch
        {
            Fail(ElevatedError.Unknown);
            return;
        }

        State = RecoverySessionState.Browsing;
    }

    public void Execute(IReadOnlyList<RestoreRequest> plan)
    {
        if (State != RecoverySessionState.Browsing)
            return;

        State = RecoverySessionState.Elevating;

        if (!TryOpen(RecoverySessionState.Browsing, out IElevatedControlChannel channel))
            return;

        List<RecoveryOutcome> outcomes = new(plan.Count);
        using (channel)
        {
            State = RecoverySessionState.Executing;
            foreach (RestoreRequest request in plan)
                outcomes.Add(channel.Recover(request.Item, request.TargetPath));
        }

        Report = outcomes;
        State = RecoverySessionState.Reported;
    }

    public void Close()
    {
        Items = Array.Empty<RecoverableItem>();
        Report = Array.Empty<RecoveryOutcome>();
        LastError = ElevatedError.None;
        State = RecoverySessionState.Idle;
    }

    private bool TryOpen(RecoverySessionState declinedState, out IElevatedControlChannel channel)
    {
        channel = null!;
        try
        {
            channel = _open();
            return true;
        }
        catch (OperationCanceledException)
        {
            State = declinedState;
            return false;
        }
        catch (System.Runtime.InteropServices.COMException comEx)
        {
            Fail(ElevatedErrors.FromHResult(comEx.HResult));
            return false;
        }
        catch
        {
            Fail(ElevatedError.Unknown);
            return false;
        }
    }

    private void Fail(ElevatedError err)
    {
        LastError = err;
        State = RecoverySessionState.Unavailable;
    }
}
