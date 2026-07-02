using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class PostureEvaluatorTests
{
    private static SarApiPosture Frame(uint service, uint driver, uint mode, ulong count = 0)
    {
        return new SarApiPosture
        {
            ProtocolVersion = 1u,
            ServiceRunning = service,
            DriverConnected = driver,
            Mode = mode,
            CapturedKeyCount = count,
        };
    }

    [Fact]
    public void EnforceWithDriver_IsGreenProtected()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 1, 42));
        Assert.Equal(PostureLevel.Green, v.Level);
        Assert.Equal(PostureReason.Protected, v.Reason);
        Assert.Equal(PostureMode.Enforce, v.Mode);
        Assert.True(v.ManagementAvailable);
        Assert.Equal(42ul, v.CapturedKeyCount);
        Assert.False(v.IsStale);
    }

    [Fact]
    public void AuditWithDriver_IsAmberAuditMode()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 0));
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.AuditMode, v.Reason);
        Assert.Equal(PostureMode.Audit, v.Mode);
    }

    [Fact]
    public void DriverDisconnected_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 0, 1));
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.DriverDisconnected, v.Reason);
    }

    [Fact]
    public void ServiceNotRunning_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(0, 0, 0));
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServiceNotRunning, v.Reason);
    }

    [Fact]
    public void UnknownMode_WithDriver_IsAmberVersionMismatch()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.Ok, Frame(1, 1, 7));
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.VersionMismatch, v.Reason);
        Assert.False(v.ManagementAvailable);
    }

    [Fact]
    public void ServerUntrusted_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.ServerUntrusted, default);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServerUntrusted, v.Reason);
        Assert.False(v.ManagementAvailable);
    }

    [Fact]
    public void VersionMismatch_IsAmber()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.VersionMismatch, default);
        Assert.Equal(PostureLevel.Amber, v.Level);
        Assert.Equal(PostureReason.VersionMismatch, v.Reason);
    }

    [Fact]
    public void AccessDenied_IsRed()
    {
        PostureVerdict v = PostureEvaluator.Evaluate(SarApiResult.AccessDenied, default);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.AccessDenied, v.Reason);
    }

    [Theory]
    [InlineData(SarApiResult.PipeUnavailable)]
    [InlineData(SarApiResult.TransportError)]
    public void TransientFailures_AreRedUnreachable(SarApiResult result)
    {
        PostureVerdict v = PostureEvaluator.Evaluate(result, default);
        Assert.Equal(PostureLevel.Red, v.Level);
        Assert.Equal(PostureReason.ServiceUnreachable, v.Reason);
    }
}
