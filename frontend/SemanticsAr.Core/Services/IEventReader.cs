using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public interface IEventReader
{
    SarApiResult Open(out nint handle);

    SarApiResult Read(nint handle, out SarApiEvent evt);

    void Close(nint handle);
}
