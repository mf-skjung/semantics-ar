using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace SemanticsAr.Core.Domain;

public static class RecoveryLadder
{
    public const int CatalogEntrySize = 576;
    public const int PreserveEntrySize = 552;
    public const int KeyIdSize = 32;

    private const int PathChars = 260;
    private const int PathBytes = PathChars * 2;

    public static IReadOnlyList<RecoverableItem> ParseCatalog(ReadOnlySpan<byte> blob, int count)
    {
        List<RecoverableItem> items = new(count);
        for (int i = 0; i < count; i++)
        {
            ReadOnlySpan<byte> e = blob.Slice(i * CatalogEntrySize, CatalogEntrySize);
            byte[] keyId = e.Slice(0, KeyIdSize).ToArray();
            uint algorithm = BinaryPrimitives.ReadUInt32LittleEndian(e.Slice(32, 4));
            uint mode = BinaryPrimitives.ReadUInt32LittleEndian(e.Slice(36, 4));
            string path = ReadPath(e.Slice(40, PathBytes));
            ulong capture = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(560, 8));
            ulong actor = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(568, 8));

            items.Add(new RecoverableItem
            {
                Rung = CertaintyRung.Definitive,
                ProvenancePath = path,
                KeyId = keyId,
                Algorithm = algorithm,
                Mode = mode,
                CaptureTime = capture,
                ActorStartKey = actor,
            });
        }
        return items;
    }

    public static IReadOnlyList<RecoverableItem> ParsePreserve(ReadOnlySpan<byte> blob, int count)
    {
        List<RecoverableItem> items = new(count);
        for (int i = 0; i < count; i++)
        {
            ReadOnlySpan<byte> e = blob.Slice(i * PreserveEntrySize, PreserveEntrySize);
            string path = ReadPath(e.Slice(0, PathBytes));
            ulong offset = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(520, 8));
            ulong length = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(528, 8));
            ulong capture = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(536, 8));
            ulong size = BinaryPrimitives.ReadUInt64LittleEndian(e.Slice(544, 8));

            items.Add(new RecoverableItem
            {
                Rung = CertaintyRung.Bounded,
                ProvenancePath = path,
                Offset = offset,
                Length = length,
                CaptureTime = capture,
                Size = size,
            });
        }
        return items;
    }

    public static RecoveryOutcome MapResult(RecoverableItem item, int kernelResult)
    {
        RecoveryOutcomeKind kind = kernelResult == 0
            ? RecoveryOutcomeKind.RestoredVerified
            : RecoveryOutcomeKind.DeclinedLeftIntact;
        return new RecoveryOutcome(item, kind, kernelResult, ElevatedError.None);
    }

    private static string ReadPath(ReadOnlySpan<byte> bytes)
    {
        ReadOnlySpan<char> chars = MemoryMarshal.Cast<byte, char>(bytes);
        int end = chars.IndexOf('\0');
        if (end < 0)
            end = chars.Length;
        return new string(chars.Slice(0, end));
    }
}
