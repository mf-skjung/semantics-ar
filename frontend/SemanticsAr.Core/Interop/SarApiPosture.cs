using System.Runtime.InteropServices;

namespace SemanticsAr.Core.Interop;

[StructLayout(LayoutKind.Sequential)]
public struct SarApiPosture
{
    public uint ProtocolVersion;
    public uint ServiceRunning;
    public uint DriverConnected;
    public uint IntegrityHalt;
    public uint Mode;
    public ulong CapturedKeyCount;
    public uint Descents;
    public uint PreserveHealth;
    public uint OldestExpiryBucket;
}
