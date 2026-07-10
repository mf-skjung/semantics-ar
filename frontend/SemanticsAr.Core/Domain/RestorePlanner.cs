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

public enum RestoreDestination
{
    OriginalLocations,
    CopyToFolder,
}

public enum PlanDecision
{
    InPlace,
    ToFolder,
    DeclineDefinitiveFolder,
    DeclineBlocked,
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

    private const long MaxFileTime = 2650467743999999999;

    public static DateTimeOffset? Anchor(ulong fileTime) =>
        fileTime is 0 or > (ulong)MaxFileTime
            ? null
            : new DateTimeOffset(DateTime.FromFileTimeUtc((long)fileTime));

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

    public static PlanDecision Decide(CertaintyRung rung, RestoreDisposition disposition, RestoreDestination destination)
    {
        if (destination == RestoreDestination.CopyToFolder)
            return rung == CertaintyRung.Definitive
                ? PlanDecision.DeclineDefinitiveFolder
                : PlanDecision.ToFolder;

        return disposition == RestoreDisposition.Blocked
            ? PlanDecision.DeclineBlocked
            : PlanDecision.InPlace;
    }

    public static string DefaultFolderRoot(DateTimeOffset when) =>
        $@"C:\Recovered\{when.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture)}";

    public static string FolderTargetPath(string folderRoot, string provenancePath, ISet<string> reserved, IFileProbe probe)
    {
        const int max = 259;
        int cut = provenancePath.LastIndexOf('\\');
        string leaf = cut >= 0 ? provenancePath[(cut + 1)..] : provenancePath;
        if (leaf.Length == 0)
            throw new ArgumentException($"Not a file path: '{provenancePath}'", nameof(provenancePath));

        int dot = leaf.LastIndexOf('.');
        string name = dot > 0 ? leaf[..dot] : leaf;
        string ext = dot > 0 ? leaf[dot..] : string.Empty;

        for (int n = 0; ; n++)
        {
            string suffix = (n == 0 ? string.Empty : $" ({n})") + ext;
            int budget = max - folderRoot.Length - 1 - suffix.Length;
            if (budget < 1)
                throw new PathTooLongException(folderRoot);
            string trimmed = name;
            if (name.Length > budget)
            {
                int take = budget;
                if (char.IsHighSurrogate(name[take - 1]))
                    take--;
                trimmed = name[..take];
            }
            string candidate = $@"{folderRoot}\{trimmed}{suffix}";
            if (!probe.Probe(candidate).Exists && reserved.Add(candidate))
                return candidate;
        }
    }
}
