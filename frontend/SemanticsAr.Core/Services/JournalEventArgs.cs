using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public sealed class JournalEventArgs(JournalEvent journalEvent) : EventArgs
{
    public JournalEvent Event { get; } = journalEvent;
}
