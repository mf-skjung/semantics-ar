using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.Input;
using H.NotifyIcon;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Services;

namespace SemanticsAr.App;

internal sealed class TrayIconController : IDisposable
{
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyIcon(IntPtr handle);

    private readonly TaskbarIcon _icon;
    private readonly PostureService _posture;
    private readonly Icon _green;
    private readonly Icon _amber;
    private readonly Icon _red;

    public TrayIconController(PostureService posture, Action showWindow, Action exit)
    {
        _posture = posture;
        _green = BuildIcon(Color.FromArgb(0x10, 0x7C, 0x10), DrawCircle);
        _amber = BuildIcon(Color.FromArgb(0xF7, 0xA6, 0x00), DrawTriangle);
        _red = BuildIcon(Color.FromArgb(0xC4, 0x2B, 0x1C), DrawSquare);

        ContextMenu menu = new();
        MenuItem open = new() { Header = "Open semantics-ar" };
        open.Click += (_, _) => showWindow();
        MenuItem quit = new() { Header = "Exit" };
        quit.Click += (_, _) => exit();
        menu.Items.Add(open);
        menu.Items.Add(quit);

        _icon = new TaskbarIcon
        {
            ToolTipText = "semantics-ar",
            ContextMenu = menu,
            LeftClickCommand = new RelayCommand(showWindow),
            Icon = _red,
        };
        _icon.ForceCreate();

        _posture.PostureChanged += OnPostureChanged;
        if (_posture.Current is PostureVerdict current)
            Update(current);
    }

    private static void DrawCircle(Graphics g, Brush b) => g.FillEllipse(b, 2, 2, 28, 28);

    private static void DrawTriangle(Graphics g, Brush b) =>
        g.FillPolygon(b, [new System.Drawing.Point(16, 3), new System.Drawing.Point(30, 29), new System.Drawing.Point(2, 29)]);

    private static void DrawSquare(Graphics g, Brush b) => g.FillRectangle(b, 4, 4, 24, 24);

    private static Icon BuildIcon(Color color, Action<Graphics, Brush> draw)
    {
        using Bitmap bitmap = new(32, 32);
        using (Graphics g = Graphics.FromImage(bitmap))
        using (SolidBrush brush = new(color))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            draw(g, brush);
        }

        IntPtr handle = bitmap.GetHicon();
        try
        {
            using Icon temp = Icon.FromHandle(handle);
            return (Icon)temp.Clone();
        }
        finally
        {
            DestroyIcon(handle);
        }
    }

    private void OnPostureChanged(object? sender, PostureChangedEventArgs e)
    {
        Dispatcher? dispatcher = Application.Current?.Dispatcher;
        if (dispatcher is null || dispatcher.HasShutdownStarted)
            return;
        dispatcher.BeginInvoke(() => Update(e.Verdict));
    }

    private void Update(PostureVerdict verdict)
    {
        _icon.Icon = verdict.Level switch
        {
            PostureLevel.Green => _green,
            PostureLevel.Amber => _amber,
            _ => _red,
        };

        string summary = verdict.Level switch
        {
            PostureLevel.Green => "Protected",
            PostureLevel.Amber => "Needs attention",
            _ => "Not protected",
        };
        _icon.ToolTipText = $"semantics-ar — {summary}";
    }

    public void Dispose()
    {
        _posture.PostureChanged -= OnPostureChanged;
        _icon.Dispose();
        _green.Dispose();
        _amber.Dispose();
        _red.Dispose();
    }
}
