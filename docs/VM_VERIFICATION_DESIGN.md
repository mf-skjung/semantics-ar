# VM Verification — Full Redesign (verify FN=0 ≻ FP≈0 ≻ cost→min, completely, without unnecessary wall-clock)

Status: design, pre-implementation. Target harness: `scripts/vm_verify_coverage.ps1` (rewrite/extend) + new in-guest
tools under `tests/harness/`. Ground truth for observability is `tools/sarctl.c` (the control CLI) and the driver
comm-port it speaks to. This document is the artifact reviewed at code level before implementation.

## 0. What "verifiable structure" means here, and the one gap that blocks it

The proposition has three ranked goals. A VM test *completely* verifies it only if every goal has a **falsifiable**
in-guest observation:

| Goal | Falsifiable observation | Observable today? |
|---|---|---|
| **FN=0** (recoverable data-destruction transform) | every read-preceded target file ends **recovered-by-key ∨ recovered-by-preserve ∨ refused**; the **lost set is empty** | YES via `sarctl list`/`recover` (key) + `preserve-list`/`preserve-recover` (preserve) + on-disk==golden (refused) |
| **FP≈0** | benign corpora produce **zero blocks**, zero content alteration; AUDIT never blocks | YES (`benign_workload`, `benign_novel`; block count parsed) |
| **cost→min** | preservation bytes ≈ destroyed bytes (not file size); snapshot once/process; passive overhead bounded | PARTIAL (Phase D burden exists; proportionality not asserted) |

**Structural finding (the reconsideration the owner asked for):** the running system is *already* structured to be
verified — `sarctl` exposes **per-file recovery provenance** (which asset recovered a file) and per-region recover with
explicit success/decline, so the strengthened "zero-loss" classification needs **no new driver instrumentation**. The
code is verifiable as-is.

**The one blocking gap:** every in-guest transform tool (`destroyer_matrix.c`, `noninplace_destroyer.c`,
`partial_encryptor.c`, `benign_novel.c`) uses **AES-CBC via BCrypt only**. The two mechanisms this cycle added —
the σ-constant stream key-recovery scan (`stream_sigma_scan`, engine 2.1) and reduced-round Salsa recovery (the F0 fix
in `stream_block`) — have **no in-guest tool that exercises them**. Therefore FN=0 for the stream-cipher families
(ChaCha/Salsa/XChaCha/XSalsa — a large share of the real population) is **currently unverified end-to-end**. Closing
this is the redesign's core addition: a stream transform tool with residency variants.

## 1. Efficiency policy (avoid *unnecessary* wall-clock; pay *necessary* cost)

The dominant cost in the current harness is fixed `Start-Sleep` (22 s in Phase F, 6 s elsewhere) and any VM
restore/reboot. Policy:

1. **Poll, don't sleep.** Replace every fixed convict-wait sleep with a bounded poll loop
   `until (observable state reached) or (deadline)`, polling `sarctl list`/`events`/`preserve-list`. A conviction that
   lands in 3 s no longer costs 22 s; a genuine hang still fails at the deadline. This is the single biggest saving.
2. **One driver load for all non-state-destructive phases.** Mode and budget are runtime commands (`sarctl mode`,
   `sarctl budget`) — capacity/mode phases need **no restore**. Only Phase H (reboot persistence) needs a reboot.
   Target: **zero mid-run restores, one reboot** (Phase H last).
3. **Minimal sufficient N.** FN=0 is proven by *zero loss over the destruction matrix*, not by large N. Counts are
   sized for coverage of every seam × representative cipher, not for statistics. Residency hit-rate (a MEASURED metric)
   uses a modest N (e.g. 24) — enough to report a fraction, not to chase a tight CI.
4. **Reuse host-proven facts.** Cipher round-trip math is exhaustively host-verified (`test_recover` 97/0, incl. the
   reduced-round round-trips). The VM does **not** re-pay to prove cipher correctness; it pays only for what is
   in-kernel-only: deferred-snapshot residency, IRP-time fail-closed refusal, mmap per-region capture, reboot
   persistence.

## 2. New in-guest tool: `tests/harness/stream_transform.c`

Purpose: exercise the stream key-recovery path and the reduced-round recovery, and create the *residency* condition the
σ-scan depends on. It is an ordinary user process performing an in-place, invertible byte transform (XOR against a
keystream) over a corpus — the same shape the AES tools already have, with a stream keystream instead of a block cipher.

CLI: `stream_transform <chacha|salsa> <rounds:8|12|20> <resident|oneshot> <dir> <count> <sizeKB>`

Behavior:
- Reads each file, produces `ct[i] = pt[i] XOR keystream[i]`, writes it back in place (the read-preceded, in-place
  destruction seam), and prints `FILE=<path> OFF=0 LEN=<n>` per file for the harness to drive recovery.
- **`resident`**: the 64-byte cipher state (`"expand 32-byte k"` σ ‖ 32-byte key ‖ counter ‖ nonce, exactly the
  `chacha_block`/`salsa_block` input layout) lives in a **heap allocation** kept live across the whole corpus and a
  short hold window after the last write, then freed. This is the state-resident case the σ-scan is designed to hit.
