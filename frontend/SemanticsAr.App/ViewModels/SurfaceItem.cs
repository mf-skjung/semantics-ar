namespace SemanticsAr.App.ViewModels;

public sealed class SurfaceItem(string name, object content)
{
    public string Name { get; } = name;

    public object Content { get; } = content;

    public override string ToString() => Name;
}
