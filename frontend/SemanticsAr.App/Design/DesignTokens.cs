using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using SemanticsAr.App.Controls;
using Wpf.Ui.Appearance;

namespace SemanticsAr.App.Design;

internal sealed record RungToken(string Label, string Gloss, string BrushKey);

internal static class DesignTokens
{
    private const string ResourceName = "SemanticsAr.App.ladder.tokens.json";

    private static readonly Dictionary<string, string> PostureBrushKeys = new()
    {
        ["green"] = "Status.Green.Brush",
        ["amber"] = "Status.Amber.Brush",
        ["red"] = "Status.Red.Brush",
        ["neutral"] = "Status.Neutral.Brush",
    };

    private static readonly Dictionary<CertaintyRung, string> RungKeys = new()
    {
        [CertaintyRung.Definitive] = "definitive",
        [CertaintyRung.Bounded] = "bounded",
        [CertaintyRung.Unrecoverable] = "unrecoverable",
    };

    private static readonly TokensDto Tokens = Load();

    internal static void Install()
    {
        Apply();
        ApplicationThemeManager.Changed += (_, _) => Apply();
    }

    internal static RungToken Rung(CertaintyRung rung)
    {
        RungDto dto = Tokens.Ladder[RungKeys[rung]];
        return new RungToken(dto.Label, dto.Gloss, LadderBrushKey(rung));
    }

    internal static string LadderBrushKey(CertaintyRung rung) => $"Ladder.{RungKeys[rung]}.Brush";

    private static void Apply()
    {
        bool highContrast = SystemParameters.HighContrast;
        bool dark = ApplicationThemeManager.GetAppTheme() == ApplicationTheme.Dark;
        ResourceDictionary res = Application.Current.Resources;

        foreach ((string token, string key) in PostureBrushKeys)
            res[key] = ToBrush(Tokens.Posture[token], dark, highContrast);

        foreach ((CertaintyRung rung, string token) in RungKeys)
            res[LadderBrushKey(rung)] = ToBrush(Tokens.Ladder[token].Accent, dark, highContrast);
    }

    private static SolidColorBrush ToBrush(ThemedColorDto themed, bool dark, bool highContrast)
    {
        Color color = highContrast
            ? SystemColors.WindowTextColor
            : (Color)ColorConverter.ConvertFromString(dark ? themed.Dark : themed.Light);
        SolidColorBrush brush = new(color);
        brush.Freeze();
        return brush;
    }

    private static TokensDto Load()
    {
        using Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"Embedded design tokens '{ResourceName}' were not found.");
        return JsonSerializer.Deserialize<TokensDto>(stream, SerializerOptions)
            ?? throw new InvalidOperationException("Design tokens deserialized to null.");
    }

    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private sealed class TokensDto
    {
        public Dictionary<string, ThemedColorDto> Posture { get; init; } = new();

        public Dictionary<string, RungDto> Ladder { get; init; } = new();
    }

    private sealed class ThemedColorDto
    {
        public string Light { get; init; } = string.Empty;

        public string Dark { get; init; } = string.Empty;
    }

    private sealed class RungDto
    {
        public string Label { get; init; } = string.Empty;

        public string Gloss { get; init; } = string.Empty;

        public ThemedColorDto Accent { get; init; } = new();
    }
}
