using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace SemanticsAr.App.Controls;

public enum VerdictTone
{
    Healthy,
    Attention,
    Alarm,
    Unknown,
}

public partial class VerdictSeal : UserControl
{
    public static readonly DependencyProperty ToneProperty = DependencyProperty.Register(
        nameof(Tone),
        typeof(VerdictTone),
        typeof(VerdictSeal),
        new PropertyMetadata(VerdictTone.Unknown, OnVisualChanged));

    public static readonly DependencyProperty IsAuditProperty = DependencyProperty.Register(
        nameof(IsAudit),
        typeof(bool),
        typeof(VerdictSeal),
        new PropertyMetadata(false, OnVisualChanged));

    public static readonly DependencyProperty IsStaleProperty = DependencyProperty.Register(
        nameof(IsStale),
        typeof(bool),
        typeof(VerdictSeal),
        new PropertyMetadata(false, OnVisualChanged));

    private static readonly DoubleCollection DotPattern = Freeze(new DoubleCollection { 0.05, 2.4 });

    public VerdictSeal()
    {
        InitializeComponent();
        Update();
    }

    public VerdictTone Tone
    {
        get => (VerdictTone)GetValue(ToneProperty);
        set => SetValue(ToneProperty, value);
    }

    public bool IsAudit
    {
        get => (bool)GetValue(IsAuditProperty);
        set => SetValue(IsAuditProperty, value);
    }

    public bool IsStale
    {
        get => (bool)GetValue(IsStaleProperty);
        set => SetValue(IsStaleProperty, value);
    }

    private static DoubleCollection Freeze(DoubleCollection collection)
    {
        collection.Freeze();
        return collection;
    }

    private static void OnVisualChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((VerdictSeal)d).Update();
    }

    private static Brush Brush(string key) =>
        Application.Current?.Resources[key] as Brush ?? Brushes.Gray;

    private static Geometry Glyph(string key) =>
        (Application.Current?.Resources[key] as Geometry) ?? Geometry.Empty;

    private void Update()
    {
        string prefix = Tone switch
        {
            VerdictTone.Healthy => "Sar.Def",
            VerdictTone.Attention => "Sar.Bnd",
            VerdictTone.Alarm => "Sar.Red",
            _ => "Sar.Unr",
        };

        Brush strong = Brush(prefix + "Brush");
        Ring.Stroke = strong;
        Ring.StrokeDashArray = IsAudit ? DotPattern : null;

        Disc.Fill = Brush(prefix + "BgBrush");
        Disc.Stroke = Brush(prefix + "LineBrush");

        GlyphPath.Data = Tone switch
        {
            VerdictTone.Healthy => Glyph("Sar.Glyph.Check"),
            VerdictTone.Attention => Glyph("Sar.Glyph.Bang"),
            VerdictTone.Alarm => Glyph("Sar.Glyph.Tri"),
            _ => Glyph("Sar.Glyph.Question"),
        };
        GlyphPath.Stroke = strong;

        Root.Opacity = IsStale ? 0.6 : 1.0;
    }
}
