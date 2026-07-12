using System.Windows;
using System.Windows.Media;
using System.Windows.Shapes;

namespace SemanticsAr.App.Controls;

public sealed class SarPieWedge : Shape
{
    public static readonly DependencyProperty FractionProperty = DependencyProperty.Register(
        nameof(Fraction),
        typeof(double),
        typeof(SarPieWedge),
        new FrameworkPropertyMetadata(1.0, FrameworkPropertyMetadataOptions.AffectsRender));

    public double Fraction
    {
        get => (double)GetValue(FractionProperty);
        set => SetValue(FractionProperty, value);
    }

    protected override Geometry DefiningGeometry
    {
        get
        {
            double w = ActualWidth;
            double h = ActualHeight;
            if (w <= 0 || h <= 0)
                return Geometry.Empty;

            double fraction = Math.Max(0, Math.Min(1, Fraction));
            if (fraction <= 0)
                return Geometry.Empty;

            double r = (Math.Min(w, h) - StrokeThickness) / 2;
            if (r <= 0)
                return Geometry.Empty;

            Point center = new(w / 2, h / 2);

            Point Point(double deg)
            {
                double a = (deg - 90) * Math.PI / 180;
                return new Point(center.X + r * Math.Cos(a), center.Y + r * Math.Sin(a));
            }

            double sweep = fraction * 360;
            if (sweep >= 359.99)
            {
                EllipseGeometry ellipse = new(center, r, r);
                return ellipse;
            }

            PathFigure figure = new()
            {
                StartPoint = center,
                IsClosed = true,
                IsFilled = true,
            };
            figure.Segments.Add(new LineSegment(Point(0), false));
            figure.Segments.Add(new ArcSegment(
                Point(sweep),
                new Size(r, r),
                0,
                sweep > 180,
                SweepDirection.Clockwise,
                true));

            PathGeometry geometry = new();
            geometry.Figures.Add(figure);
            return geometry;
        }
    }
}
