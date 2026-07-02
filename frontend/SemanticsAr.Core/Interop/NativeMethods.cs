using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[assembly: DisableRuntimeMarshalling]

namespace SemanticsAr.Core.Interop;

internal static partial class NativeMethods
{
    internal const uint ExpectedAbiVersion = 1u;

    private const string Library = "sarapi";

    [LibraryImport(Library)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint sarapi_abi_version();

    [LibraryImport(Library)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial SarApiResult sarapi_posture_read(out SarApiPosture posture);

    [LibraryImport(Library)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial SarApiResult sarapi_events_open(out nint handle);

    [LibraryImport(Library)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial SarApiResult sarapi_events_read(nint handle, out SarApiEvent evt);

    [LibraryImport(Library)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void sarapi_events_close(nint handle);
}
