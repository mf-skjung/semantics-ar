using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class JournalService : IDisposable
{
    private readonly IEventReader _reader;
    private readonly object _gate = new();

    private nint _handle;
    private ulong _lastGeneration;
    private ulong _lastSequence;
    private Thread? _thread;
    private CancellationTokenSource? _cts;

    public JournalService(IEventReader reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        _reader = reader;
    }

    public event EventHandler<JournalEventArgs>? EventReceived;

    public event EventHandler? Disconnected;

    public bool IsConnected
    {
        get
        {
            lock (_gate)
                return _handle != 0;
        }
    }

    public bool ReadNext()
    {
        if (!EnsureConnected())
            return false;

        nint handle;
        lock (_gate)
            handle = _handle;

        SarApiResult result = _reader.Read(handle, out SarApiEvent raw);
        if (result != SarApiResult.Ok)
        {
            Disconnect();
            Disconnected?.Invoke(this, EventArgs.Empty);
            return false;
        }

        if (raw.Valid != 0)
            Process(in raw);

        return true;
    }

    public void Start(TimeSpan reconnectDelay)
    {
        lock (_gate)
        {
            if (_thread != null)
                return;

            _cts = new CancellationTokenSource();
            CancellationToken token = _cts.Token;
            _thread = new Thread(() => RunLoop(token, reconnectDelay))
            {
                IsBackground = true,
                Name = "SemanticsAr.Journal",
            };
            _thread.Start();
        }
    }

    public void Stop()
    {
        Thread? thread;
        CancellationTokenSource? cts;

        lock (_gate)
        {
            thread = _thread;
            cts = _cts;
            _thread = null;
            _cts = null;
        }

        cts?.Cancel();
        thread?.Join();
        cts?.Dispose();
        Disconnect();
    }

    public void Dispose()
    {
        Stop();
    }

    private void RunLoop(CancellationToken token, TimeSpan reconnectDelay)
    {
        while (!token.IsCancellationRequested)
        {
            if (!ReadNext())
                token.WaitHandle.WaitOne(reconnectDelay);
        }
    }

    private bool EnsureConnected()
    {
        lock (_gate)
        {
            if (_handle != 0)
                return true;
        }

        SarApiResult result = _reader.Open(out nint handle);
        if (result != SarApiResult.Ok)
            return false;

        lock (_gate)
            _handle = handle;
        return true;
    }

    private void Disconnect()
    {
        nint handle;

        lock (_gate)
        {
            handle = _handle;
            _handle = 0;
        }

        if (handle != 0)
            _reader.Close(handle);
    }

    private void Process(in SarApiEvent raw)
    {
        bool isFirst;
        bool duplicate;
        bool discontinuity;

        lock (_gate)
        {
            isFirst = _lastSequence == 0 && _lastGeneration == 0;
            duplicate = !isFirst
                && raw.Generation == _lastGeneration
                && raw.Sequence <= _lastSequence;
            discontinuity = !isFirst
                && !duplicate
                && (raw.Generation != _lastGeneration || raw.Sequence > _lastSequence + 1);

            if (!duplicate)
            {
                _lastGeneration = raw.Generation;
                _lastSequence = raw.Sequence;
            }
        }

        if (duplicate)
            return;

        JournalEvent journalEvent = new(
            (JournalEventClass)raw.EventClass,
            raw.Generation,
            raw.Sequence,
            DateTime.FromFileTimeUtc((long)raw.Timestamp),
            raw.ActorStartKey,
            raw.Gap != 0 || discontinuity);

        EventReceived?.Invoke(this, new JournalEventArgs(journalEvent));
    }
}
