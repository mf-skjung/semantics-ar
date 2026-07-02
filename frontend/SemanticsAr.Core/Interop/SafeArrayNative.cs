using System.Runtime.InteropServices;

namespace SemanticsAr.Core.Interop;

internal static class SafeArrayNative
{
    public static byte[] CopyBytes(nint psa)
    {
        if (psa == 0)
            return Array.Empty<byte>();
        if (NativeMethods.SafeArrayGetLBound(psa, 1, out int lb) < 0)
            return Array.Empty<byte>();
        if (NativeMethods.SafeArrayGetUBound(psa, 1, out int ub) < 0)
            return Array.Empty<byte>();

        int count = ub - lb + 1;
        if (count <= 0)
            return Array.Empty<byte>();
        if (NativeMethods.SafeArrayAccessData(psa, out nint data) < 0)
            return Array.Empty<byte>();

        try
        {
            byte[] bytes = new byte[count];
            Marshal.Copy(data, bytes, 0, count);
            return bytes;
        }
        finally
        {
            NativeMethods.SafeArrayUnaccessData(psa);
        }
    }

    public static nint FromBytes(ReadOnlySpan<byte> source)
    {
        nint psa = NativeMethods.SafeArrayCreateVector(NativeMethods.VT_UI1, 0, (uint)source.Length);
        if (psa == 0)
            throw new OutOfMemoryException();

        if (NativeMethods.SafeArrayAccessData(psa, out nint data) < 0)
        {
            NativeMethods.SafeArrayDestroy(psa);
            throw new InvalidOperationException();
        }

        try
        {
            unsafe
            {
                source.CopyTo(new Span<byte>((void*)data, source.Length));
            }
        }
        finally
        {
            NativeMethods.SafeArrayUnaccessData(psa);
        }
        return psa;
    }

    public static void Destroy(nint psa)
    {
        if (psa != 0)
            NativeMethods.SafeArrayDestroy(psa);
    }
}
