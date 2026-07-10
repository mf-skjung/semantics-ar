using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace SemanticsAr.Core.Interop;

[GeneratedComInterface(StringMarshalling = StringMarshalling.Custom,
                       StringMarshallingCustomType = typeof(BStrStringMarshaller))]
[Guid("B3F2A6C1-5D84-4E2A-9C77-1E5A0D9C4A11")]
internal partial interface ISarElevatedControl
{
    [PreserveSig]
    int CatalogPage(uint start, out uint total, out uint returned, out nint blob);

    [PreserveSig]
    int PreservePage(uint start, out uint total, out uint returned, out nint blob);

    [PreserveSig]
    int AppIdentityPage(uint start, out uint total, out uint returned, out nint blob);

    [PreserveSig]
    int Recover(nint keyId, string targetPath, out int result);

    [PreserveSig]
    int PreserveRecover(string targetPath, ulong offset, ulong length, out int result);

    [PreserveSig]
    int SetMode(uint mode, out int result);

    [PreserveSig]
    int SetBudget(ulong retention100ns, ulong capacityBytes, out int result);

    [PreserveSig]
    int ResolveIdentity(string imagePath, out nint identityBlob, out uint verdict, out int result);

    [PreserveSig]
    int WhitelistAdd(string imagePath, out uint verdict, out int result);

    [PreserveSig]
    int WhitelistRemove(string imagePath, out uint verdict, out int result);

    [PreserveSig]
    int WhitelistPage(uint start, out uint total, out uint returned, out nint blob);

    [PreserveSig]
    int Shutdown();
}
