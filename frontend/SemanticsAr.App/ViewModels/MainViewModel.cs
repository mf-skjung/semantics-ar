using System.Collections.ObjectModel;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public partial class MainViewModel : ObservableObject
{
    [ObservableProperty]
    private SurfaceItem _selectedSurface;

    [ObservableProperty]
    private string _modeLabel = string.Empty;

    public MainViewModel(PostureService posture, Func<IElevatedControlChannel> channelFactory)
    {
        Home = new HomeViewModel(posture);

        Surfaces =
        [
            new SurfaceItem("Home", Home),
            new SurfaceItem("Recovery", new RecoveryViewModel(posture, channelFactory)),
            new SurfaceItem("Budget & exemptions", new BudgetViewModel(channelFactory)),
        ];

        _selectedSurface = Surfaces[0];

        posture.PostureChanged += (_, e) =>
            Application.Current.Dispatcher.Invoke(() => ModeLabel = ModeLabelFor(e.Verdict.Mode));
        if (posture.Current is PostureVerdict current)
            ModeLabel = ModeLabelFor(current.Mode);
    }

    public ObservableCollection<SurfaceItem> Surfaces { get; }

    public HomeViewModel Home { get; }

    private static string ModeLabelFor(PostureMode mode) => mode switch
    {
        PostureMode.Enforce => "ENFORCE MODE",
        PostureMode.Audit => "AUDIT MODE",
        _ => "MODE UNKNOWN",
    };
}
