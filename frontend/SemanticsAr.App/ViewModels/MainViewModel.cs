using System.Collections.ObjectModel;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public partial class MainViewModel : ObservableObject
{
    private readonly Func<IElevatedControlChannel> _channelFactory;

    [ObservableProperty]
    private SurfaceItem _selectedSurface;

    [ObservableProperty]
    private string _modeLabel = string.Empty;

    private PostureMode _currentMode = PostureMode.Unknown;

    public MainViewModel(PostureService posture, Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
        Home = new HomeViewModel(posture);

        Surfaces =
        [
            new SurfaceItem("Home", Home),
            new SurfaceItem("Recovery", new RecoveryViewModel(posture, channelFactory)),
            new SurfaceItem("Budget & exemptions", new BudgetViewModel(channelFactory)),
        ];

        _selectedSurface = Surfaces[0];

        posture.PostureChanged += (_, e) =>
            Application.Current.Dispatcher.Invoke(() => ApplyMode(e.Verdict.Mode));
        if (posture.Current is PostureVerdict current)
            ApplyMode(current.Mode);
    }

    public ObservableCollection<SurfaceItem> Surfaces { get; }

    public HomeViewModel Home { get; }

    public bool CanSwitchMode => _currentMode is PostureMode.Audit or PostureMode.Enforce;

    public ModeControlViewModel CreateModeControl() => new(_currentMode, _channelFactory);

    private void ApplyMode(PostureMode mode)
    {
        _currentMode = mode;
        ModeLabel = ModeLabelFor(mode);
        OnPropertyChanged(nameof(CanSwitchMode));
    }

    private static string ModeLabelFor(PostureMode mode) => mode switch
    {
        PostureMode.Enforce => "ENFORCE MODE",
        PostureMode.Audit => "AUDIT MODE",
        _ => "MODE UNKNOWN",
    };
}
