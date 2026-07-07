# VM probe evidence — A-H1 (IRQL) + A-H2 (region granularity)

Date: 2026-07-07. VM: SarTarget, clean-baseline-20260704 restored. Instrumented driver
(SarMmapOnPagingWrite emits SAR_EVENT_CLASS_MMAP_PROBE packing irql|toplevel|sync|want|offsetPage|length).
Harness: mmap_probe.exe (dirties 64 scattered pages, stride 16, of a 4 MB file; modes lazy/explicit/pressure).
Raw: AH1_events_{lazy,explicit,pressure}_20260707.txt, AH1_events_all_20260707.txt, AH1_decoded_20260707.csv.

## A-H1 — IRQL of the memory-mapped paging-write pre-op

Result: **256/256 rows PASSIVE_LEVEL (irql=0). Zero APC, zero DISPATCH.**

Both flavors observed and both PASSIVE:
- sync=1 (IRP_SYNCHRONOUS_PAGING_IO — FlushViewOfFile / UnmapViewOfFile section flush), length 4096: PASSIVE.
- sync=0 (IRP_PAGING_IO only — asynchronous mapped-page-writer / lazy writeback), length 61440 coalesced: **PASSIVE**.

Verdict: CONFIRMED. The asynchronous mapped-page-writer flush — the load-bearing case (fix ③;
FABLE5 A.1/A.5) — is entered at PASSIVE_LEVEL. Synchronous inline FS-coherent capture is executable
at this seam. FABLE5's inline construction is not blocked by IRQL for this workload.

## A-H2 — region granularity (no whole-file dump)

Result: **every paging write names a sub-range, never the whole file.**
- sync=1 writes: exactly the dirtied pages (stride-16 offsets: 48,64,80,...,1008), 4096 B each.
- sync=0 writes: coalesced runs of up to 61440 B (15 pages) around dirtied clusters.
- Max single paging-write span observed: 61440 B (60 KB).

Verdict: CONFIRMED. The 4 MB file is never emitted as one write. Region-granular capture is real;
per-stream reserved capture buffer of ~64 KB covers the max clustered span.

## New finding — top-level IRP is SET at the pre-op (topl=1 for all 256 rows)

Contrary to the assumption that top-level is NULL above NTFS, IoGetTopLevelIrp() != NULL for every
observed paging write. Our seam runs in a nested context.

Implications:
- The inline pre-image read MUST save and null the top-level IRP (IoSetTopLevelIrp(NULL)) before the
  read, or NTFS may treat it as a recursive modified-write. FABLE5's save/null step is therefore
  mandatory, not optional — empirically validated.
- A-H3 must confirm the FS-coherent shadow read is safe from this nested, top-level-set context.

## Honest limits of THIS run (still pending before implementation gate)

- No APC-level paging write was reproduced. The pressure mode (768 MB commit+touch) did not trigger a
  fault-collided/trim write at APC. So the APC sub-path FABLE5 flagged was NOT exercised; the
  bounded-pend fallback remains designed-but-unverified. Reproducing APC likely needs Driver Verifier
  Force IRQL and/or heavier, sustained pressure.
- Driver Verifier (Force IRQL / Low Resources / Special Pool) was NOT enabled this run. Deadlock/IRQL
  stressing is not yet done.
- A-H3 (inline shadow FS-coherent read returns the pre-image and does not deadlock) NOT yet tested —
  this run only measured IRQL/region at the seam, it did not perform the capture read.
- A-H4 (bounded-pend liveness under pressure) NOT yet tested.

## Gate status

- A-H1: PASS (PASSIVE confirmed incl. async).
- A-H2: PASS (region-granular confirmed; max span 60 KB).
- A-H3, A-H4, APC-path stress: PENDING — required before Subsystem A implementation begins.
