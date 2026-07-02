namespace SemanticsAr.Core.Domain;

public enum ElevatedError
{
    None,
    AccessDenied,
    PipeUnavailable,
    ServerUntrusted,
    VersionMismatch,
    Transport,
    Unknown,
}

public static class ElevatedErrors
{
    private const int E_ACCESSDENIED = unchecked((int)0x80070005);
    private const int SAR_E_PIPE_UNAVAILABLE = unchecked((int)0x80040201);
    private const int SAR_E_SERVER_UNTRUSTED = unchecked((int)0x80040202);
    private const int SAR_E_VERSION_MISMATCH = unchecked((int)0x80040203);
    private const int SAR_E_TRANSPORT = unchecked((int)0x80040204);

    public static ElevatedError FromHResult(int hr)
    {
        return hr switch
        {
            0 => ElevatedError.None,
            E_ACCESSDENIED => ElevatedError.AccessDenied,
            SAR_E_PIPE_UNAVAILABLE => ElevatedError.PipeUnavailable,
            SAR_E_SERVER_UNTRUSTED => ElevatedError.ServerUntrusted,
            SAR_E_VERSION_MISMATCH => ElevatedError.VersionMismatch,
            SAR_E_TRANSPORT => ElevatedError.Transport,
            _ => hr < 0 ? ElevatedError.Unknown : ElevatedError.None,
        };
    }
}
