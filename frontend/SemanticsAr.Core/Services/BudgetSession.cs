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

    public IReadOnlyList<RecoverableItem> Preserved { get; private set; } = Array.Empty<RecoverableItem>();

    public IReadOnlyList<AppIdentity> Identities { get; private set; } = Array.Empty<AppIdentity>();

    public ElevatedError LastError { get; private set; }

    public void Begin()
    {
        if (State is not (BudgetSessionState.Idle or BudgetSessionState.Unavailable))
            return;

        State = BudgetSessionState.Elevating;
        LastError = ElevatedError.None;
        Preserved = Array.Empty<RecoverableItem>();
        Identities = Array.Empty<AppIdentity>();

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
            ElevatedError preservedErr = channel.LoadPreserved(out IReadOnlyList<RecoverableItem> preserved);
            if (preservedErr != ElevatedError.None)
            {
                Fail(preservedErr);
                return;
            }

            ElevatedError identityErr = channel.LoadAppIdentities(out IReadOnlyList<AppIdentity> identities);
            if (identityErr != ElevatedError.None)
            {
                Fail(identityErr);
                return;
            }

            Preserved = preserved;
            Identities = identities;
        }

        State = BudgetSessionState.Loaded;
    }

    public BudgetAttribution Compute(BudgetRange range, DateTimeOffset now) =>
        BudgetAttribution.Build(Preserved, Identities, range, now);

    public void Close()
    {
        Preserved = Array.Empty<RecoverableItem>();
        Identities = Array.Empty<AppIdentity>();
        LastError = ElevatedError.None;
        State = BudgetSessionState.Idle;
    }

    private void Fail(ElevatedError err)
    {
        LastError = err;
        State = BudgetSessionState.Unavailable;
    }
}
