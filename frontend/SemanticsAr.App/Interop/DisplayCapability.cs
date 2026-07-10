using System.Runtime.InteropServices;
using System.Windows.Media;

namespace SemanticsAr.App.Interop;

internal static class DisplayCapability
{
    private const int MicaMinimumBuild = 22000;

    [DllImport("dwmapi.dll", PreserveSig = false)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DwmIsCompositionEnabled();

    private static bool CompositionEnabled()
    {
        try { return DwmIsCompositionEnabled(); }
        catch (DllNotFoundException) { return false; }
        catch (EntryPointNotFoundException) { return false; }
    }

    internal static bool HardwareComposited => (RenderCapability.Tier >> 16) > 0 && CompositionEnabled();

    internal static bool SupportsMica => HardwareComposited && Environment.OSVersion.Version.Build >= MicaMinimumBuild;
}
