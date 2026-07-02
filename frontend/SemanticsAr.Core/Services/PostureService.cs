using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;

namespace SemanticsAr.Core.Services;

public sealed class PostureService : IDisposable
{
    private readonly IPostureReader _reader;
    private readonly int _staleTolerance;
    private readonly object _gate = new();

    private Timer? _timer;
    private PostureVerdict? _lastGood;
    private PostureVerdict? _current;
    private int _consecutiveTransientFailures;

    public PostureService(IPostureReader reader, int staleTolerance = 3)
    {
        ArgumentNullException.ThrowIfNull(reader);
        if (staleTolerance < 1)
            throw new ArgumentOutOfRangeException(nameof(staleTolerance));

        _reader = reader;
        _staleTolerance = staleTolerance;
    }

    public event EventHandler<PostureChangedEventArgs>? PostureChanged;

    public PostureVerdict? Current
    {
        get
        {
            lock (_gate)
                return _current;
        }
    }

    public void Start(TimeSpan interval)
    {
        lock (_gate)
        {
            _timer ??= new Timer(_ => Poll(), null, TimeSpan.Zero, interval);
        }
    }

    public PostureVerdict Poll()
    {
        SarApiResult result = _reader.Read(out SarApiPosture posture);
        PostureVerdict verdict = Resolve(result, in posture);
        Publish(verdict);
        return verdict;
    }

    private PostureVerdict Resolve(SarApiResult result, in SarApiPosture posture)
    {
        PostureVerdict verdict = PostureEvaluator.Evaluate(result, in posture);

        lock (_gate)
        {
            if (result == SarApiResult.Ok)
            {
                _consecutiveTransientFailures = 0;
                _lastGood = verdict;
                return verdict;
            }

            bool transient = result is SarApiResult.PipeUnavailable
                or SarApiResult.TransportError;
            if (!transient)
            {
                _consecutiveTransientFailures = 0;
                return verdict;
            }

            _consecutiveTransientFailures++;
            if (_consecutiveTransientFailures < _staleTolerance
                && _lastGood is PostureVerdict good)
                return good with { IsStale = true };

            return verdict;
        }
    }

    private void Publish(PostureVerdict verdict)
    {
        bool changed;

        lock (_gate)
        {
            changed = _current != verdict;
            _current = verdict;
        }

        if (changed)
            PostureChanged?.Invoke(this, new PostureChangedEventArgs(verdict));
    }

    public void Dispose()
    {
        Timer? timer;

        lock (_gate)
        {
            timer = _timer;
            _timer = null;
        }

        timer?.Dispose();
    }
}
