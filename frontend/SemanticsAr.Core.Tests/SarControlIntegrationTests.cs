using System.IO.Pipes;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using SemanticsAr.Core.Interop;
using Xunit;

namespace SemanticsAr.Core.Tests;

internal static partial class ControlNative
{
    [LibraryImport("sarapi")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial SarApiResult sarapi_set_mode(uint mode, out int result);
}

public sealed class SarControlIntegrationTests
{
    [Fact]
    public void SetMode_NoServer_ReturnsPipeUnavailable()
    {
        SarApiResult result = ControlNative.sarapi_set_mode(0, out _);
        Assert.Equal(SarApiResult.PipeUnavailable, result);
    }

    [Fact]
    public void SetMode_NonSystemServer_ReturnsServerUntrusted()
    {
        using NamedPipeServerStream server = new(
            "SemanticsArControl", PipeDirection.InOut, 1, PipeTransmissionMode.Byte);

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

        SarApiResult result = ControlNative.sarapi_set_mode(0, out _);

        Assert.Equal(SarApiResult.ServerUntrusted, result);
    }
}
