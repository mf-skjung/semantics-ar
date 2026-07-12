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

    public CertaintyLadder()
    {
        InitializeComponent();
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
