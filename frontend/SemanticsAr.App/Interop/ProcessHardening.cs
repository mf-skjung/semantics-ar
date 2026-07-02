using System.Runtime.InteropServices;

namespace SemanticsAr.App.Interop;

internal static class ProcessHardening
{
    private const int ProcessExtensionPointDisablePolicy = 6;
    private const int ProcessImageLoadPolicy = 10;

    private const uint ExtensionPointsDisabled = 0x1;
    private const uint NoRemoteImages = 0x1;
    private const uint NoLowMandatoryLabelImages = 0x2;
    private const uint PreferSystem32Images = 0x4;

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetProcessMitigationPolicy(int policy, ref uint buffer, nuint length);

    internal static void Apply()
    {
        uint imageLoad = NoRemoteImages | NoLowMandatoryLabelImages | PreferSystem32Images;
        _ = SetProcessMitigationPolicy(ProcessImageLoadPolicy, ref imageLoad, sizeof(uint));

        uint extensionPoints = ExtensionPointsDisabled;
        _ = SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, ref extensionPoints, sizeof(uint));
    }
}
