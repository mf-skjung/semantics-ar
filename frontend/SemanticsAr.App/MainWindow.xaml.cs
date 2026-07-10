using System.Windows;
using SemanticsAr.App.ViewModels;
using Wpf.Ui.Controls;

namespace SemanticsAr.App;

public partial class MainWindow : FluentWindow
{
    public MainWindow()
    {
        InitializeComponent();
    }

    private void ModeChip_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel main)
            return;

        ModeSwitchWindow dialog = new()
        {
            Owner = this,
            DataContext = main.CreateModeControl(),
        };
        dialog.ShowDialog();
    }
}
