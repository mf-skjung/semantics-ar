using System.Windows;
using System.Windows.Interop;
using SemanticsAr.App.Design;
using SemanticsAr.App.Interop;
using SemanticsAr.App.Notifications;
using SemanticsAr.App.ViewModels;
using SemanticsAr.Core.Services;
using Wpf.Ui.Appearance;

namespace SemanticsAr.App;

public partial class App : Application
{
    private PostureService? _posture;
    private JournalService? _journal;
    private ToastNotifier? _toast;
    private TrayIconController? _tray;
    private MainWindow? _window;
    private DateTimeOffset _startedAt;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        ProcessHardening.Apply();
        ShutdownMode = ShutdownMode.OnExplicitShutdown;

        ApplicationThemeManager.ApplySystemTheme();
        DesignTokens.Install();

        _startedAt = DateTimeOffset.UtcNow;
        _posture = new PostureService(new NativePostureReader());
        _journal = new JournalService(new NativeEventReader());
        _toast = new ToastNotifier();
        _journal.EventReceived += OnJournalEventReceived;

        _window = new MainWindow();
        _window.DataContext = new MainViewModel(_posture, OpenElevatedChannel);
        _window.Closing += OnWindowClosing;
        MainWindow = _window;

        _tray = new TrayIconController(_posture, ShowMainWindow, Shutdown);

        _window.Show();
        _posture.Start(TimeSpan.FromMilliseconds(1500));
        _journal.Start(TimeSpan.FromSeconds(2));

        if (!OnboardingStore.IsCompleted())
            new OnboardingWindow(new OnboardingViewModel()) { Owner = _window }.ShowDialog();
    }

    private void OnJournalEventReceived(object? sender, JournalEventArgs e)
    {
        if (e.Event.Timestamp >= _startedAt)
            _toast?.Notify(e.Event);
    }

    private void OnWindowClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        e.Cancel = true;
        _window?.Hide();
    }

    private IElevatedControlChannel OpenElevatedChannel()
    {
        nint owner = _window is null ? nint.Zero : new WindowInteropHelper(_window).Handle;
        return ElevatedControlChannel.Activate(owner);
    }

    private void ShowMainWindow()
    {
        if (_window is null)
            return;

        _window.Show();
        if (_window.WindowState == WindowState.Minimized)
            _window.WindowState = WindowState.Normal;
        _window.Activate();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        _journal?.Dispose();
        _toast?.Dispose();
        _posture?.Dispose();
        base.OnExit(e);
    }
}
