using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
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
        (string bg, string line) = Rung switch
        {
            CertaintyRung.Definitive => ("Sar.DefBgBrush", "Sar.DefLineBrush"),
            CertaintyRung.Bounded => ("Sar.BndBgBrush", "Sar.BndLineBrush"),
            _ => ("Sar.UnrBgBrush", "Sar.UnrLineBrush"),
        };

        RungToken token = DesignTokens.Rung(Rung);
        Glyph.Rung = Rung;
        LabelText.Text = token.Label;
        AutomationProperties.SetName(ChipBorder, token.Label);

        LabelText.SetResourceReference(TextBlock.ForegroundProperty, token.BrushKey);
        ChipBorder.SetResourceReference(Border.BackgroundProperty, bg);
        ChipBorder.SetResourceReference(Border.BorderBrushProperty, line);
    }
}
