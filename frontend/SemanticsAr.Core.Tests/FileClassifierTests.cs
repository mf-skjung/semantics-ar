using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class FileClassifierTests
{
    [Theory]
    [InlineData(@"\Device\HarddiskVolume3\Users\a\report.docx", FileClass.Document)]
    [InlineData(@"\Device\HarddiskVolume3\Users\a\sheet.XLSX", FileClass.Document)]
    [InlineData(@"C:\shots\holiday.jpeg", FileClass.Image)]
    [InlineData(@"C:\clips\render.mov", FileClass.Media)]
    [InlineData(@"C:\dl\archive.7z", FileClass.Archive)]
    [InlineData(@"C:\src\main.cs", FileClass.Code)]
    [InlineData(@"C:\data\blob.bin", FileClass.Other)]
    public void Classify_MapsExtensionToCoarseClass(string path, FileClass expected)
    {
        Assert.Equal(expected, FileClassifier.Classify(path));
    }

    [Theory]
    [InlineData(@"C:\folder\noext")]
    [InlineData(@"C:\folder\trailingdot.")]
    [InlineData(@"C:\.hiddenonly")]
    [InlineData("")]
    public void Classify_NoUsableExtension_IsOther(string path)
    {
        Assert.Equal(FileClass.Other, FileClassifier.Classify(path));
    }

    [Fact]
    public void Label_CoversEveryClass()
    {
        foreach (FileClass cls in Enum.GetValues<FileClass>())
            Assert.False(string.IsNullOrWhiteSpace(FileClassifier.Label(cls)));
    }
}
