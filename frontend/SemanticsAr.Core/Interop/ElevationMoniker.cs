using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace SemanticsAr.Core.Interop;

internal static class ElevationMoniker
{
    private const string ClsidElevatedControl = "B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A12";
    private const int E_ABORT = unchecked((int)0x80004004);
    private const int ERROR_CANCELLED_HR = unchecked((int)0x800704C7);

    private static readonly Guid IID_IUnknown = new("00000000-0000-0000-C000-000000000046");
    private static readonly StrategyBasedComWrappers Wrappers = new();

    public static ISarElevatedControl Activate(nint ownerHwnd)
    {
        BindOpts3 opts = default;
        opts.cbStruct = NativeMethods.BindOpts3Size;
        opts.dwClassContext = NativeMethods.CLSCTX_LOCAL_SERVER;
        opts.hwnd = ownerHwnd;

        string moniker = $"Elevation:Administrator!new:{{{ClsidElevatedControl}}}";
        Guid iid = IID_IUnknown;

        int hr = NativeMethods.CoGetObject(moniker, ref opts, in iid, out nint punk);
        if (hr < 0)
        {
            if (hr == ERROR_CANCELLED_HR || hr == E_ABORT)
                throw new OperationCanceledException();
            Marshal.ThrowExceptionForHR(hr);
        }

        try
        {
            object rcw = Wrappers.GetOrCreateObjectForComInstance(punk, CreateObjectFlags.None);
            return (ISarElevatedControl)rcw;
        }
        finally
        {
            Marshal.Release(punk);
        }
    }
}
