using System.Runtime.InteropServices;

namespace SemanticsAr.Core.Interop;

[StructLayout(LayoutKind.Sequential)]
public struct SarApiEvent
{
    public uint Valid;
    public uint Gap;
    public uint EventClass;
    public ulong Generation;
    public ulong Sequence;
    public ulong Timestamp;
    public ulong ActorStartKey;
}
