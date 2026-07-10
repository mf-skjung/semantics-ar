using System.Globalization;

namespace SemanticsAr.App.ViewModels;

public static class BudgetFormat
{
    private static readonly string[] ByteUnits = ["bytes", "KB", "MB", "GB", "TB"];

    public static string Bytes(ulong bytes)
    {
        double value = bytes;
        int unit = 0;
        while (unit < ByteUnits.Length - 1
            && Math.Round(value, unit == 0 ? 0 : 1, MidpointRounding.AwayFromZero) >= 1024)
        {
            value /= 1024;
            unit++;
        }

        string number = unit == 0
            ? value.ToString("N0", CultureInfo.CurrentCulture)
            : value.ToString("N1", CultureInfo.CurrentCulture);
        return $"{number} {ByteUnits[unit]}";
    }

    public static string Days(TimeSpan window)
    {
        if (window < TimeSpan.Zero)
            window = TimeSpan.Zero;
        double days = window.TotalDays;
        if (days < 1)
            return "less than a day";
        long whole = (long)Math.Round(days, MidpointRounding.AwayFromZero);
        return whole == 1 ? "about 1 day" : $"about {whole} days";
    }

    public static string Copies(int count) =>
        count == 1 ? "1 kept copy" : $"{count:N0} kept copies";
}
