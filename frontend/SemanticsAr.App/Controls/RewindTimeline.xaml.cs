using System.Windows;
using System.Windows.Controls;

namespace SemanticsAr.App.Controls;

public partial class RewindTimeline : UserControl
{
    public static readonly DependencyProperty EarliestLabelProperty = DependencyProperty.Register(
        nameof(EarliestLabel), typeof(string), typeof(RewindTimeline), new PropertyMetadata(string.Empty));

    public RewindTimeline()
    {
        InitializeComponent();
    }

    public string EarliestLabel
    {
        get => (string)GetValue(EarliestLabelProperty);
        set => SetValue(EarliestLabelProperty, value);
    }
}
