using System.Globalization;
using System.Windows.Data;

namespace SemanticsAr.App.Converters;

public sealed class ExpiryToFractionConverter : IMultiValueConverter
{
    public object? Convert(object[] values, Type targetType, object? parameter, CultureInfo culture)
    {
        if (values.Length < 3
            || values[0] is not DateTimeOffset expiry
            || values[1] is not DateTimeOffset windowStart
            || values[2] is not DateTimeOffset now)
            return null;

        double total = (expiry - windowStart).TotalSeconds;
        if (total <= 0)
            return null;

        double remaining = (expiry - now).TotalSeconds;
        return Math.Max(0, Math.Min(1, remaining / total));
    }

    public object[] ConvertBack(object value, Type[] targetTypes, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException();
    }
}
