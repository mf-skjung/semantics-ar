using System.ComponentModel;
using SemanticsAr.App.ViewModels;
using Wpf.Ui.Controls;

namespace SemanticsAr.App;

public partial class OnboardingWindow : FluentWindow
{
    private readonly OnboardingViewModel _viewModel;

    public OnboardingWindow(OnboardingViewModel viewModel)
    {
        _viewModel = viewModel;
        DataContext = viewModel;
        InitializeComponent();
        _viewModel.PropertyChanged += OnViewModelPropertyChanged;
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(OnboardingViewModel.Completed) && _viewModel.Completed)
        {
            OnboardingStore.MarkCompleted();
            Close();
        }
    }
}
