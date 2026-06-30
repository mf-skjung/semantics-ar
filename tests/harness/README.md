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
```

All encrypt AES-256-CBC with a fixed IV; the captured key is recovered by the driver via
IV-location at conviction.

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

- **wipe_encryptor** `<dir> <count> <postHoldMs>` — the **pre-write-wipe evasion**: per
  file it encrypts in memory, then `BCryptDestroyKey` + `SecureZeroMemory` of the key
  object and raw key **before** writing the ciphertext, then stays alive `postHoldMs` so
  the deferred worker attaches a *live* process and still finds no key. Demonstrates the
  VIII.2 confirmed-limit (key destroyed before the snapshot) — the gap periodic memory
  sampling targets.

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
