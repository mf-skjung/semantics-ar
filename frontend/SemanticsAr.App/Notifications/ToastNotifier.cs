using Microsoft.Windows.AppNotifications;
using Microsoft.Windows.AppNotifications.Builder;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.Notifications;

internal sealed class ToastNotifier : IDisposable
{
    private readonly bool _available;

    public ToastNotifier()
    {
        _available = false;
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
        if (!_available || journalEvent.Gap)
            return;

        (string title, string body) = Describe(journalEvent.EventClass);
        if (title.Length == 0)
            return;

        AppNotificationBuilder builder = new AppNotificationBuilder()
            .AddText(title)
            .AddText(body);

        AppNotificationManager.Default.Show(builder.BuildNotification());
    }

    private static (string Title, string Body) Describe(JournalEventClass eventClass)
    {
        return eventClass switch
        {
            JournalEventClass.KeyCaptured =>
                ("Encryption key captured", "A destructive write was observed and its key was captured for recovery."),
            JournalEventClass.BlockForward =>
                ("Destruction blocked", "A convicted process was blocked from writing further."),
            JournalEventClass.BlockPhantom =>
                ("Destruction blocked", "A process touching decoy files was blocked."),
            JournalEventClass.BlockCapacity =>
                ("Protection capacity low", "A destructive write was blocked because the preservation store is full."),
            _ => (string.Empty, string.Empty),
        };
    }

    public void Dispose()
    {
        if (_available)
            AppNotificationManager.Default.Unregister();
    }
}
