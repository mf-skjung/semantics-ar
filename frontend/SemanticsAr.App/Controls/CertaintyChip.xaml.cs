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

        RungToken token = DesignTokens.Rung(Rung);
        GlyphText.Text = glyph;
        LabelText.Text = token.Label;
        AutomationProperties.SetName(ChipBorder, token.Label);

        AccentDot.SetResourceReference(Shape.FillProperty, token.BrushKey);
        ChipBorder.SetResourceReference(Border.BorderBrushProperty, token.BrushKey);
    }
}
