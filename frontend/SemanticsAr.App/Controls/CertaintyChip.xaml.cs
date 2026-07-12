using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Shapes;
using SemanticsAr.App.Design;

namespace SemanticsAr.App.Controls;

public enum CertaintyRung
{
    Definitive,
    Bounded,
    Unrecoverable,
}

public partial class CertaintyChip : UserControl
{
    public static readonly DependencyProperty RungProperty = DependencyProperty.Register(
        nameof(Rung),
        typeof(CertaintyRung),
        typeof(CertaintyChip),
        new PropertyMetadata(CertaintyRung.Definitive, OnRungChanged));

    public CertaintyChip()
    {
        InitializeComponent();
        Update();
    }

    public CertaintyRung Rung
    {
        get => (CertaintyRung)GetValue(RungProperty);
        set => SetValue(RungProperty, value);
    }

    private static void OnRungChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((CertaintyChip)d).Update();
    }

    private void Update()
    {
        string glyph = Rung switch
        {
            CertaintyRung.Definitive => "✓",
            CertaintyRung.Bounded => "⌛",
            _ => "∅",
        };
        (string bg, string line) = Rung switch
        {
            CertaintyRung.Definitive => ("Sar.DefBgBrush", "Sar.DefLineBrush"),
            CertaintyRung.Bounded => ("Sar.BndBgBrush", "Sar.BndLineBrush"),
            _ => ("Sar.UnrBgBrush", "Sar.UnrLineBrush"),
        };

        RungToken token = DesignTokens.Rung(Rung);
        GlyphText.Text = glyph;
        LabelText.Text = token.Label;
        AutomationProperties.SetName(ChipBorder, token.Label);

        GlyphText.SetResourceReference(TextBlock.ForegroundProperty, token.BrushKey);
        LabelText.SetResourceReference(TextBlock.ForegroundProperty, token.BrushKey);
        ChipBorder.SetResourceReference(Border.BackgroundProperty, bg);

        if (Rung == CertaintyRung.Unrecoverable)
        {
            ChipBorder.BorderThickness = new Thickness(3, 1, 1, 1);
            ChipBorder.CornerRadius = new CornerRadius(4, 999, 999, 4);
            ChipBorder.SetResourceReference(Border.BorderBrushProperty, "Sar.UnrBrush");
        }
        else
        {
            ChipBorder.BorderThickness = new Thickness(1);
            ChipBorder.CornerRadius = new CornerRadius(999);
            ChipBorder.SetResourceReference(Border.BorderBrushProperty, line);
        }
    }
}
