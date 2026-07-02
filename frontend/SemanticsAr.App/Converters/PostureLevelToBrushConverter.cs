using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.App.Converters;

public sealed class PostureLevelToBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        string key = value switch
        {
            PostureLevel.Green => "Status.Green.Brush",
            PostureLevel.Amber => "Status.Amber.Brush",
            PostureLevel.Red => "Status.Red.Brush",
            _ => "Status.Neutral.Brush",
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
