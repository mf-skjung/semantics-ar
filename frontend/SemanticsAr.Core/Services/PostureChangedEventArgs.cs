using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public sealed class PostureChangedEventArgs(PostureVerdict verdict) : EventArgs
{
    public PostureVerdict Verdict { get; } = verdict;
}
