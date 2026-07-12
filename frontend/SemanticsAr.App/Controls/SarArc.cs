using System.Windows;
using System.Windows.Media;
using System.Windows.Shapes;

namespace SemanticsAr.App.Controls;

public sealed class SarArc : Shape
{
    public static readonly DependencyProperty StartAngleProperty = DependencyProperty.Register(
        nameof(StartAngle),
        typeof(double),
        typeof(SarArc),
        new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty SweepAngleProperty = DependencyProperty.Register(
        nameof(SweepAngle),
        typeof(double),
        typeof(SarArc),
        new FrameworkPropertyMetadata(90.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public double StartAngle
    {
        get => (double)GetValue(StartAngleProperty);
        set => SetValue(StartAngleProperty, value);
    }

    public double SweepAngle
    {
        get => (double)GetValue(SweepAngleProperty);
        set => SetValue(SweepAngleProperty, value);
    }

    protected override Geometry DefiningGeometry
    {
        get
        {
            double w = ActualWidth;
            double h = ActualHeight;
            if (w <= 0 || h <= 0)
                return Geometry.Empty;

            double sweep = Math.Max(-359.99, Math.Min(359.99, SweepAngle));
            double r = (Math.Min(w, h) - StrokeThickness) / 2;
            if (r <= 0)
                return Geometry.Empty;

            Point center = new(w / 2, h / 2);

            Point Point(double deg)
            {
                double a = (deg - 90) * Math.PI / 180;
                return new Point(center.X + r * Math.Cos(a), center.Y + r * Math.Sin(a));
            }

            PathFigure figure = new()
            {
                StartPoint = Point(StartAngle),
                IsClosed = false,
                IsFilled = false,
            };
            figure.Segments.Add(new ArcSegment(
                Point(StartAngle + sweep),
                new Size(r, r),
                0,
                Math.Abs(sweep) > 180,
                sweep >= 0 ? SweepDirection.Clockwise : SweepDirection.Counterclockwise,
                true));

            PathGeometry geometry = new();
            geometry.Figures.Add(figure);
            return geometry;
        }
    }
}
