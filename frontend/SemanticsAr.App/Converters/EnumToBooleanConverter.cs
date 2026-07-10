using System.Globalization;
using System.Windows.Data;

namespace SemanticsAr.App.Converters;

public sealed class EnumToBooleanConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is not null && parameter is string name
        && string.Equals(value.ToString(), name, StringComparison.Ordinal);

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true && parameter is string name
            ? Enum.Parse(Nullable.GetUnderlyingType(targetType) ?? targetType, name)
            : Binding.DoNothing;
}
