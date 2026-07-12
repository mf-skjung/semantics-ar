using System.Collections;
using System.Collections.Specialized;
using System.Windows;
using System.Windows.Media;

namespace SemanticsAr.App.Controls;

public sealed class StackMeter : FrameworkElement
{
    public static readonly DependencyProperty SegmentsProperty = DependencyProperty.Register(
        nameof(Segments),
        typeof(IEnumerable),
        typeof(StackMeter),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender, OnSegmentsChanged));

    // AffectsRender only fires when the collection *reference* changes. The view-model reuses one
    // ObservableCollection and mutates its contents (Clear + Add on a range switch), so subscribe to
    // its change notifications and repaint, otherwise the bar keeps stale proportions.
    private static void OnSegmentsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        StackMeter meter = (StackMeter)d;
        if (e.OldValue is INotifyCollectionChanged oldIncc)
            oldIncc.CollectionChanged -= meter.OnSegmentsCollectionChanged;
        if (e.NewValue is INotifyCollectionChanged newIncc)
            newIncc.CollectionChanged += meter.OnSegmentsCollectionChanged;
    }

    private void OnSegmentsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        InvalidateVisual();
    }

    public static readonly DependencyProperty GapProperty = DependencyProperty.Register(
        nameof(Gap),
        typeof(double),
        typeof(StackMeter),
        new FrameworkPropertyMetadata(2.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public IEnumerable? Segments
    {
        get => (IEnumerable?)GetValue(SegmentsProperty);
        set => SetValue(SegmentsProperty, value);
    }

    public double Gap
    {
        get => (double)GetValue(GapProperty);
        set => SetValue(GapProperty, value);
    }

    protected override void OnRender(DrawingContext dc)
    {
        double w = ActualWidth;
        double h = ActualHeight;
        if (w <= 0 || h <= 0 || Segments is null)
            return;

        List<BudgetSegment> segments = new();
        double total = 0;
        foreach (object? item in Segments)
        {
            if (item is BudgetSegment s && s.Weight > 0)
            {
                segments.Add(s);
                total += s.Weight;
            }
        }
        if (total <= 0 || segments.Count == 0)
            return;

        double gap = Gap;
        double usable = Math.Max(0, w - gap * (segments.Count - 1));
        double radius = Math.Min(h / 2, 4);
        double x = 0;

        for (int i = 0; i < segments.Count; i++)
        {
            double segWidth = usable * (segments[i].Weight / total);
            if (segWidth <= 0)
                continue;

            Rect rect = new(x, 0, segWidth, h);
            dc.DrawRoundedRectangle(segments[i].Brush, null, rect, radius, radius);
            x += segWidth + gap;
        }
    }
}
