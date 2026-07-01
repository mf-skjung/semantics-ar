# Live-kernel test & measurement harnesses

Standalone user-mode CNG encryptors used to exercise and measure the driver on a live
Windows VM (capture / recovery / ENFORCE block / load / evasion). They are **not** part
of the CMake build — they are controlled "attackers" driven from the host over
PowerShell Direct against the deployed `semantics_ar` minifilter.

## Build

x64 Native Tools prompt (or `vcvars64.bat`), static CRT so no runtime DLL is needed on
the target:

```
cl /O2 /MT partial_encryptor.c   /link bcrypt.lib
cl /O2 /MT ransom_sim.c          /link bcrypt.lib
cl /O2 /MT splitter.c            /link bcrypt.lib
cl /O2 /MT wipe_encryptor.c      /link bcrypt.lib
cl /O2 /MT noninplace_destroyer.c /link bcrypt.lib
cl /O2 /MT destroyer_matrix.c    /link bcrypt.lib
cl /O2 /MT benign_novel.c        /link bcrypt.lib
cl /O2 /MT phantom_enum.c        /link kernel32.lib
```

The encryptor harnesses encrypt AES-256-CBC with a fixed IV; the captured key is recovered
by the driver via IV-location at conviction. `phantom_enum` is not an encryptor — it is a
read-only enumeration prober (no bcrypt), the one harness that attacks the **phantom
decoy-visibility** surface rather than the destruction/capture surface.

## Harnesses

- **partial_encryptor** `<file> [headKB]` — encrypts only the head (default 8 KB) of a
  file in place, leaving the tail plaintext. Exercises **partial/intermittent-encryption
  capture + range-scoped recovery**: the driver records one encrypted region and recovery
  restores it while leaving the plaintext tail byte-identical.

- **ransom_sim** `<dir> <count> <perfile|reuse> <holdms> <threads>` — multi-threaded
  single-process full-file encryptor. `perfile` = a fresh random key per file (transient,
  capture-hostile), `reuse` = one shared key (resident, capture-friendly). The capture-rate
  load test (single process).

- **splitter** `run <dir> <total> <batch> <parallel>` (worker mode `worker <dir> <start>
  <count>` is spawned internally) — spreads encryption across many short-lived worker
  processes to evade the **per-process ENFORCE block**. Quantifies block degradation vs
  split count; the identity-independent capture floor still holds.

- **noninplace_destroyer** `<newdelete|renameover|truncate|mmap|setzero> <dir> <count> [sizeKB]`
  — the **non-in-place destruction paths**, the one family the four in-place harnesses above do
  not exercise. Per file it creates a known-plaintext victim, reads it (arming the read→destroy
  gate), then destroys the original by: writing `<file>.locked` and `DeleteFile` (newdelete);
  `MoveFileEx(REPLACE_EXISTING)` of a ciphertext temp over it (renameover); writing a ciphertext
  head then `SetEndOfFile` to drop the tail (truncate); a `PAGE_READWRITE` section map overwritten
  and flushed (mmap); or `FSCTL_SET_ZERO_DATA` over the body (setzero). Each must leave the
  pre-destruction original recoverable from the preservation store (probation hold). Verify by
  enumerating the preserve store / running operator restore after each mode and diffing against the
  known plaintext; under ENFORCE, a convicted actor's holds are promoted and survive capacity slide.

