using System.Runtime.InteropServices;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public enum ExemptionSessionState
{
    Idle,
    Elevating,
    Loaded,
    Unavailable,
}

public sealed class ExemptionSession
{
    private readonly Func<IElevatedControlChannel> _open;

    public ExemptionSession(Func<IElevatedControlChannel> open)
    {
        _open = open;
    }

    public ExemptionSessionState State { get; private set; } = ExemptionSessionState.Idle;

    public IReadOnlyList<Exemption> Exemptions { get; private set; } = Array.Empty<Exemption>();

    public ElevatedError LastError { get; private set; }

    public void Begin()
    {
        if (State == ExemptionSessionState.Elevating)
            return;

        State = ExemptionSessionState.Elevating;
        LastError = ElevatedError.None;
        Exemptions = Array.Empty<Exemption>();

        IElevatedControlChannel? channel = Open(out bool cancelled, out ElevatedError openErr);
        if (cancelled)
        {
            State = ExemptionSessionState.Idle;
            return;
        }
        if (channel is null)
        {
            Fail(openErr);
            return;
        }

        using (channel)
        {
            ElevatedError err = channel.LoadExemptions(out IReadOnlyList<Exemption> items);
            if (err != ElevatedError.None)
            {
                Fail(err);
                return;
            }
            Exemptions = items;
        }

        State = ExemptionSessionState.Loaded;
    }

    public ResolvedIdentity? Resolve(string imagePath, out ElevatedError error)
    {
        error = ElevatedError.None;
        IElevatedControlChannel? channel = Open(out bool cancelled, out ElevatedError openErr);
        if (cancelled)
        {
            error = ElevatedError.AccessDenied;
            return null;
        }
        if (channel is null)
        {
            error = openErr;
            return null;
        }

        using (channel)
        {
            error = channel.ResolveIdentity(imagePath, out ResolvedIdentity? identity);
            return identity;
        }
    }

    public ExemptionAdd Add(string imagePath)
    {
        IElevatedControlChannel? channel = Open(out bool cancelled, out ElevatedError openErr);
        if (cancelled)
            return new ExemptionAdd(ExemptionAddResult.ChannelError, 0, ElevatedError.AccessDenied);
        if (channel is null)
            return new ExemptionAdd(ExemptionAddResult.ChannelError, 0, openErr);

        using (channel)
            return channel.WhitelistAdd(imagePath);
    }

    public ElevatedError Remove(string imagePath)
    {
        IElevatedControlChannel? channel = Open(out bool cancelled, out ElevatedError openErr);
        if (cancelled)
            return ElevatedError.AccessDenied;
        if (channel is null)
            return openErr;

        using (channel)
            return channel.WhitelistRemove(imagePath, out _);
    }

    public void Close()
    {
        Exemptions = Array.Empty<Exemption>();
        LastError = ElevatedError.None;
        State = ExemptionSessionState.Idle;
    }

    private IElevatedControlChannel? Open(out bool cancelled, out ElevatedError error)
    {
        cancelled = false;
        error = ElevatedError.None;
        try
        {
            return _open();
        }
        catch (OperationCanceledException)
        {
            cancelled = true;
            return null;
        }
        catch (COMException comEx)
        {
            error = ElevatedErrors.FromHResult(comEx.HResult);
            return null;
        }
        catch
        {
            error = ElevatedError.Unknown;
            return null;
        }
    }

    private void Fail(ElevatedError err)
    {
        LastError = err;
        State = ExemptionSessionState.Unavailable;
    }
}
