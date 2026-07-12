using System.Windows;
using System.Windows.Controls;

namespace SemanticsAr.App.Controls;

public partial class ModePictogram : UserControl
{
    public static readonly DependencyProperty IsEnforceProperty = DependencyProperty.Register(
        nameof(IsEnforce),
        typeof(bool),
        typeof(ModePictogram),
        new PropertyMetadata(true, OnIsEnforceChanged));

    public ModePictogram()
    {
        InitializeComponent();
        Apply();
    }

    public bool IsEnforce
    {
        get => (bool)GetValue(IsEnforceProperty);
        set => SetValue(IsEnforceProperty, value);
    }

    private static void OnIsEnforceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        ((ModePictogram)d).Apply();
    }

    private void Apply()
    {
        EnforceLayer.Visibility = IsEnforce ? Visibility.Visible : Visibility.Collapsed;
        AuditLayer.Visibility = IsEnforce ? Visibility.Collapsed : Visibility.Visible;
    }
}
