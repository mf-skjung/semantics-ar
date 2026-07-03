using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class NativeEventReader : IEventReader
{
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
}
