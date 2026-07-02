using System.IO.Pipes;
using SemanticsAr.Core.Interop;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class SarApiIntegrationTests
{
    [Fact]
    public void AbiVersion_IsCompatible()
    {
        Assert.True(NativePostureReader.IsAbiCompatible());
    }

    [Fact]
    public void Read_NoServer_ReturnsPipeUnavailable()
    {
        NativePostureReader reader = new();
        SarApiResult result = reader.Read(out _);
        Assert.Equal(SarApiResult.PipeUnavailable, result);
    }

    [Fact]
    public void Read_NonSystemServer_ReturnsServerUntrusted()
    {
        using NamedPipeServerStream server = new(
            "SemanticsAr.Posture", PipeDirection.Out, 1, PipeTransmissionMode.Message);

        Task accept = Task.Run(() =>
        {
            try
            {
                server.WaitForConnection();
            }
            catch
            {
            }
        });

        NativePostureReader reader = new();
        SarApiResult result = reader.Read(out _);

        Assert.Equal(SarApiResult.ServerUntrusted, result);
    }
}
