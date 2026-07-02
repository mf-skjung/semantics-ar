using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;

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
        (string glyph, string label, string brushKey) = Rung switch
        {
            CertaintyRung.Definitive => ("✓", "Definitive", "Status.Green.Brush"),
            CertaintyRung.Bounded => ("⌛", "Bounded", "Status.Amber.Brush"),
            _ => ("∅", "Unrecoverable", "Status.Neutral.Brush"),
        };

        GlyphText.Text = glyph;
        LabelText.Text = label;
        AutomationProperties.SetName(ChipBorder, label);

        if (Application.Current?.Resources[brushKey] is Brush brush)
        {
            AccentDot.Fill = brush;
            ChipBorder.BorderBrush = brush;
        }
    }
}
