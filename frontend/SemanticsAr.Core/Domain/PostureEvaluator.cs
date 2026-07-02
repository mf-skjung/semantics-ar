using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Domain;

public static class PostureEvaluator
{
    public static PostureVerdict Evaluate(SarApiResult result, in SarApiPosture posture)
    {
        return result switch
        {
            SarApiResult.Ok => EvaluateFrame(in posture),
            SarApiResult.ServerUntrusted => new PostureVerdict(
                PostureLevel.Red, PostureReason.ServerUntrusted,
                PostureMode.Unknown, false, 0, false),
            SarApiResult.VersionMismatch => new PostureVerdict(
                PostureLevel.Amber, PostureReason.VersionMismatch,
                PostureMode.Unknown, false, 0, false),
            SarApiResult.AccessDenied => new PostureVerdict(
                PostureLevel.Red, PostureReason.AccessDenied,
                PostureMode.Unknown, false, 0, false),
            _ => new PostureVerdict(
                PostureLevel.Red, PostureReason.ServiceUnreachable,
                PostureMode.Unknown, false, 0, false),
        };
    }

    private static PostureVerdict EvaluateFrame(in SarApiPosture p)
    {
        PostureMode mode = p.Mode switch
        {
            0u => PostureMode.Audit,
            1u => PostureMode.Enforce,
            _ => PostureMode.Unknown,
        };

        if (p.ServiceRunning == 0)
            return new PostureVerdict(PostureLevel.Red, PostureReason.ServiceNotRunning,
                mode, true, p.CapturedKeyCount, false);

        if (p.DriverConnected == 0)
            return new PostureVerdict(PostureLevel.Red, PostureReason.DriverDisconnected,
                mode, true, p.CapturedKeyCount, false);

        if (mode == PostureMode.Enforce)
            return new PostureVerdict(PostureLevel.Green, PostureReason.Protected,
                mode, true, p.CapturedKeyCount, false);

        if (mode == PostureMode.Audit)
            return new PostureVerdict(PostureLevel.Amber, PostureReason.AuditMode,
                mode, true, p.CapturedKeyCount, false);

        return new PostureVerdict(PostureLevel.Amber, PostureReason.VersionMismatch,
            mode, false, p.CapturedKeyCount, false);
    }
}