- **`oneshot`**: the state is a stack local, used for one file, then `SecureZeroMemory`'d before the next — never
  co-resident across the hold window. This is the σ-miss case that must fall to preserve.
- The keystream generator is the engine's own `chacha_block`/`salsa_block` (compile `engine/ciphers/stream.c` into the
  tool) so the in-guest state layout is **byte-identical** to what the scan expects — no reimplementation drift.

Rounds arg drives the F0/F2 case directly: `salsa 12 resident` produces a Salsa20/12 keystream, convicted by
`try_salsa` at 12 rounds and recovered by the fixed `stream_block` at 12 rounds.

`build_harness.bat` auto-compiles any `tests/harness/*.c`; the only change is adding `engine\ciphers\stream.c` (and its
headers) to the tool's compile line, or `#include`-ing it.

## 3. Phase plan (▲ = new, ✎ = strengthened, • = retained as-is)

- • **Phase 0** — package present check + deploy + driver load + `fltmc` liveness assert (abort if absent, rule §6.4).
- • **A / A2 / P** — non-in-place FN, destruction-member matrix, phantom un-bypassability (AES, already green).
- ✎ **E** — in-place FN: add the **proportionality** assertion (preserved region bytes ≈ destroyed bytes, from
  `preserve-list` region length vs file size) to close the cost→min observable.
- • **F** — Oracle AES reused-key capture → recover-by-key → reconcile (poll-ified).
- ▲ **S (stream FN, verifies 2.1)** — `stream_transform chacha 20 resident` and `salsa 20 resident`; poll `sarctl list`
  until a `key_id` exists per file (deadline); `recover key_id file`; assert **every file recovered by KEY** and equals
  golden. This is the first end-to-end proof the stream families are captured, not just preserved.
- ▲ **S-res (residency MEASUREMENT, closes the recorded empirical unknown 2.1)** — run `resident` vs `oneshot` for
  chacha and salsa; **METRIC** = fraction of files with a `key_id` (σ-scan hit rate) per variant; **ASSERT** the
  `oneshot` (σ-miss) files are nonetheless recovered by **preserve** (FN=0 holds without the key). Reports the number
  the handoff says must be measured.
- ▲ **F2 (reduced-round, verifies the F0 fix in-kernel)** — `stream_transform salsa 12 resident`; assert convicted +
  recovered-by-key + equals golden. Host already proves the round math; this proves the whole driver→engine→recover
  chain on a live snapshot.
- ✎ **G (zero-loss, the central strengthening)** — see §4.
- ▲ **G-audit** — same footprint-exceeding transform under AUDIT: assert **zero blocks** (window slides; AUDIT is not a
  protective guarantee), the symmetry the handoff requires.
- • **B / C / C2** — benign FP=0 under AUDIT and ENFORCE (retained; poll-ified).
- • **H** — reboot persistence of staged regions (the one necessary reboot; run last).
- ✎ **D** — burden (median of 3); add the once-per-process snapshot assertion if observable via `events`.

## 4. Phase G — from "something blocked" to "zero files lost" (the core fix)

Current Phase G asserts `ovfBlocked > 0` (some op refused). That can pass while boundary files are silently lost. New
Phase G proves the FN=0 invariant directly, for **each** ENFORCE fail-closed seam
(in-place write, truncate/set-EOF, `FSCTL_SET_ZERO_DATA`, supersede/OVERWRITE_IF create):

```
# preconditions: ENFORCE; budget set so capacity is exceeded by the run
golden[f] = SHA256(f) for every target f          # before any transform
run seam-specific footprint-exceeding transform over the corpus
for each target f:
    onDisk = SHA256(f)
    if onDisk == golden[f]:                        # op refused, original intact
        class[f] = REFUSED
    elif (key_id = keystore_lookup(f)) and recover(key_id, f) and SHA256(f)==golden[f]:
        class[f] = RECOVERED_BY_KEY
    elif (reg = preserve_lookup(f)) and preserve-recover(f, reg.off, reg.len) and SHA256(f)==golden[f]:
        class[f] = RECOVERED_BY_PRESERVE
    else:
        class[f] = LOST
ASSERT  count(LOST) == 0                            # the FN=0 invariant, directly
METRIC  {REFUSED, RECOVERED_BY_KEY, RECOVERED_BY_PRESERVE} histogram
ASSERT  count(REFUSED) > 0                          # fail-closed actually engaged (not a trivially-empty run)
```

Soundness of the classifier (the property the verifier itself must have): a file is counted safe **only** after a
positive recovery to golden or an untouched-original check; there is no path that marks a modified, unrecovered file as
safe. `golden` is captured before the transform; `preserve-recover`/`recover` re-verify against the stored content tag
in-kernel (`sar_preserve_verify_extract` / `sar_recover_verify`), so a false "recovered" is rejected by the kernel
before the harness ever compares. The `REFUSED>0` assert guards against a vacuous pass where the transform never ran.

