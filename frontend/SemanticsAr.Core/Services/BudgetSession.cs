using System.Runtime.InteropServices;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public enum BudgetSessionState
{
    Idle,
    Elevating,
    Loaded,
    Unavailable,
}

public sealed class BudgetSession
{
    private readonly Func<IElevatedControlChannel> _open;

    public BudgetSession(Func<IElevatedControlChannel> open)
    {
        _open = open;
    }

    public BudgetSessionState State { get; private set; } = BudgetSessionState.Idle;

    public BudgetSnapshot? Snapshot { get; private set; }

    public ElevatedError LastError { get; private set; }

    public void Begin()
    {
        if (State is not (BudgetSessionState.Idle or BudgetSessionState.Unavailable))
            return;

        State = BudgetSessionState.Elevating;
        LastError = ElevatedError.None;
        Snapshot = null;

        IElevatedControlChannel channel;
        try
        {
            channel = _open();
        }
        catch (OperationCanceledException)
        {
            State = BudgetSessionState.Idle;
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

        using (channel)
        {
            ElevatedError err = channel.LoadPreserved(out IReadOnlyList<RecoverableItem> preserved);
            if (err != ElevatedError.None)
            {
                Fail(err);
                return;
            }

            Snapshot = BudgetSnapshot.FromPreserve(preserved);
        }

        State = BudgetSessionState.Loaded;
    }

    public void Close()
    {
        Snapshot = null;
        LastError = ElevatedError.None;
        State = BudgetSessionState.Idle;
    }

    private void Fail(ElevatedError err)
    {
        LastError = err;
        State = BudgetSessionState.Unavailable;
    }
}
