using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public interface IPostureReader
{
    SarApiResult Read(out SarApiPosture posture);
}