- **destroyer_matrix** `<mode> <dir> <count> [sizeKB]` — the **gate-completeness** matrix: the
  destruction primitives `noninplace_destroyer` does not cover, so every user-mode path to data
  loss is exercised, not just the common five. Modes: `noncached` (`FILE_FLAG_NO_BUFFERING`
  sector-aligned write), `disposeex` (`FileDispositionInfoEx` delete), `allocshrink`
  (`FileAllocationInfo` shrink below EOF), `setsparse` (`FSCTL_SET_SPARSE` + zero-range), `trim`
  (`FSCTL_FILE_LEVEL_TRIM`), `blockclone` (`FSCTL_DUPLICATE_EXTENTS_TO_FILE`), `linkreplace`
  (NT `FileLinkInformation` hardlink replace-if-exists, via `NtSetInformationFile`), plus the
  real-family shapes — `mmapclose` (**WastedLocker**: map, write the view, then close the file
  handle *before* unmapping so the dirty pages are flushed by the Cache Manager lazy writer in
  PID 4 — the case the section-acquire capture exists for), `intermittent` (**LockFile**: modify
  every other 16 bytes in place, keeping entropy near-benign so an entropy-based detector would
  miss it — proves the gate keys on the *operation*, not chi²), `copydelete` (**Peeler**
  copy-encrypt-delete: Dharma/Sage) and `preoverwrite` (**Locky** rename-then-overwrite). Each
  arms the read→destroy gate on a known-plaintext victim, then reports
  `PERFORMED / UNAVAILABLE / ERRORS`: **UNAVAILABLE** distinguishes a primitive the volume does
  not support (file-level TRIM and block-clone are unsupported on plain-NTFS virtual disks — an
  honest reachability gap, not a gate miss) from a real harness error. Verify by confirming each
  *performed* mode leaves the original recoverable from the preserve store; a performed
  destruction with no capture/preservation is the defect.

- **benign_novel** `<dir> <count> [sizeKB]` — the **false-positive / availability** corpus: the
  benign workloads that most *resemble* ransomware, run at speed, to prove ENFORCE does not block
  legitimate work. Per iteration it runs five high-novelty classes — `compress_inplace` (overwrite
  a file in place with high-entropy content, mimicking a recompressor: the entropy-detector
  stressor), `vacuum` (SQLite-style build-new + atomic rename-over-original + delete), `transcode`
  (copy-transform-delete, the benign twin of Peeler copy-encrypt-delete), `encrypt_tool` (a
  genuine user-initiated AES encryptor writing `.locked` and deleting the source — the hardest FP
  case, which the Constitution admits the gate captures while keeping data available), and
  `bulk_churn` (mass move/copy/delete, the backup/sync/build pattern). It reports per-class
  `ok/blk` and `BLOCKED_TOTAL`. The pass is **`BLOCKED_TOTAL=0` under ENFORCE at default budget** —
  capture is acceptable (availability is preserved via probation), but a *blocked* benign
  operation is a false positive and the defect. Measure specificity per class, not blended.

- **phantom_enum** `<dir>` — the **phantom un-bypassability** prober, the only harness that
  attacks decoy *visibility* instead of data destruction. For a target directory it enumerates
  the same directory by every route an attacker has and checks they agree: all nine
  `NtQueryDirectoryFileEx` info classes (Directory / Full / Both / Names / IdFull / IdBoth /
  IdExtd / IdExtdBoth / IdGlobalTx), the `FSCTL_ENUM_USN_DATA` MFT-order listing, and
  `OpenFileById` on each phantom's synthetic FileReferenceNumber. It takes the id-bearing IdBoth
  enumeration as the reference phantom set (entries whose FRN carries `0xFFFF000000000000`), then
  asserts every phantom name reappears in every other method with the **same** FRN — a
  *metamorphic* oracle: there is no single "correct" listing, but any phantom present in one
  surface and absent (or FRN-inconsistent) in another is a cross-reference **tell** that unmasks
  the decoys. `CONSISTENT=YES tells=0` (exit 0) is the pass; any tell is the defect. USN and
  open-by-id require an admin/volume handle (they degrade to a logged `ERR` without one). This is
  the first live-kernel exercise of the open-by-FileId reparse and USN-injection paths.

