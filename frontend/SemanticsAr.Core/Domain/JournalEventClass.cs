namespace SemanticsAr.Core.Domain;

public enum JournalEventClass
{
    None = 0,
    KeyCaptured = 1,
    BlockForward = 2,
    BlockPhantom = 3,
    BlockCapacity = 4,
    ModeChanged = 5,
    WhitelistAdded = 6,
    WhitelistRemoved = 7,
}
