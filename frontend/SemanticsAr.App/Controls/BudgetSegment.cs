using System.Windows.Media;

namespace SemanticsAr.App.Controls;

public sealed class BudgetSegment
{
    public Brush Brush { get; init; } = Brushes.Gray;

    public double Weight { get; init; }

    public string Label { get; init; } = string.Empty;

    public string Detail { get; init; } = string.Empty;
}
