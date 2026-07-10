using Wpf.Ui.Controls;

namespace SemanticsAr.App;

public partial class ModeSwitchWindow : FluentWindow
{
    public ModeSwitchWindow()
    {
        InitializeComponent();
    }

    private void Close_Click(object sender, System.Windows.RoutedEventArgs e)
    {
        Close();
    }
}
