using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;
using SemanticsAr.Core.Services;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class JournalServiceTests
{
    private sealed class ScriptedEventReader : IEventReader
    {
        private readonly Queue<SarApiResult> _opens = new();
        private readonly Queue<(SarApiResult Result, SarApiEvent Frame)> _reads = new();

        public int OpenCalls { get; private set; }

        public int CloseCalls { get; private set; }

        public void EnqueueOpen(SarApiResult result)
        {
            _opens.Enqueue(result);
        }

        public void EnqueueRead(SarApiResult result, SarApiEvent frame = default)
        {
            _reads.Enqueue((result, frame));
        }

        public SarApiResult Open(out nint handle)
        {
            OpenCalls++;
            SarApiResult result = _opens.Count > 0 ? _opens.Dequeue() : SarApiResult.Ok;
            handle = result == SarApiResult.Ok ? (nint)1 : 0;
            return result;
        }

        public SarApiResult Read(nint handle, out SarApiEvent evt)
        {
            (SarApiResult result, SarApiEvent frame) = _reads.Dequeue();
            evt = frame;
            return result;
        }

        public void Close(nint handle)
        {
            CloseCalls++;
        }
    }

    private static SarApiEvent Frame(uint eventClass, ulong generation, ulong sequence,
        ulong actorStartKey = 0, uint gap = 0, ulong timestamp = 133_000_000_000_000_000ul)
    {
        return new SarApiEvent
        {
            Valid = 1u,
            Gap = gap,
            EventClass = eventClass,
            Generation = generation,
            Sequence = sequence,
            Timestamp = timestamp,
            ActorStartKey = actorStartKey,
        };
    }

    [Fact]
    public void ReadNext_DeliversMappedEvent()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 1ul, actorStartKey: 42ul));

        using JournalService service = new(reader);
        JournalEvent? received = null;
        service.EventReceived += (_, e) => received = e.Event;

        Assert.True(service.ReadNext());
        Assert.NotNull(received);
        Assert.Equal(JournalEventClass.KeyCaptured, received!.Value.EventClass);
        Assert.Equal(100ul, received.Value.Generation);
        Assert.Equal(1ul, received.Value.Sequence);
        Assert.Equal(42ul, received.Value.ActorStartKey);
        Assert.False(received.Value.Gap);
    }

    [Fact]
    public void ReadNext_SuppressesDuplicateSequence()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 1ul));
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 1ul));

        using JournalService service = new(reader);
        int raised = 0;
        service.EventReceived += (_, _) => raised++;

        Assert.True(service.ReadNext());
        Assert.True(service.ReadNext());
        Assert.Equal(1, raised);
    }

    [Fact]
    public void ReadNext_SequenceJump_FlagsClientSideGap()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 1ul));
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 5ul));

        using JournalService service = new(reader);
        List<JournalEvent> received = [];
        service.EventReceived += (_, e) => received.Add(e.Event);

        service.ReadNext();
        service.ReadNext();

        Assert.Equal(2, received.Count);
        Assert.False(received[0].Gap);
        Assert.True(received[1].Gap);
    }

    [Fact]
    public void ReadNext_GenerationChange_FlagsClientSideGap()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 3ul));
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 200ul, 1ul));

        using JournalService service = new(reader);
        List<JournalEvent> received = [];
        service.EventReceived += (_, e) => received.Add(e.Event);

        service.ReadNext();
        service.ReadNext();

        Assert.Equal(2, received.Count);
        Assert.False(received[0].Gap);
        Assert.True(received[1].Gap);
    }

    [Fact]
    public void ReadNext_ServerReportedGap_PassesThrough()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 50ul, gap: 1u));

        using JournalService service = new(reader);
        JournalEvent? received = null;
        service.EventReceived += (_, e) => received = e.Event;

        service.ReadNext();

        Assert.True(received!.Value.Gap);
    }

    [Fact]
    public void ReadNext_OpenFailure_ReturnsFalseAndDoesNotRead()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.PipeUnavailable);

        using JournalService service = new(reader);

        Assert.False(service.ReadNext());
        Assert.False(service.IsConnected);
    }

    [Fact]
    public void ReadNext_ReadFailure_DisconnectsAndRaisesDisconnected()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.TransportError);

        using JournalService service = new(reader);
        int disconnected = 0;
        service.Disconnected += (_, _) => disconnected++;

        Assert.True(service.IsConnected == false);
        service.ReadNext();

        Assert.Equal(1, disconnected);
        Assert.False(service.IsConnected);
        Assert.Equal(1, reader.CloseCalls);
    }

    [Fact]
    public void ReadNext_ReconnectsAfterDisconnect()
    {
        ScriptedEventReader reader = new();
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.TransportError);
        reader.EnqueueOpen(SarApiResult.Ok);
        reader.EnqueueRead(SarApiResult.Ok, Frame(1u, 100ul, 1ul));

        using JournalService service = new(reader);
        Assert.False(service.ReadNext());
        Assert.True(service.ReadNext());
        Assert.Equal(2, reader.OpenCalls);
    }
}
