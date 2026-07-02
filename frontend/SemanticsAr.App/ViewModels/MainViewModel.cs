using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App.ViewModels;

public partial class MainViewModel : ObservableObject
{
    [ObservableProperty]
    private SurfaceItem _selectedSurface;

    public MainViewModel(PostureService posture)
    {
        Home = new HomeViewModel(posture);

        Surfaces =
        [
            new SurfaceItem("Home", Home),
            new SurfaceItem("Recovery", new RecoveryPreviewViewModel()),
            new SurfaceItem("Activity",
                new PlaceholderViewModel("Activity",
                    "The detection timeline arrives in a later build of this surface.")),
            new SurfaceItem("Response",
                new PlaceholderViewModel("Response",
                    "Mode and whitelist management arrive in a later build of this surface.")),
            new SurfaceItem("Settings",
                new PlaceholderViewModel("Settings",
                    "Budget and diagnostics arrive in a later build of this surface.")),
        ];

        _selectedSurface = Surfaces[0];
    }

    public ObservableCollection<SurfaceItem> Surfaces { get; }

    public HomeViewModel Home { get; }
}
