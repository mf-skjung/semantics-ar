namespace SemanticsAr.Core.Domain;

public enum FileClass
{
    Document,
    Image,
    Media,
    Archive,
    Code,
    Other,
}

public static class FileClassifier
{
    private static readonly Dictionary<string, FileClass> ByExtension = new(StringComparer.OrdinalIgnoreCase)
    {
        ["doc"] = FileClass.Document,
        ["docx"] = FileClass.Document,
        ["docm"] = FileClass.Document,
        ["dot"] = FileClass.Document,
        ["dotx"] = FileClass.Document,
        ["rtf"] = FileClass.Document,
        ["txt"] = FileClass.Document,
        ["pdf"] = FileClass.Document,
        ["odt"] = FileClass.Document,
        ["ods"] = FileClass.Document,
        ["odp"] = FileClass.Document,
        ["xls"] = FileClass.Document,
        ["xlsx"] = FileClass.Document,
        ["xlsm"] = FileClass.Document,
        ["xlsb"] = FileClass.Document,
        ["csv"] = FileClass.Document,
        ["tsv"] = FileClass.Document,
        ["ppt"] = FileClass.Document,
        ["pptx"] = FileClass.Document,
        ["pptm"] = FileClass.Document,
        ["md"] = FileClass.Document,
        ["markdown"] = FileClass.Document,
        ["epub"] = FileClass.Document,
        ["pages"] = FileClass.Document,
        ["numbers"] = FileClass.Document,
        ["key"] = FileClass.Document,

        ["jpg"] = FileClass.Image,
        ["jpeg"] = FileClass.Image,
        ["png"] = FileClass.Image,
        ["gif"] = FileClass.Image,
        ["bmp"] = FileClass.Image,
        ["tif"] = FileClass.Image,
        ["tiff"] = FileClass.Image,
        ["webp"] = FileClass.Image,
        ["heic"] = FileClass.Image,
        ["heif"] = FileClass.Image,
        ["svg"] = FileClass.Image,
        ["ico"] = FileClass.Image,
        ["raw"] = FileClass.Image,
        ["cr2"] = FileClass.Image,
        ["nef"] = FileClass.Image,
        ["arw"] = FileClass.Image,
        ["dng"] = FileClass.Image,
        ["psd"] = FileClass.Image,
        ["ai"] = FileClass.Image,

        ["mp4"] = FileClass.Media,
        ["mov"] = FileClass.Media,
        ["mkv"] = FileClass.Media,
        ["avi"] = FileClass.Media,
        ["wmv"] = FileClass.Media,
        ["flv"] = FileClass.Media,
        ["webm"] = FileClass.Media,
        ["m4v"] = FileClass.Media,
        ["mpg"] = FileClass.Media,
        ["mpeg"] = FileClass.Media,
        ["mp3"] = FileClass.Media,
        ["wav"] = FileClass.Media,
        ["flac"] = FileClass.Media,
        ["aac"] = FileClass.Media,
        ["ogg"] = FileClass.Media,
        ["m4a"] = FileClass.Media,
        ["wma"] = FileClass.Media,
        ["aiff"] = FileClass.Media,

        ["zip"] = FileClass.Archive,
        ["rar"] = FileClass.Archive,
        ["7z"] = FileClass.Archive,
        ["tar"] = FileClass.Archive,
        ["gz"] = FileClass.Archive,
        ["tgz"] = FileClass.Archive,
        ["bz2"] = FileClass.Archive,
        ["xz"] = FileClass.Archive,
        ["zst"] = FileClass.Archive,
        ["iso"] = FileClass.Archive,
        ["cab"] = FileClass.Archive,
        ["lz"] = FileClass.Archive,
        ["lzma"] = FileClass.Archive,

        ["c"] = FileClass.Code,
        ["h"] = FileClass.Code,
        ["cpp"] = FileClass.Code,
        ["cc"] = FileClass.Code,
        ["cxx"] = FileClass.Code,
        ["hpp"] = FileClass.Code,
        ["cs"] = FileClass.Code,
        ["js"] = FileClass.Code,
        ["ts"] = FileClass.Code,
        ["jsx"] = FileClass.Code,
        ["tsx"] = FileClass.Code,
        ["py"] = FileClass.Code,
        ["rb"] = FileClass.Code,
        ["go"] = FileClass.Code,
        ["rs"] = FileClass.Code,
        ["java"] = FileClass.Code,
        ["kt"] = FileClass.Code,
        ["swift"] = FileClass.Code,
        ["php"] = FileClass.Code,
        ["pl"] = FileClass.Code,
        ["ps1"] = FileClass.Code,
        ["psm1"] = FileClass.Code,
        ["sh"] = FileClass.Code,
        ["bat"] = FileClass.Code,
        ["cmd"] = FileClass.Code,
        ["json"] = FileClass.Code,
        ["xml"] = FileClass.Code,
        ["yml"] = FileClass.Code,
        ["yaml"] = FileClass.Code,
        ["toml"] = FileClass.Code,
        ["ini"] = FileClass.Code,
        ["sql"] = FileClass.Code,
        ["html"] = FileClass.Code,
        ["css"] = FileClass.Code,
        ["scss"] = FileClass.Code,
    };

    private static readonly Dictionary<string, FileClass>.AlternateLookup<ReadOnlySpan<char>> ByExtensionSpan =
        ByExtension.GetAlternateLookup<ReadOnlySpan<char>>();

    public static FileClass Classify(string path)
    {
        ReadOnlySpan<char> span = path.AsSpan();
        int slash = span.LastIndexOfAny('\\', '/');
        ReadOnlySpan<char> leaf = slash >= 0 ? span[(slash + 1)..] : span;
        int dot = leaf.LastIndexOf('.');
        if (dot <= 0 || dot == leaf.Length - 1)
            return FileClass.Other;

        return ByExtensionSpan.TryGetValue(leaf[(dot + 1)..], out FileClass cls)
            ? cls
            : FileClass.Other;
    }

    public static string Label(FileClass cls) => cls switch
    {
        FileClass.Document => "Documents",
        FileClass.Image => "Images",
        FileClass.Media => "Audio & video",
        FileClass.Archive => "Archives",
        FileClass.Code => "Code & scripts",
        _ => "Other files",
    };
}
