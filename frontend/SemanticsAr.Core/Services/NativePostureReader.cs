using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class NativePostureReader : IPostureReader
{
    public static bool IsAbiCompatible()
    {
        return NativeMethods.sarapi_abi_version() == NativeMethods.ExpectedAbiVersion;
    }

    public SarApiResult Read(out SarApiPosture posture)
    {
        return NativeMethods.sarapi_posture_read(out posture);
    }
}
