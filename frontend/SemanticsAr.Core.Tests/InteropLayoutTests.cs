using System.Runtime.CompilerServices;
using SemanticsAr.Core.Interop;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class InteropLayoutTests
{
    [Fact]
    public void SarApiPosture_MatchesNativeLayout()
    {
        Assert.Equal(40, Unsafe.SizeOf<SarApiPosture>());
    }

    [Fact]
    public void SarApiEvent_MatchesNativeLayout()
    {
        Assert.Equal(48, Unsafe.SizeOf<SarApiEvent>());
    }
}
