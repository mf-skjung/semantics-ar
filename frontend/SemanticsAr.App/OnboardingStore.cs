using Microsoft.Win32;

namespace SemanticsAr.App;

internal static class OnboardingStore
{
    private const string KeyPath = @"Software\MetaForensics\SemanticsAr";
    private const string ValueName = "OnboardingCompleted";

    public static bool IsCompleted()
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(KeyPath);
        return key?.GetValue(ValueName) is int value && value != 0;
    }

    public static void MarkCompleted()
    {
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(KeyPath);
        key.SetValue(ValueName, 1, RegistryValueKind.DWord);
    }
}
