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

[System.Flags]
public enum PostureDescent
{
    None = 0,
    NoTpm = 0x1,
    NoVbsHvci = 0x2,
    NoPpl = 0x4,
}

public enum PreserveHealth
{
    Unknown = 0,
    Healthy = 1,
    Low = 2,
    Critical = 3,
}

public enum PreserveExpiry
{
    None = 0,
    MoreThanSevenDays = 1,
    WithinSevenDays = 2,
    WithinOneDay = 3,
}

public enum PostureReason
{
    Protected,
    AuditMode,
    AuditAcknowledged,
    DriverDisconnected,
    ServiceNotRunning,
    ServiceUnreachable,
    ServerUntrusted,
    VersionMismatch,
    AccessDenied,
}
