namespace SemanticsAr.App.ViewModels;

public sealed class PlaceholderViewModel(string title, string message)
{
    public string Title { get; } = title;

    public string Message { get; } = message;
}
