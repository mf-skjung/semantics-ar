using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace SemanticsAr.Core.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct BindOpts3
{
    public uint cbStruct;
    public uint grfFlags;
    public uint grfMode;
    public uint dwTickCountDeadline;
    public uint dwTrackFlags;
    public uint dwClassContext;
    public uint locale;
    public nint pServerInfo;
    public nint hwnd;
}

internal static partial class NativeMethods
{
    internal const uint CLSCTX_LOCAL_SERVER = 0x4u;
    internal const ushort VT_UI1 = 17;

    [LibraryImport("ole32", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CoGetObject(string pszName, ref BindOpts3 pBindOptions,
                                            in Guid riid, out nint ppv);

    [LibraryImport("ole32")]
    internal static partial int CoSetProxyBlanket(nint pProxy, uint dwAuthnSvc, uint dwAuthzSvc,
                                                  nint pServerPrincName, uint dwAuthnLevel,
                                                  uint dwImpLevel, nint pAuthInfo, uint dwCapabilities);

    [LibraryImport("oleaut32")]
    internal static partial nint SafeArrayCreateVector(ushort vt, int lLbound, uint cElements);

    [LibraryImport("oleaut32")]
    internal static partial int SafeArrayDestroy(nint psa);

    [LibraryImport("oleaut32")]
    internal static partial int SafeArrayAccessData(nint psa, out nint ppvData);

    [LibraryImport("oleaut32")]
    internal static partial int SafeArrayUnaccessData(nint psa);

    [LibraryImport("oleaut32")]
    internal static partial int SafeArrayGetLBound(nint psa, uint nDim, out int lbound);

    [LibraryImport("oleaut32")]
    internal static partial int SafeArrayGetUBound(nint psa, uint nDim, out int ubound);

    internal static uint BindOpts3Size => (uint)Unsafe.SizeOf<BindOpts3>();
}
