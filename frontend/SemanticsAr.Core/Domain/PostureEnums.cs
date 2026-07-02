namespace SemanticsAr.Core.Domain;

public enum PostureLevel
{
    Green,
    Amber,
    Red,
}

public enum PostureMode
{
    Unknown,
    Audit,
    Enforce,
}

public enum PostureReason
{
    Protected,
    AuditMode,
    DriverDisconnected,
    ServiceNotRunning,
    ServiceUnreachable,
    ServerUntrusted,
    VersionMismatch,
    AccessDenied,
}
