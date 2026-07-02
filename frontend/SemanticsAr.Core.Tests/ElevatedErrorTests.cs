using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class ElevatedErrorTests
{
    [Theory]
    [InlineData(0, ElevatedError.None)]
    [InlineData(unchecked((int)0x80070005), ElevatedError.AccessDenied)]
    [InlineData(unchecked((int)0x80040201), ElevatedError.PipeUnavailable)]
    [InlineData(unchecked((int)0x80040202), ElevatedError.ServerUntrusted)]
    [InlineData(unchecked((int)0x80040203), ElevatedError.VersionMismatch)]
    [InlineData(unchecked((int)0x80040204), ElevatedError.Transport)]
    [InlineData(unchecked((int)0x80004005), ElevatedError.Unknown)]
    [InlineData(1, ElevatedError.None)]
    public void FromHResult_Maps(int hr, ElevatedError expected)
    {
        Assert.Equal(expected, ElevatedErrors.FromHResult(hr));
    }
}
