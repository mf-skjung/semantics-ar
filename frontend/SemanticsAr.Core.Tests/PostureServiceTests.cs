using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class PostureServiceTests
{
    private sealed class ScriptedReader : IPostureReader
    {
        private readonly Queue<(SarApiResult, SarApiPosture)> _steps = new();

        public void Enqueue(SarApiResult result, SarApiPosture posture = default)
        {
            _steps.Enqueue((result, posture));
        }

        public SarApiResult Read(out SarApiPosture posture)
        {
            (SarApiResult result, SarApiPosture frame) = _steps.Dequeue();
            posture = frame;
            return result;
        }
    }

    private static SarApiPosture Enforce()
    {
        return new SarApiPosture
        {
            ProtocolVersion = 1u,
            ServiceRunning = 1u,
            DriverConnected = 1u,
            Mode = 1u,
        };
    }

    [Fact]
    public void TransientFailure_WithinTolerance_ServesLastGoodAsStale()
    {
        ScriptedReader reader = new();
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.PipeUnavailable);

        using PostureService service = new(reader, staleTolerance: 3);

        PostureVerdict good = service.Poll();
        Assert.Equal(PostureLevel.Green, good.Level);
        Assert.False(good.IsStale);

        PostureVerdict stale = service.Poll();
        Assert.Equal(PostureLevel.Green, stale.Level);
        Assert.True(stale.IsStale);
    }

    [Fact]
    public void TransientFailure_BeyondTolerance_FlipsToRed()
    {
        ScriptedReader reader = new();
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.PipeUnavailable);
        reader.Enqueue(SarApiResult.PipeUnavailable);
        reader.Enqueue(SarApiResult.PipeUnavailable);

        using PostureService service = new(reader, staleTolerance: 3);

        service.Poll();
        Assert.True(service.Poll().IsStale);
        Assert.True(service.Poll().IsStale);

        PostureVerdict down = service.Poll();
        Assert.Equal(PostureLevel.Red, down.Level);
        Assert.Equal(PostureReason.ServiceUnreachable, down.Reason);
        Assert.False(down.IsStale);
    }

    [Fact]
    public void ServerUntrusted_FlipsImmediately_NoStale()
    {
        ScriptedReader reader = new();
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.ServerUntrusted);

        using PostureService service = new(reader, staleTolerance: 3);

        service.Poll();
        PostureVerdict untrusted = service.Poll();
        Assert.Equal(PostureLevel.Red, untrusted.Level);
        Assert.Equal(PostureReason.ServerUntrusted, untrusted.Reason);
        Assert.False(untrusted.IsStale);
    }

    [Fact]
    public void Recovery_AfterTransient_ResetsStrikes()
    {
        ScriptedReader reader = new();
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.PipeUnavailable);
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.PipeUnavailable);

        using PostureService service = new(reader, staleTolerance: 2);

        service.Poll();
        Assert.True(service.Poll().IsStale);
        Assert.False(service.Poll().IsStale);
        Assert.True(service.Poll().IsStale);
    }

    [Fact]
    public void PostureChanged_RaisedOnlyOnChange()
    {
        ScriptedReader reader = new();
        reader.Enqueue(SarApiResult.Ok, Enforce());
        reader.Enqueue(SarApiResult.Ok, Enforce());

        using PostureService service = new(reader);
        int raised = 0;
        service.PostureChanged += (_, _) => raised++;

        service.Poll();
        service.Poll();

        Assert.Equal(1, raised);
    }
}
