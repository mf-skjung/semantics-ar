using System.Reflection;
using System.Runtime.InteropServices;
using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class NativeEventReader : IEventReader
{
    static NativeEventReader()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    }

    public SarApiResult Open(out nint handle)
    {
        return NativeMethods.sarapi_events_open(out handle);
    }

    public SarApiResult Read(nint handle, out SarApiEvent evt)
    {
        return NativeMethods.sarapi_events_read(handle, out evt);
    }

    public void Close(nint handle)
    {
        NativeMethods.sarapi_events_close(handle);
    }

    private static nint Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, "sarapi", StringComparison.Ordinal))
            return nint.Zero;

        return NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, "sarapi.dll"));
    }
}
