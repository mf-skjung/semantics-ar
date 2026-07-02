using System.Reflection;
using System.Runtime.InteropServices;
using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class NativePostureReader : IPostureReader
{
    static NativePostureReader()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    }

    public static bool IsAbiCompatible()
    {
        return NativeMethods.sarapi_abi_version() == NativeMethods.ExpectedAbiVersion;
    }

    public SarApiResult Read(out SarApiPosture posture)
    {
        return NativeMethods.sarapi_posture_read(out posture);
    }

    private static nint Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, "sarapi", StringComparison.Ordinal))
            return nint.Zero;

        return NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, "sarapi.dll"));
    }
}
