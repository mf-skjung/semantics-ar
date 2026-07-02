namespace SemanticsAr.Core.Interop;

public enum SarApiResult
{
    Ok = 0,
    InvalidArg = 1,
    PipeUnavailable = 2,
    AccessDenied = 3,
    ServerUntrusted = 4,
    VersionMismatch = 5,
    TransportError = 6,
}
