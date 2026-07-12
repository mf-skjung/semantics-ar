using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace SemanticsAr.App.Controls;

public partial class CertaintyGlyph : UserControl
{
    public static readonly DependencyProperty RungProperty = DependencyProperty.Register(
        nameof(Rung),
        typeof(CertaintyRung),
        typeof(CertaintyGlyph),
        new PropertyMetadata(CertaintyRung.Definitive, OnVisualChanged));

    public static readonly DependencyProperty RemainingFractionProperty = DependencyProperty.Register(
        nameof(RemainingFraction),
        typeof(double?),
        typeof(CertaintyGlyph),
        new PropertyMetadata(null, OnVisualChanged));

    public static readonly DependencyProperty GlyphSizeProperty = DependencyProperty.Register(
        nameof(GlyphSize),
        typeof(double),
        typeof(CertaintyGlyph),
        new PropertyMetadata(18.0, OnVisualChanged));

    public CertaintyGlyph()
    {
        InitializeComponent();
        Update();
    }

    public CertaintyRung Rung
    {
        get => (CertaintyRung)GetValue(RungProperty);
        set => SetValue(RungProperty, value);
    }

    public double? RemainingFraction
    {
        get => (double?)GetValue(RemainingFractionProperty);
        set => SetValue(RemainingFractionProperty, value);
    }

    public double GlyphSize
    {
        get => (double)GetValue(GlyphSizeProperty);
        set => SetValue(GlyphSizeProperty, value);
    }

    private static void OnVisualChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((CertaintyGlyph)d).Update();
    }

    private static Brush Brush(string key) =>
        Application.Current?.Resources[key] as Brush ?? Brushes.Gray;

    private static Geometry Glyph(string key) =>
        (Application.Current?.Resources[key] as Geometry) ?? Geometry.Empty;

    private void Update()
    {
        double size = GlyphSize;
        Root.Width = size;
        Root.Height = size;

        Wedge.Visibility = Visibility.Collapsed;
        GlyphBox.Visibility = Visibility.Visible;
        GlyphPath.Stroke = null;
        GlyphPath.Fill = null;

        switch (Rung)
        {
            case CertaintyRung.Definitive:
                Disc.Fill = Brush("Sar.DefBrush");
                Disc.Stroke = null;
                Disc.StrokeThickness = 0;
                GlyphPath.Data = Glyph("Sar.Glyph.Check");
                GlyphPath.Stroke = Brush("Sar.OnAccentBrush");
                GlyphPath.StrokeThickness = 1.8;
                break;

            case CertaintyRung.Bounded:
                Disc.Fill = Brush("Sar.SurfaceBrush");
                Disc.Stroke = Brush("Sar.BndBrush");
                Disc.StrokeThickness = Math.Max(1.5, size * 0.11);
                if (RemainingFraction is double fraction)
                {
                    GlyphBox.Visibility = Visibility.Collapsed;
                    Wedge.Visibility = Visibility.Visible;
                    Wedge.Fill = Brush("Sar.BndBrush");
                    Wedge.Fraction = fraction;
                    double inset = size * 0.24;
                    Wedge.Margin = new Thickness(inset);
                }
                else
                {
                    GlyphPath.Data = Glyph("Sar.Glyph.Hourglass");
                    GlyphPath.Fill = Brush("Sar.BndBrush");
                }
                break;

            default:
                Disc.Fill = Brushes.Transparent;
                Disc.Stroke = Brush("Sar.UnrLineBrush");
                Disc.StrokeThickness = Math.Max(1.25, size * 0.09);
                GlyphPath.Data = Glyph("Sar.Glyph.Dash");
                GlyphPath.Stroke = Brush("Sar.UnrBrush");
                GlyphPath.StrokeThickness = 1.8;
                break;
        }
    }
}
