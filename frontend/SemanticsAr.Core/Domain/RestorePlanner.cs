using System.Globalization;
using System.IO;

namespace SemanticsAr.Core.Domain;

public enum RestoreDisposition
{
    Additive,
    DeletedSince,
    ModifiedSince,
    Blocked,
}

public readonly record struct FileState(bool Exists, bool IsDirectory, bool IsReparsePoint, bool Accessible, DateTimeOffset LastWriteUtc);

public interface IFileProbe
{
    FileState Probe(string ntPath);
}

public static class RestorePlanner
{
    public const string GlobalRootPrefix = @"\\?\GLOBALROOT";

    public static string ToWin32(string ntPath) =>
        ntPath.StartsWith('\\') ? GlobalRootPrefix + ntPath : ntPath;

    public static DateTimeOffset? Anchor(ulong fileTime)
    {
        if (fileTime == 0 || fileTime > (ulong)long.MaxValue)
            return null;
        long ft = (long)fileTime;
        return ft <= DateTimeOffset.MaxValue.ToFileTime()
            ? DateTimeOffset.FromFileTime(ft)
            : null;
    }

    public static RestoreDisposition Classify(RecoverableItem item, IFileProbe probe) =>
        Classify(item, probe.Probe(item.ProvenancePath));

    public static RestoreDisposition Classify(RecoverableItem item, FileState state)
    {
        if (state.Exists && state.IsDirectory)
            return RestoreDisposition.Blocked;
        if (!state.Exists)
            return state.Accessible ? RestoreDisposition.DeletedSince : RestoreDisposition.ModifiedSince;
        if (state.IsReparsePoint)
            return RestoreDisposition.ModifiedSince;
        return Anchor(item.CaptureTime) is { } anchor && state.LastWriteUtc <= anchor
            ? RestoreDisposition.Additive
            : RestoreDisposition.ModifiedSince;
    }

    public static string SideBySidePath(string ntPath, DateTimeOffset when, ISet<string> reserved, IFileProbe probe)
    {
        const int max = 259;
        int cut = ntPath.LastIndexOf('\\');
        if (cut <= 0 || cut == ntPath.Length - 1)
            throw new ArgumentException(ntPath, nameof(ntPath));

        string dir = ntPath[..cut];
        string leaf = ntPath[(cut + 1)..];
        int dot = leaf.LastIndexOf('.');
        string name = dot > 0 ? leaf[..dot] : leaf;
        string ext = dot > 0 ? leaf[dot..] : string.Empty;
        string stamp = when.ToString("yyyyMMdd_HHmmss", CultureInfo.InvariantCulture);

        for (int n = 0; ; n++)
        {
            string suffix = (n == 0 ? $"_RESTORED_{stamp}" : $"_RESTORED_{stamp}({n})") + ext;
            int budget = max - dir.Length - 1 - suffix.Length;
            if (budget < 1)
                throw new PathTooLongException(ntPath);
            string trimmed = name.Length > budget ? name[..budget] : name;
            string candidate = $"{dir}\\{trimmed}{suffix}";
            if (!probe.Probe(candidate).Exists && reserved.Add(candidate))
                return candidate;
        }
    }
}
