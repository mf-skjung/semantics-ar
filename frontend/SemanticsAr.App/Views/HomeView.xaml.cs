using System.ComponentModel;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using SemanticsAr.App.ViewModels;

namespace SemanticsAr.App.Views;

public partial class HomeView : UserControl
{
    public HomeView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
        Unloaded += OnUnloaded;
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (e.OldValue is INotifyPropertyChanged oldVm)
            oldVm.PropertyChanged -= OnViewModelPropertyChanged;
        if (e.NewValue is INotifyPropertyChanged newVm)
            newVm.PropertyChanged += OnViewModelPropertyChanged;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        if (DataContext is INotifyPropertyChanged vm)
            vm.PropertyChanged -= OnViewModelPropertyChanged;
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(HomeViewModel.AutomationText))
            RaiseLiveRegionChanged();
    }

    private void RaiseLiveRegionChanged()
    {
        AutomationPeer? peer = UIElementAutomationPeer.FromElement(Hero)
            ?? UIElementAutomationPeer.CreatePeerForElement(Hero);
        peer?.RaiseAutomationEvent(AutomationEvents.LiveRegionChanged);
    }
}
