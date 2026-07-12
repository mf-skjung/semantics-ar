using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Threading;
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
    private readonly PolicyViewModel _policy;

    public MainViewModel(PostureService posture, Func<IElevatedControlChannel> channelFactory)
    {
        _channelFactory = channelFactory;
        Home = new HomeViewModel(posture);

        _policy = new PolicyViewModel(channelFactory, PickApp);

        Surfaces =
        [
            new SurfaceItem("Home", Home, Wpf.Ui.Controls.SymbolRegular.Home24),
            new SurfaceItem("Recovery", new RecoveryViewModel(posture, channelFactory), Wpf.Ui.Controls.SymbolRegular.ArrowClockwise24),
            new SurfaceItem("Recovery budget", new BudgetViewModel(channelFactory, RequestExempt), Wpf.Ui.Controls.SymbolRegular.DataHistogram24),
            new SurfaceItem("Exemptions", _policy, Wpf.Ui.Controls.SymbolRegular.ShieldKeyhole24),
        ];

        _selectedSurface = Surfaces[0];

        posture.PostureChanged += (_, e) =>
        {
            Dispatcher? dispatcher = Application.Current?.Dispatcher;
            if (dispatcher is null || dispatcher.HasShutdownStarted)
                return;
            dispatcher.BeginInvoke(() => ApplyMode(e.Verdict.Mode));
        };
        if (posture.Current is PostureVerdict current)
            ApplyMode(current.Mode);
    }

    public ObservableCollection<SurfaceItem> Surfaces { get; }

    public HomeViewModel Home { get; }

    public bool CanSwitchMode => _currentMode is PostureMode.Audit or PostureMode.Enforce;

    public ModeControlViewModel CreateModeControl() => new(_currentMode, _channelFactory);

    private void ApplyMode(PostureMode mode)
    {
        bool wasEnabled = CanSwitchMode;
        _currentMode = mode;
        ModeLabel = ModeLabelFor(mode);
        if (CanSwitchMode != wasEnabled)
            OnPropertyChanged(nameof(CanSwitchMode));
    }

    private static string ModeLabelFor(PostureMode mode) => mode switch
    {
        PostureMode.Enforce => "ENFORCE MODE",
        PostureMode.Audit => "AUDIT MODE",
        _ => "MODE UNKNOWN",
    };

    private void RequestExempt(string imagePath, string costText)
    {
        foreach (SurfaceItem surface in Surfaces)
        {
            if (ReferenceEquals(surface.Content, _policy))
            {
                SelectedSurface = surface;
                break;
            }
        }
        _policy.BeginExempt(imagePath, costText);
    }

    private static string? PickApp()
    {
        Microsoft.Win32.OpenFileDialog dialog = new()
        {
            Filter = "Applications (*.exe)|*.exe",
            CheckFileExists = true,
            Title = "Choose an application to exempt",
        };
        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
