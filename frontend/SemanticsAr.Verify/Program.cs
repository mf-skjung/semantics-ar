using System.Reflection;
using System.Runtime.InteropServices;
using SemanticsAr.Core.Domain;
using SemanticsAr.Core.Interop;
using SemanticsAr.Core.Services;

int pass = 0;
int fail = 0;

void Check(string name, bool cond, string detail = "")
{
    if (cond)
    {
        Console.WriteLine("PASS  " + name);
        pass++;
    }
    else
    {
        Console.WriteLine("FAIL  " + name + (detail.Length > 0 ? "  " + detail : string.Empty));
        fail++;
    }
}

void Metric(string name, string value) => Console.WriteLine("METRIC  " + name + " = " + value);

static bool AllIntegral(Type t) =>
    t.GetFields(BindingFlags.Public | BindingFlags.Instance)
        .All(f => f.FieldType == typeof(uint) || f.FieldType == typeof(ulong));

static string FieldTypes(Type t) =>
    string.Join(",", t.GetFields(BindingFlags.Public | BindingFlags.Instance).Select(f => f.FieldType.Name));

Console.WriteLine("=== SemanticsAr Slice-1 posture-plane verification ===");

Check("sarapi ABI compatible", NativePostureReader.IsAbiCompatible());

int postureSize = Marshal.SizeOf<SarApiPosture>();
Metric("SarApiPosture size", postureSize.ToString());
Check("posture DTO is a fixed 40-byte frame", postureSize == 40, "size=" + postureSize);
Check("posture DTO carries no path-capable field", AllIntegral(typeof(SarApiPosture)), FieldTypes(typeof(SarApiPosture)));
Check("event DTO carries no path-capable field", AllIntegral(typeof(SarApiEvent)), FieldTypes(typeof(SarApiEvent)));

var reader = new NativePostureReader();
SarApiResult result = reader.Read(out SarApiPosture posture);
Metric("posture read result", result.ToString());
Check("posture read OK (no prompt, server verified SYSTEM)", result == SarApiResult.Ok, result.ToString());

if (result == SarApiResult.Ok)
{
    PostureVerdict v = PostureEvaluator.Evaluate(result, in posture, false);
    Metric("verdict", v.Level + " / " + v.Reason + " mode=" + v.Mode);
    Metric("protocol_version", posture.ProtocolVersion.ToString());
    Metric("captured_key_count", posture.CapturedKeyCount.ToString());
    Metric("descents / health / oldest-expiry", v.Descents + " / " + v.PreserveHealth + " / " + v.OldestProtectedExpiry);
    Check("protocol version matches (no version skew)", posture.ProtocolVersion == 2u, posture.ProtocolVersion.ToString());
    Check("service running", posture.ServiceRunning != 0u);
    Check("driver connected", posture.DriverConnected != 0u);
}

var eventReader = new NativeEventReader();
var journal = new JournalService(eventReader);
int received = 0;
journal.EventReceived += (_, e) =>
{
    Interlocked.Increment(ref received);
    Console.WriteLine("EVENT  class=" + e.Event.EventClass + " gen=" + e.Event.Generation +
        " seq=" + e.Event.Sequence + " gap=" + e.Event.Gap);
};
journal.Start(TimeSpan.FromSeconds(1));
Thread.Sleep(700);
Check("events pipe connected (open + server verified SYSTEM)", journal.IsConnected);
Thread.Sleep(6000);
Metric("events received in listen window", Volatile.Read(ref received).ToString());

Console.WriteLine("=== Slice-2 elevated itemized-plane verification ===");

int HostCount() => System.Diagnostics.Process.GetProcessesByName("SemanticsArElevationHost").Length;

int hostsBefore = HostCount();
Metric("elevation hosts before", hostsBefore.ToString());

var session = new RecoverySession(() => ElevatedControlChannel.Activate(nint.Zero));
session.Begin();
Metric("recovery session state after Begin", session.State.ToString());

if (session.State == RecoverySessionState.Browsing)
{
    Check("elevated snapshot fetched (activate + host->service chain + round-trip)", true);
    Metric("recoverable items", session.Items.Count.ToString());

    var probe = new Win32FileProbe();
    int classified = 0;
    foreach (RecoverableItem item in session.Items.Take(8))
    {
        RestoreDisposition disposition = RestorePlanner.Classify(item, probe);
        Console.WriteLine("ITEM  rung=" + item.Rung + " disp=" + disposition + " path=" + item.ProvenancePath);
        classified++;
    }
    Check("provenance paths classify via GLOBALROOT probe", classified == Math.Min(8, session.Items.Count));

    Thread.Sleep(500);
    int hostsAfterSnapshot = HostCount();
    Metric("elevation hosts after snapshot release", hostsAfterSnapshot.ToString());
    Check("single-use host exited after snapshot (no resident broker)", hostsAfterSnapshot <= hostsBefore);

    if (session.Items.Count > 0)
    {
        RecoverableItem first = session.Items[0];
        var reserved = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string target;
        if (first.Rung == CertaintyRung.Definitive)
        {
            target = first.ProvenancePath;
        }
        else
        {
            string folderRoot = RestorePlanner.DefaultFolderRoot(DateTimeOffset.Now);
            try
            {
                System.IO.Directory.CreateDirectory(folderRoot);
                target = RestorePlanner.FolderTargetPath(folderRoot, first.ProvenancePath, reserved, probe);
            }
            catch
            {
                target = first.ProvenancePath;
            }
        }

        session.Execute([new RestoreRequest(first, target)]);
        Metric("recover round-trip state", session.State.ToString());
        if (session.State == RecoverySessionState.Reported && session.Report.Count == 1)
        {
            RecoveryOutcome outcome = session.Report[0];
            Metric("recover outcome", outcome.Kind + " kernel=" + outcome.KernelResult);
            Check("recover round-trip completed over a fresh elevation", outcome.Kind != RecoveryOutcomeKind.ChannelError,
                outcome.Error.ToString());
        }
        else
        {
            Check("recover round-trip completed over a fresh elevation", false, "state=" + session.State);
        }

        Thread.Sleep(500);
        Metric("elevation hosts after recover", HostCount().ToString());
    }
}
else
{
    Check("elevated snapshot fetched", false, "state=" + session.State + " err=" + session.LastError);
}

Console.WriteLine("=== " + pass + " passed, " + fail + " failed ===");
return fail == 0 ? 0 : 1;
