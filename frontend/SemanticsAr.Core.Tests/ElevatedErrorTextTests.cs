using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class ElevatedErrorTextTests
{
    [Fact]
    public void AccessDenied_WeavesTheAction()
    {
        Assert.Equal(
            "Elevation is required to change the protection mode.",
            ElevatedErrorText.Describe(ElevatedError.AccessDenied, "change the protection mode"));
    }

    [Fact]
    public void Transport_WeavesTheAction()
    {
        Assert.Equal(
            "The control channel failed while trying to read your recovery budget.",
            ElevatedErrorText.Describe(ElevatedError.Transport, "read your recovery budget"));
    }

    [Fact]
    public void ServerUntrusted_IsActionIndependentAndRefuses()
    {
        Assert.Equal(
            "The control channel could not be verified as genuine. This action was refused.",
            ElevatedErrorText.Describe(ElevatedError.ServerUntrusted, "anything"));
    }

    [Fact]
    public void Unknown_FallsBackWithTheAction()
    {
        Assert.Equal(
            "Could not view and restore recoverable files right now.",
            ElevatedErrorText.Describe(ElevatedError.Unknown, "view and restore recoverable files"));
    }

    [Theory]
    [InlineData(ElevatedError.None)]
    [InlineData(ElevatedError.AccessDenied)]
    [InlineData(ElevatedError.PipeUnavailable)]
    [InlineData(ElevatedError.ServerUntrusted)]
    [InlineData(ElevatedError.VersionMismatch)]
    [InlineData(ElevatedError.Transport)]
    [InlineData(ElevatedError.Unknown)]
    public void EveryError_ProducesNonEmptyText(ElevatedError error)
    {
        Assert.False(string.IsNullOrWhiteSpace(ElevatedErrorText.Describe(error, "do the thing")));
    }
}