- **wipe_encryptor** `<dir> <count> <postHoldMs>` — the **pre-write-wipe evasion**: per
  file it encrypts in memory, then `BCryptDestroyKey` + `SecureZeroMemory` of the key
  object and raw key **before** writing the ciphertext, then stays alive `postHoldMs` so
  the deferred worker attaches a *live* process and still finds no key. Demonstrates the
  VIII.2 confirmed-limit (key destroyed before the snapshot) — the gap periodic memory
  sampling targets.

## Running the defect-seeking pass

`scripts\vm_verify_coverage.ps1` drives these harnesses against the deployed filter. Build them
first with `scripts\build_harness.bat` (compiles every `*.c` here into `build_harness\`); the
verify script deploys and runs them. Its phases map to the verification tracks:

- **Track 2 — gate completeness** (`Phase A2`): `destroyer_matrix` over every destruction member;
  each *performed* primitive must leave the victim restorable. UNAVAILABLE primitives are skipped
  as honest reachability gaps, not failures.
- **Track 3 — phantom un-bypassability** (`Phase P`): `phantom_enum`'s metamorphic consistency
  across all enumeration paths — the first live exercise of open-by-FileId (3-A) and USN
  injection (3-B).
- **Track 4 — false-positive / availability** (`Phase C2`): `benign_novel` under ENFORCE — zero
  blocked benign operations.
- **Regression baseline**: the pre-existing `Phase A/E/F/B/C/G/H/D` assertions run alongside, so a
  new-track defect is never confused with a regression in already-verified behaviour.

Two things this pass depends on that are **not** in the script, because they are run-environment
setup, not measurement — do them on the VM before a serious pass:

1. **Instrumentation.** Run the whole pass with the driver under **Driver Verifier**: `/standard`
   plus **File System Filter Verification** (catches leaked minifilter contexts / NameInformation /
   `FLT_CALLBACK_DATA` refcounts at unload), **Special Pool**, **Force IRQL Checking**, and **Low
   Resources Simulation** (proves the allocation-failure paths decline instead of bugchecking).
   Latent memory/IRQL/leak bugs then surface as an immediate, diagnosable bugcheck — route it
   through `docs/DEBUGGING.md` (dedicated dump + `!fltkd`/`!verifier 3 semantics_ar.sys`). Bake it
   into the test snapshot so every run inherits it.
2. **Negative control — prove the pass can go red before trusting a green.** Coverage is easily
   fooled; a suite that stays green on a seeded defect is confirmation theater. Deliberately break
   one thing (disable a gate hook, weaken a phantom's name derivation, skip one member's capture)
   and confirm the matching `Phase A2`/`Phase P` assertion **fails**. If it does not, the oracle is
   broken — fix the test, not the driver. Restore the clean driver before the real pass.

The honest metric is not a headline "all caught": it is *which* members are provably captured,
*which* are unreachable on this volume, that the phantom set is identical across every enumeration
surface, and that no benign class is blocked. Record what a run actually found under Findings —
a surfaced defect is the high-value result, not the failure.

## Findings (2026-06-26, slow Win11 VM; absolute timing is VM-pessimistic)

- **Stability**: no deadlock/hang/bugcheck and paged-pool peak < +42 MB at 500 files ×
  8 threads and 250 concurrent processes.
- **ENFORCE**: net permanent loss 0 in realistic scenarios — a single fast process is
  blocked after ~2 files; process-splitting evades the block (prevented 99.8% → 33% as
  split rises) but **not** capture (`captured ≳ damaged` always → all damaged recoverable),
  empirically vindicating identity-independent capture.
- **Heap churn**: a single fast process churns its own heap (write-grab capture falls to
  ~6.6% at 500 files); splitting *reduces* per-heap churn and *raises* capture.
- **pre-write-wipe**: 0% capture (AUDIT) and total compromise in ENFORCE (0 prevented /
  100% damaged / 0 captured) — the one decisive gap, and the documented VIII.2 edge.

See `docs/HANDOFF.md` §4 item 5 for the periodic-memory-sampling follow-up these measure.
