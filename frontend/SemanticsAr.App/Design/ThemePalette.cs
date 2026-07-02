using System.Windows;
using System.Windows.Media;
using Wpf.Ui.Appearance;

namespace SemanticsAr.App.Design;

internal static class ThemePalette
{
    private static readonly (string Key, Color Light, Color Dark)[] Tokens =
    [
        ("Status.Green.Brush", Color.FromRgb(0x0F, 0x7B, 0x0F), Color.FromRgb(0x6C, 0xCB, 0x5F)),
        ("Status.Amber.Brush", Color.FromRgb(0x9D, 0x5D, 0x00), Color.FromRgb(0xFC, 0xE1, 0x00)),
        ("Status.Red.Brush", Color.FromRgb(0xC4, 0x2B, 0x1C), Color.FromRgb(0xFF, 0x99, 0xA4)),
        ("Status.Neutral.Brush", Color.FromRgb(0x61, 0x61, 0x61), Color.FromRgb(0x9A, 0x9A, 0x9A)),
    ];

    internal static void Install()
    {
        Apply();
        ApplicationThemeManager.Changed += (_, _) => Apply();
    }

    private static void Apply()
    {
        bool highContrast = SystemParameters.HighContrast;
        bool dark = ApplicationThemeManager.GetAppTheme() == ApplicationTheme.Dark;
        ResourceDictionary res = Application.Current.Resources;

        foreach ((string key, Color light, Color darkColor) in Tokens)
        {
            Color color = highContrast
                ? SystemColors.WindowTextColor
                : (dark ? darkColor : light);
            res[key] = new SolidColorBrush(color);
        }
    }
}
