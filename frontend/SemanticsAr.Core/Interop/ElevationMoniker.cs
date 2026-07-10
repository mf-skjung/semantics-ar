using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace SemanticsAr.Core.Interop;

internal static class ElevationMoniker
{
    private const string ClsidElevatedControl = "B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12";
    private const int E_ABORT = unchecked((int)0x80004004);
    private const int ERROR_CANCELLED_HR = unchecked((int)0x800704C7);

    private const uint RPC_C_AUTHN_DEFAULT = 0xFFFFFFFFu;
    private const uint RPC_C_AUTHZ_DEFAULT = 0xFFFFFFFFu;
    private const uint RPC_C_AUTHN_LEVEL_PKT_PRIVACY = 6u;
    private const uint RPC_C_IMP_LEVEL_IDENTIFY = 2u;
    private const uint EOAC_NONE = 0u;

    private static readonly Guid IID_ISarElevatedControl = new("B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A11");
    private static readonly StrategyBasedComWrappers Wrappers = new();

    public static ISarElevatedControl Activate(nint ownerHwnd)
    {
        BindOpts3 opts = default;
        opts.cbStruct = NativeMethods.BindOpts3Size;
        opts.dwClassContext = NativeMethods.CLSCTX_LOCAL_SERVER;
        opts.hwnd = ownerHwnd;

        string moniker = $"Elevation:Administrator!new:{{{ClsidElevatedControl}}}";
        Guid iid = IID_ISarElevatedControl;

        int hr = NativeMethods.CoGetObject(moniker, ref opts, in iid, out nint proxy);
        if (hr < 0)
        {
            if (hr == ERROR_CANCELLED_HR || hr == E_ABORT)
                throw new OperationCanceledException();
            Marshal.ThrowExceptionForHR(hr);
        }

        try
        {
            int sb = NativeMethods.CoSetProxyBlanket(proxy,
                                                     RPC_C_AUTHN_DEFAULT, RPC_C_AUTHZ_DEFAULT, nint.Zero,
                                                     RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY,
                                                     nint.Zero, EOAC_NONE);
            if (sb < 0)
                Marshal.ThrowExceptionForHR(sb);

            object rcw = Wrappers.GetOrCreateObjectForComInstance(proxy, CreateObjectFlags.None);
            return (ISarElevatedControl)rcw;
        }
        finally
        {
            Marshal.Release(proxy);
        }
    }
}
