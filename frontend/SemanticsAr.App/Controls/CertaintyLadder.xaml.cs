using System.Windows;
using System.Windows.Controls;

namespace SemanticsAr.App.Controls;

public partial class CertaintyLadder : UserControl
{
    public static readonly DependencyProperty DefinitiveCountProperty = DependencyProperty.Register(
        nameof(DefinitiveCount), typeof(string), typeof(CertaintyLadder), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty BoundedCountProperty = DependencyProperty.Register(
        nameof(BoundedCount), typeof(string), typeof(CertaintyLadder), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty UnrecoverableCountProperty = DependencyProperty.Register(
        nameof(UnrecoverableCount), typeof(string), typeof(CertaintyLadder), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty CollapsedProperty = DependencyProperty.Register(
        nameof(Collapsed), typeof(bool), typeof(CertaintyLadder), new PropertyMetadata(false, OnCollapsedChanged));

    public CertaintyLadder()
    {
        InitializeComponent();
    }

    public bool Collapsed
    {
        get => (bool)GetValue(CollapsedProperty);
        set => SetValue(CollapsedProperty, value);
    }

    private static void OnCollapsedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        CertaintyLadder ladder = (CertaintyLadder)d;
        bool collapsed = (bool)e.NewValue;
        double opacity = collapsed ? 0.4 : 1.0;
        Visibility strike = collapsed ? Visibility.Visible : Visibility.Collapsed;
        ladder.DefRow.Opacity = opacity;
        ladder.BndRow.Opacity = opacity;
        ladder.DefStrike.Visibility = strike;
        ladder.BndStrike.Visibility = strike;
    }

    public string DefinitiveCount
    {
        get => (string)GetValue(DefinitiveCountProperty);
        set => SetValue(DefinitiveCountProperty, value);
    }

    public string BoundedCount
    {
        get => (string)GetValue(BoundedCountProperty);
        set => SetValue(BoundedCountProperty, value);
    }

    public string UnrecoverableCount
    {
        get => (string)GetValue(UnrecoverableCountProperty);
        set => SetValue(UnrecoverableCountProperty, value);
    }
}
