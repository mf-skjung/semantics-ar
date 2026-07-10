using System.IO;
using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public sealed class Win32FileProbe : IFileProbe
{
    public FileState Probe(string ntPath)
    {
        string win32 = RestorePlanner.ToWin32(ntPath);
        try
        {
            FileAttributes attributes = File.GetAttributes(win32);
            bool reparse = (attributes & FileAttributes.ReparsePoint) != 0;
            if ((attributes & FileAttributes.Directory) != 0)
                return new FileState(true, true, reparse, true, default);
            return new FileState(true, false, reparse, true, File.GetLastWriteTimeUtc(win32));
        }
        catch (FileNotFoundException)
        {
            return new FileState(false, false, false, true, default);
        }
        catch (DirectoryNotFoundException)
        {
            return new FileState(false, false, false, true, default);
        }
        catch (UnauthorizedAccessException)
        {
            return new FileState(false, false, false, false, default);
        }
        catch (ArgumentException)
        {
            return new FileState(false, false, false, false, default);
        }
        catch (IOException)
        {
            return new FileState(false, false, false, false, default);
        }
    }
}
