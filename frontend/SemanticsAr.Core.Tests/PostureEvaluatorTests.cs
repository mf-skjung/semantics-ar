using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class PostureEvaluatorTests
{
    private static SarApiPosture Frame(uint service, uint driver, uint mode,
        ulong count = 0, uint descents = 0, uint health = 0, uint expiry = 0)
    {
        return new SarApiPosture
        {
            ProtocolVersion = 1u,
            ServiceRunning = service,
            DriverConnected = driver,
            Mode = mode,
            CapturedKeyCount = count,
            Descents = descents,
            PreserveHealth = health,
            OldestExpiryBucket = expiry,
        };
    }

    [Fact]
    public void EnforceWithDriver_IsGreenProtected()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 1, 42), false);
        Assert.Equal(PostureLevel.Green, v.Level);
        Assert.Equal(PostureReason.Protected, v.Reason);
        Assert.Equal(PostureMode.Enforce, v.Mode);
        Assert.True(v.ManagementAvailable);
        Assert.Equal(42ul, v.CapturedKeyCount);
        Assert.False(v.IsStale);
    }

    [Fact]
    public void UnacknowledgedAudit_IsAmberAuditMode()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 0), false);
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.AuditMode, v.Reason);
        Assert.Equal(PostureMode.Audit, v.Mode);
    }

    [Fact]
    public void AcknowledgedAudit_CollapsesToGreenNeutral()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 0), true);
        Assert.Equal(PostureLevel.Green, v.Level);
        Assert.Equal(PostureReason.AuditAcknowledged, v.Reason);
        Assert.Equal(PostureMode.Audit, v.Mode);
    }

    [Fact]
    public void EnforceIgnoresAcknowledgement()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 1), true);
        Assert.Equal(PostureLevel.Green, v.Level);
        Assert.Equal(PostureReason.Protected, v.Reason);
    }

    [Fact]
    public void DriverDisconnected_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 0, 1), false);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.DriverDisconnected, v.Reason);
    }

    [Fact]
    public void ServiceNotRunning_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(0, 0, 0), false);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServiceNotRunning, v.Reason);
    }

    [Fact]
    public void UnknownMode_WithDriver_IsAmberVersionMismatch()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 7), false);
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.VersionMismatch, v.Reason);
        Assert.False(v.ManagementAvailable);
    }

    [Fact]
    public void Descents_ArePassedThroughWithoutChangingLevel()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok,
            Frame(1, 1, 1, 0, 0x1u | 0x4u), false);
        Assert.Equal(PostureLevel.Green, v.Level);
        Assert.Equal(PostureDescent.NoTpm | PostureDescent.NoPpl, v.Descents);
    }

    [Fact]
    public void NoDescents_IsPostureDescentNone()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 1), false);
        Assert.Equal(PostureDescent.None, v.Descents);
    }

    [Fact]
    public void PreserveHealthAndExpiry_ArePassedThrough()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok,
            Frame(1, 1, 1, 0, 0, health: 3u, expiry: 3u), false);
        Assert.Equal(PreserveHealth.Critical, v.PreserveHealth);
        Assert.Equal(PreserveExpiry.WithinOneDay, v.OldestProtectedExpiry);
    }

    [Fact]
    public void PreserveHealthUnknown_WhenStatsAbsent()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 1), false);
        Assert.Equal(PreserveHealth.Unknown, v.PreserveHealth);
        Assert.Equal(PreserveExpiry.None, v.OldestProtectedExpiry);
    }

    [Fact]
    public void ServerUntrusted_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.ServerUntrusted, default, false);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServerUntrusted, v.Reason);
        Assert.False(v.ManagementAvailable);
    }

    [Fact]
    public void VersionMismatch_IsAmber()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.VersionMismatch, default, false);
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.VersionMismatch, v.Reason);
    }

    [Fact]
    public void AccessDenied_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.AccessDenied, default, false);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.AccessDenied, v.Reason);
    }

    [Theory]
    [InlineData(SarApiResult.PipeUnavailable)]
    [InlineData(SarApiResult.TransportError)]
    public void TransientFailures_AreRedUnreachable(SarApiResult result)
    {
        PostureVerdict v = PostureEvaluator.Evaluate(result, default, false);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServiceUnreachable, v.Reason);
    }
}
