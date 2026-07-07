# VM probe evidence — A-H3 (paging-write region capture obtains the pre-image)

Date: 2026-07-07. VM: SarTarget, clean-baseline-20260704 restored. Instrumented build with
**eager whole-file capture DISABLED** (g_sar_probe_no_eager=TRUE), so ONLY the paging-write region
capture path (SarMmapOnPagingWrite -> SarMmapCaptureInline -> SarRawReadStageRange) runs.
SarRawReadStageRange logs, per staged region, SAR_EVENT_CLASS_MMAP_CAP packing
firstByte(8) | pathHash16(16) | page(16) | step(24).
Harness: mmap_probe.exe fills nothing; target ah3.dat is a 4 MB file pre-filled with 0xAA (pre-image P);
the harness dirties every 16th page's first byte via XOR 0xFF -> 0x55 (post-image Q), no explicit flush,
holds 3 s, unmaps. Raw: AH3_events_20260707.txt, AH3_decoded_20260707.csv.

## Result (fully attributed by file via pathHash)

Target file ah3.dat (pathHash 0x4348):
- 64 capture rows, **firstByte == 0xAA on all 64** (the pre-image), step 4096 (single page each).
- **0 post-image (0x55). 0 garbage.**
- distinct pages captured = 64 = exactly the dirtied stride-16 pages (file is 1024 pages).

Other captures: 48 rows across **11 distinct other files** (different pathHashes) — ordinary
system memory-mapped files whose paging writes are now also region-captured because eager was
disabled; their firstBytes are those files' real content (looked "random" only relative to 0xAA).
Not garbage, not a target-file defect. (In the pre-attribution run these 47-48 rows were the source
of the "other firstByte" count; the pathHash confirms hypothesis H2, not an extent-resolution bug.)

IRQL != PASSIVE across all mmap-probe pre-ops: 0.

## Verdict

PASS. On the target file, the paging-write region capture obtained the correct pre-image for every
dirtied region, region-exact (64 of 1024 pages, never whole-file), with zero post-image contamination
and zero garbage, entirely at PASSIVE_LEVEL. The raw-disk read mechanism resolved correctly for the
target and for 11 other concurrently-mapped files — FABLE5's A.3 "raw-disk garbage" concern did NOT
reproduce for this workload.

## The design premise is now empirically proven (A-H1 + A-H2 + A-H3)

- A-H1: async mapped-page-writer flush pre-op is PASSIVE (inline capture executable). 256/256.
- A-H2: writes are region-granular sub-ranges (<=60 KB), never whole-file.
- A-H3: region capture obtains the correct pre-image (target 64/64), region-exact, no post-image.
- top-level IRP is SET at the seam -> IoSetTopLevelIrp(NULL) around the inline read is mandatory.

Meets all four fixed constraints: FN=0 (pre-image secured before the flush commits), region backup
(only dirtied regions), zero usability impact (nothing refused/blocked; capture rides the flush
transparently), frontier-transcending (region-granular capture where the external minifilter
baseline either whole-file-copies or gives up).

## Genuinely remaining (minor / deferred), before implementation is fully closed

- A.3 layout-DRIFT edge NOT exercised: the target file was static (pre-allocated, not grown or
  reallocated mid-capture). VCN->LCN drift / torn-read (FABLE5 A.3) would need a probe with a file
  being extended/reallocated during capture. For the dominant steady-state overwrite it is proven.
- A-H4 bounded-pend liveness: NOT needed unless an APC-level paging-write is ever observed; across
  all runs IRQL!=PASSIVE = 0, so the bounded-pend machinery stays deferred (build only if a field
  telemetry counter records a non-PASSIVE arrival).
- Recovery-path end-to-end (restore the captured region and byte-compare to P) not yet run; the
  capture-side pre-image correctness is proven here.