## 5. Residency reconciliation note (why S-res is honest, not a hit-rate we can inflate)

The σ-scan hits only when the full 64-byte state is co-resident in committed-private memory at snapshot time
(handoff 2.1). `resident` is engineered to satisfy that; `oneshot` is engineered to defeat it. We report both fractions
as measured facts and **do not** claim the `resident` number as the real-population rate — the real rate depends on how
a given family keeps its state, which is exactly the recorded unknown. The load-bearing assertion is the FN=0 one:
even at 0% σ-hit (`oneshot`), every file is recovered by preserve, so stream key-miss is never data loss inside the
envelope.

## 6. Open items this design closes (mapped to HANDOFF §4)

- A.1 (fail-closed prevents loss, all four seams) → Phase G zero-loss + G per-seam.
- A.2 (AUDIT unchanged) → Phase G-audit.
- A.3 (stream σ-scan recovers real keys + residency measured) → Phases S and S-res.
- A.4 (Salsa20/12 convicted+recovered) → Phase F2 (host F0 fix proven in-kernel).
- A.5 (no regression) → retained A/A2/P/E/F/EV/B/C/C2/H/D, poll-ified.

## 7. Verifier-soundness hardening (from FABLE5 code-level review of §4)

The §4 classifier's only success predicate was content equality `SHA256(f)==golden[f]`. That is strictly weaker than
"f is genuinely recoverable in isolation": anything that momentarily makes f's bytes equal golden passes. Five holes
follow, each with the fix folded into the harness. The unifying rule: **assert provenance and isolation, not just
content.**

1. **Barrier before classify (in-flight TOCTOU).** Classification must run only after (a) the transform process has
   *exited* and (b) the off-IRP capture/convict pipeline has *quiesced*. Reading f before the batch reaches it records a
   spurious `REFUSED`, then the batch corrupts it after the assert. Fix: `Wait-Process` on the transform, then poll
   `sarctl status`/`events` until in-flight capture counters are stable across two reads (a drain barrier), *then*
   classify. This also replaces the fixed `Start-Sleep` waits (the efficiency win and the correctness fix are the same
   change).

2. **Set closure (silent add/drop).** Assert the classified set is exactly the golden key set and of size N
   (`class.Keys == golden.Keys ∧ count==N`). A file deleted/renamed by a namespace seam, or created mid-run, must land
   in the tally as LOST or be explicitly accounted — never silently dropped from the loop.

3. **Telemetry guard, not content guard (vacuous-pass).** `REFUSED>0` inferred from content can pass with zero actual
   refusals (a file unchanged for an unrelated reason). Replace with the driver's own signal: assert the event journal
   shows `SAR_EVENT_CLASS_BLOCK_CAPACITY` events > 0 (`sarctl events`), and reconcile the blocked-originator set against
   the REFUSED set (`REFUSED ⊇ files the journal reports blocked`). This proves the fail-closed path *engaged*, not just
   that some bytes happen to match.

4. **Identity-bound recovery (aliased content / aliased key).** "Recovered by key" must use a key whose record's
   provenance path **is that file** — Phase F's existing `key_id`↔filename match in `sarctl list` is the right
   discipline; keep it. Content equality alone is ambiguous whenever two files share golden content: a key/preserved
   region from file X could reproduce file Y's golden without Y ever being captured. For the FN=0 *recoverability*
   assertion this ambiguity is benign (Y genuinely is recoverable — key reuse recovering every file under the key is
   II.4.2 by design). But it is **not** benign for the residency metric — see §7a.

5. **Durable read.** After a keyed/preserve recover writes bytes back, re-hash from the durable path (the recover path
   does a metadata-preserving atomic replace, so a fresh `ReadAllBytes` already crosses the replace; Phase H
   independently proves reboot persistence). Where a recover commits through cache, `FlushFileBuffers` before the
   compare. Low risk here but cheap to enforce.

### 7a. Residency metric must be per-process and distinct-keyed (the sharp consequence of hole #4)

The deferred snapshot is **once per process** (`ctx->scanned`): a single process streaming N files under one resident
state produces exactly one σ-scan opportunity, and one capture covers all N files under that key. So "fraction of files
recovered by key" would read ~100% off a *single* σ-hit and is a dishonest residency number. The honest metric is
**per independent process**:

- Phase S-res launches **M independent `stream_transform` processes**, each with a **distinct key/nonce** and a small
  private corpus, half `resident` and half `oneshot`.
- **METRIC** = fraction of *processes* whose distinct key appears in `sarctl list` (σ-scan hit rate), reported
  separately for `resident` and `oneshot`.
- **ASSERT** (FN=0, load-bearing) every `oneshot` process's files — σ-miss by construction — are nonetheless recovered
  by **preserve** to golden. σ-miss is never data loss inside the envelope.

Distinct keys also remove hole #4's ambiguity from the metric: a captured key attributes to exactly one process, so the
hit fraction is real, not inflated by key reuse across the corpus.
```
