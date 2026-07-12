using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Domain;

public static class PostureEvaluator
{
    public static PostureVerdict Evaluate(SarApiResult result, in SarApiPosture posture, bool auditAcknowledged)
    {
        return result switch
        {
            SarApiResult.Ok => EvaluateFrame(in posture, auditAcknowledged),
            SarApiResult.ServerUntrusted => Error(PostureLevel.Red, PostureReason.ServerUntrusted),
            SarApiResult.VersionMismatch => Error(PostureLevel.Amber, PostureReason.VersionMismatch),
            SarApiResult.AccessDenied => Error(PostureLevel.Red, PostureReason.AccessDenied),
            _ => Error(PostureLevel.Red, PostureReason.ServiceUnreachable),
        };
    }

    private static PostureVerdict Error(PostureLevel level, PostureReason reason)
    {
        return new PostureVerdict(level, reason, PostureMode.Unknown, false, 0, false,
            PostureDescent.None, PreserveHealth.Unknown, PreserveExpiry.None);
    }

    private static PostureVerdict EvaluateFrame(in SarApiPosture p, bool auditAcknowledged)
    {
        PostureMode mode = p.Mode switch
        {
            0u => PostureMode.Audit,
            1u => PostureMode.Enforce,
            _ => PostureMode.Unknown,
        };

        PostureDescent descents = (PostureDescent)p.Descents;
        PreserveHealth health = (PreserveHealth)p.PreserveHealth;
        PreserveExpiry expiry = (PreserveExpiry)p.OldestExpiryBucket;

        if (p.IntegrityHalt != 0)
            return new PostureVerdict(PostureLevel.Red, PostureReason.IntegrityHalt,
                mode, true, p.CapturedKeyCount, false, descents, health, expiry, IntegrityHalt: true);

        if (p.ServiceRunning == 0)
            return new PostureVerdict(PostureLevel.Red, PostureReason.ServiceNotRunning,
                mode, true, p.CapturedKeyCount, false, descents, health, expiry);

        if (p.DriverConnected == 0)
            return new PostureVerdict(PostureLevel.Red, PostureReason.DriverDisconnected,
                mode, true, p.CapturedKeyCount, false, descents, health, expiry);

        if (mode == PostureMode.Enforce)
            return new PostureVerdict(PostureLevel.Green, PostureReason.Protected,
                mode, true, p.CapturedKeyCount, false, descents, health, expiry);

        if (mode == PostureMode.Audit)
            return auditAcknowledged
                ? new PostureVerdict(PostureLevel.Green, PostureReason.AuditAcknowledged,
                    mode, true, p.CapturedKeyCount, false, descents, health, expiry)
                : new PostureVerdict(PostureLevel.Amber, PostureReason.AuditMode,
                    mode, true, p.CapturedKeyCount, false, descents, health, expiry);

        return new PostureVerdict(PostureLevel.Amber, PostureReason.VersionMismatch,
            mode, false, p.CapturedKeyCount, false, descents, health, expiry);
    }
}
