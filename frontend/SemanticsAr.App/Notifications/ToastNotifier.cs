using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.Notifications;

internal sealed class ToastNotifier : IDisposable
{
    private static readonly TimeSpan CoalesceWindow = TimeSpan.FromSeconds(3);

    private readonly bool _available;
    private readonly object _gate = new();
    private readonly Dictionary<JournalEventClass, int> _pending = new();
    private readonly Timer _timer;

    public ToastNotifier()
    {
        _timer = new Timer(_ => Flush(), null, Timeout.Infinite, Timeout.Infinite);

        if (!AppNotificationManager.IsSupported())
            return;

        try
        {
            AppNotificationManager.Default.Register();
            _available = true;
        }
        catch (Exception)
        {
        }
    }

    public void Notify(JournalEvent journalEvent)
    {
        if (!_available || journalEvent.Gap || !IsReportable(journalEvent.EventClass))
            return;

        lock (_gate)
        {
            _pending.TryGetValue(journalEvent.EventClass, out int count);
            _pending[journalEvent.EventClass] = count + 1;
            _timer.Change(CoalesceWindow, Timeout.InfiniteTimeSpan);
        }
    }

    public void NotifyIntegrityHalt()
    {
        if (!_available)
            return;

        AppNotification notification = new AppNotificationBuilder()
            .AddText("Protection may be compromised")
            .AddText("The protected recovery store failed its integrity check. Recovery can no longer be "
                + "guaranteed. Open semantics-ar and investigate this device.")
            .BuildNotification();
        Show(notification);
    }

    // The notification platform (WpnService / AppNotificationManager) can fail at runtime with a
    // COMException; this runs on a timer/threadpool thread, so an escape would be an unhandled
    // threadpool exception that terminates the process. Never let a toast failure kill the app.
    private static void Show(AppNotification notification)
    {
        try
        {
            AppNotificationManager.Default.Show(notification);
        }
        catch (Exception)
        {
        }
    }

    private void Flush()
    {
        KeyValuePair<JournalEventClass, int>[] batch;

        lock (_gate)
        {
            if (_pending.Count == 0)
                return;

            batch = _pending.ToArray();
            _pending.Clear();
        }

        foreach ((JournalEventClass eventClass, int count) in batch)
        {
            (string title, string body) = Describe(eventClass, count);
            AppNotification notification = new AppNotificationBuilder()
                .AddText(title)
                .AddText(body)
                .BuildNotification();
            Show(notification);
        }
    }

    private static bool IsReportable(JournalEventClass eventClass) =>
        eventClass is JournalEventClass.KeyCaptured
            or JournalEventClass.BlockForward
            or JournalEventClass.BlockPhantom
            or JournalEventClass.BlockCapacity;

    private static (string Title, string Body) Describe(JournalEventClass eventClass, int count)
    {
        return eventClass switch
        {
            JournalEventClass.KeyCaptured => count == 1
                ? ("Encryption key captured", "A destructive write was observed and its key was captured, so the original can be recovered.")
                : ($"{count} encryption keys captured", $"{count} destructive writes were observed and their keys were captured, so the originals can be recovered."),
            JournalEventClass.BlockForward => count == 1
                ? ("Destruction blocked", "A convicted process was blocked from a further destructive write.")
                : ("Destruction blocked", $"A convicted process was blocked from {count} further destructive writes."),
            JournalEventClass.BlockPhantom =>
                ("Destruction blocked", "A process that touched decoy files was blocked."),
            JournalEventClass.BlockCapacity =>
                ("Protection capacity reached", "A destructive write was declined because the recovery store is full. Review your budget."),
            _ => (string.Empty, string.Empty),
        };
    }

    public void Dispose()
    {
        _timer.Dispose();
        if (_available)
            AppNotificationManager.Default.Unregister();
    }
}
