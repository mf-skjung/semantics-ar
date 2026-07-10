using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class ElevatedControlChannel : IElevatedControlChannel
{
    private ISarElevatedControl? _control;

    private ElevatedControlChannel(ISarElevatedControl control)
    {
        _control = control;
    }

    public static ElevatedControlChannel Activate(nint ownerHwnd)
    {
        return new ElevatedControlChannel(ElevationMoniker.Activate(ownerHwnd));
    }

    public ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items)
    {
        return LoadPaged(catalog: true, out items);
    }

    public ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items)
    {
        return LoadPaged(catalog: false, out items);
    }

    public ElevatedError LoadAppIdentities(out IReadOnlyList<AppIdentity> items)
    {
        items = Array.Empty<AppIdentity>();
        ISarElevatedControl control = Control();
        List<AppIdentity> all = new();
        uint start = 0;

        while (true)
        {
            int hr = control.AppIdentityPage(start, out uint total, out uint returned, out nint blob);

            ElevatedError err = ElevatedErrors.FromHResult(hr);
            if (err != ElevatedError.None)
                return err;

            try
            {
                if (returned > 0)
                {
                    byte[] bytes = SafeArrayNative.CopyBytes(blob);
                    all.AddRange(RecoveryLadder.ParseAppIdentities(bytes, (int)returned));
                }
            }
            finally
            {
                SafeArrayNative.Destroy(blob);
            }

            start += returned;
            if (returned == 0 || start >= total)
                break;
        }

        items = all;
        return ElevatedError.None;
    }

    public RecoveryOutcome Recover(RecoverableItem item, string targetPath)
    {
        ISarElevatedControl control = Control();
        int hr;
        int result;

        if (item.Rung == CertaintyRung.Definitive)
        {
            nint key = SafeArrayNative.FromBytes(item.KeyId ?? Array.Empty<byte>());
            try
            {
                hr = control.Recover(key, targetPath, out result);
            }
            finally
            {
                SafeArrayNative.Destroy(key);
            }
        }
        else
        {
            hr = control.PreserveRecover(targetPath, item.Offset, item.Length, out result);
        }

        ElevatedError err = ElevatedErrors.FromHResult(hr);
        if (err != ElevatedError.None)
            return new RecoveryOutcome(item, RecoveryOutcomeKind.ChannelError, 0, err) { TargetPath = targetPath };
        return RecoveryLadder.MapResult(item, result) with { TargetPath = targetPath };
    }

    public ElevatedError SetMode(uint mode)
    {
        return ElevatedErrors.FromHResult(Control().SetMode(mode, out _));
    }

    public ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes)
    {
        return ElevatedErrors.FromHResult(Control().SetBudget(retention100ns, capacityBytes, out _));
    }

    public void Dispose()
    {
        ISarElevatedControl? control = _control;
        _control = null;
        if (control is null)
            return;
        try
        {
            control.Shutdown();
        }
        catch
        {
        }
    }

    private ElevatedError LoadPaged(bool catalog, out IReadOnlyList<RecoverableItem> items)
    {
        items = Array.Empty<RecoverableItem>();
        ISarElevatedControl control = Control();
        List<RecoverableItem> all = new();
        uint start = 0;

        while (true)
        {
            int hr = catalog
                ? control.CatalogPage(start, out uint total, out uint returned, out nint blob)
                : control.PreservePage(start, out total, out returned, out blob);

            ElevatedError err = ElevatedErrors.FromHResult(hr);
            if (err != ElevatedError.None)
                return err;

            try
            {
                if (returned > 0)
                {
                    byte[] bytes = SafeArrayNative.CopyBytes(blob);
                    all.AddRange(catalog
                        ? RecoveryLadder.ParseCatalog(bytes, (int)returned)
                        : RecoveryLadder.ParsePreserve(bytes, (int)returned));
                }
            }
            finally
            {
                SafeArrayNative.Destroy(blob);
            }

            start += returned;
            if (returned == 0 || start >= total)
                break;
        }

        items = all;
        return ElevatedError.None;
    }

    private ISarElevatedControl Control()
    {
        return _control ?? throw new ObjectDisposedException(nameof(ElevatedControlChannel));
    }
}
