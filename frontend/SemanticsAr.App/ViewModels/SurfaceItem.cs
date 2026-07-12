using Wpf.Ui.Controls;

namespace SemanticsAr.App.ViewModels;

public sealed class SurfaceItem(string name, object content, SymbolRegular icon)
{
    public string Name { get; } = name;

    public object Content { get; } = content;

    public SymbolRegular Icon { get; } = icon;

    public override string ToString() => Name;
}
