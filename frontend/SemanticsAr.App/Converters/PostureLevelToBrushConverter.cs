using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.Converters;

public sealed class PostureLevelToBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        string prefix = value switch
        {
            PostureLevel.Green => "Sar.Def",
            PostureLevel.Amber => "Sar.Bnd",
            PostureLevel.Red => "Sar.Red",
            _ => "Sar.Unr",
        };
        string key = (parameter as string) switch
        {
            "bg" => prefix + "BgBrush",
            "line" => prefix + "LineBrush",
            _ => prefix + "Brush",
        };

        return System.Windows.Application.Current.Resources[key] is Brush brush
            ? brush
            : Brushes.Gray;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
