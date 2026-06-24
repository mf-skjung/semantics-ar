# Slice 3 — Constitutional re-founding of the host-verifiable core — Design Notes

This records the rationale that the code cannot express. It is durable truth, not a
restatement of the source. It is subordinate to `CONSTITUTION.md`; where they disagree,
the Constitution governs. It builds on `SLICE1_DESIGN.md` and `SLICE2_DESIGN.md`.

## Scope delivered in this slice

Two coupled halves, both fully host-verifiable:

- **Decontamination & unification.** The entire `driver/` and `service/` trees — which
  were the *previous* design executing live (preservation, shadow store, journal,
  eviction, capacity limits, gate-blocking, key-less auto-block, self-trust PIDs, and a
  duplicate second conviction stack) — are removed. The portable `engine/` is now the
  sole cipher / conviction / recovery / keystore implementation.
- **The Gate, T-axis (Constitution IV.2).** A new freestanding engine module
  (`engine/include/sar_gate.h`, `engine/src/gate.c`) implementing the conditional-novelty
  cost filter that decides *whether a write is worth asking the Oracle about*, with an
  exhaustive host test suite (`tests/test_gate.c`).

## Why the whole Windows tree was deleted, not refactored

The Constitution states the old machinery is "gone — not refactored, gone" (closing
note; Parts I, III, V). Inspection confirmed the contamination was structural, not
cosmetic: `driver/src/dtaxis/write.c` ran the gate to a `…_TGATE_PRESERVE` verdict, called
`semantics_ar_preserve_range` (shadow store + journal), **blocked the write** with
`STATUS_ACCESS_DENIED` when preservation failed, and **auto-blocked** any
`is_confirmed_process` write — each a direct violation (I.1, III.1, IV.3.1, V.1.2). The
instance context *was* a shadow store with a `ShadowMaxBytes` capacity cap and a
`TrustedPids[]` self-trust table (III.3.1). `service/` carried a second oracle / battery /
keystore / mode-verify / cipher stack duplicating `engine/`.

Deleting only the forbidden files while keeping the shells would have left dangling
includes and half-built state (a cardinal failure of the operating contract). The chosen
disposition is a clean slate: the contaminated trees and their orphaned build plumbing
(`cmake/`, `scripts/windows/`, the `if(WIN32)` service wiring, the `.inf`/`.vcxproj`) are
removed; the Windows product is rebuilt **greenfield against the clean contract** in named
later slices. This is not data loss — every platform fact needed for that rebuild is
specified in the Constitution itself: the destruction-axis enumeration (IV.1.2), runtime
feature detection / compatibility (IX), the authenticated comm-port handshake and its
version contract (VII.3, and `SLICE1_DESIGN.md`), and the kernel battery location
(II.3, `SLICE1_DESIGN.md`).

### `battery_seam.h` was deleted, and where its contract now lives

`driver/include/battery_seam.h` defined the kernel-internal gate→battery seam (the
`SEMANTICS_AR_DESTRUCT_*` member tags, a fixed 256-byte `(P, C)` sample, and the
`novelty_slice_offset/length`). Nothing in the host-verifiable scope consumes it, so
keeping it would be dead code (forbidden by the operating contract and Constitution
IX.1.1). It is removed. Its one host-relevant element — the high-novelty slice the gate
emits — is now expressed cleanly as this slice's own `sar_gate_result_t`
(`novelty_off/novelty_len`). The greenfield capture slice will define the gate→battery
seam against the *live* `sar_gate` API (assembling the `(P, C)` sample, the
destruction-member tag, and the gate's novelty slice into a battery request, and binding
provenance for per-file keying), rather than against a header frozen ahead of its
consumer.

> This supersedes the "Deferred (later slices)" sections of `SLICE1_DESIGN.md` and the
> "Windows service/driver wiring … is out" note of `SLICE2_DESIGN.md`: those files no
> longer exist to be rewritten in place; the Windows realization is now a greenfield
> build.

## The Gate-T measure (Constitution IV.2)

The gate is a **cost filter, not a classifier** (IV.2.1). It emits *capture-candidate or
skip*; it never blocks (IV.3.1), never decides encryption/attack/direction, and uses no
entropy/content/file-type/ML signal (IV.2.3, IV.2.5). For a write that overwrites bytes of
an existing file, it compares the **written range** against the **original bytes at the
same offsets** (the destroyed pre-image), in fixed 256-byte blocks:

```
coverage(block) = (written non-overlapping 2-grams present in the original block's
                   distinct-2-gram set) / (written non-overlapping 2-gram count)
novelty(block)  = 1 - coverage
fire(block)     iff novelty >= theta            (theta = 0.10)
verdict(write)  = capture-candidate iff ANY block fires
novelty slice   = bounding span of the firing blocks (a coarse hint; see below)
```

The judgment is strictly conditional — *given the original, is the new content
predictable?* — never *is the new content random?*. `SAR_GATE_BLOCK_SIZE = 256` and
`theta = 10/100` are compile-time `[DESIGN]` points inside the derived safe band of
IV.2.4; they are **not** runtime-tunable (Constitution 0.5).

### [DECISION] Three refinements over the bare spec (validated by external research)

1. **Asymmetric n-gram counting.** The original block's 2-gram set is built from
   **overlapping** windows (up to 255 distinct entries — the richest possible membership
   set), while the written range is queried with **non-overlapping** pairs (`block_len/2`).
   The asymmetry is a free specificity gain (a richer original dictionary lowers novelty on
   benign edits, fewer needless asks) and it does **not** weaken the IV.2.4 concentration
   bound: that bound concerns the independence of the *written* pairs, which stay
   non-overlapping; the overlapping *build* carries no statistical claim, only set
   membership. It also makes the identity case exact: every written even-position pair is a
   member of the overlapping set, so `nc == nq`, novelty `0`, skip.

2. **Integer theta arithmetic.** `fire` is evaluated as
   `(n_query - n_covered) * THETA_DEN >= n_query * THETA_NUM`, with no floating point. This
   is the correct freestanding/kernel realization of `theta = 0.10` (the gate's intended
   home is the kernel write path, where float is unavailable). `n_query <= 128` per block,
   so the products fit a 32-bit int.

3. **Plain `memset` reset of the 8 KiB presence map per block.** A "clear-list" reset was
   considered and rejected: with up to ~255 scattered set positions in a 128-cache-line
   map, a selective clear touches roughly the whole map anyway, with worse locality and
   bookkeeping overhead. The straight `memset` is the correct default; any selective reset
   is at most a benchmark-gated micro-optimization, not adopted.

### Operational restatement (density)

"Any 256-byte block fires" is equivalent to "≥ ~26 fresh ciphertext bytes (~10%) appear in
some 256-byte window of the write." This framing makes both the coverage of real families
and the single blind spot precise.

### Coverage of real partial / intermittent encryption

Validated against current threat-intel parameters: every documented family's encrypted
spans cross theta at a 256-byte block — LockBit 2.0 (4 KB head), DarkSide (512 KB),
BlackMatter / Rhysida (1 MB), PLAY (2/3/5 chunks), BlackCat/ALPHV (Smart/Dot patterns),
Black Basta (64-byte cipher chunks), LockFile (16 bytes at 50% density → novelty ≈ 0.5).
The thinnest deployed margin is a Black Basta 64-byte run split evenly across a block
boundary: novelty ≈ 0.125 (vs theta = 0.10). So **256 bytes is adequate but not lavish**;
the single dial, if a future family pushes run density below ~10%, is to halve the block to
128 bytes (doubling the margin at ~2× cost). It is not changed now (no current family
requires it; no speculative generality).

### Confirmed limits — recorded, never engineered around (Constitution IV.2 / VIII.2)

Because the gate is a cost filter whose only failure mode is a false *skip* (a missed
capture opportunity, absorbed by cumulative-N and the in-memory scan capture path —
Constitution II.4.2, VIII.2) and which **cannot** produce a false block, the following are
honest limits, not design drivers:

- **On-disk alphabet ≤ 16 symbols (e.g. hex-armored ciphertext over a hex-armored
  original) AND a near-de-Bruijn original.** The 2-gram space collapses to ≤ 256 cells; a
  contrived original holding ~255 of them yields coverage ≈ 0.996, a skip. *Realistic*
  hex content holds only ~160 distinct pairs (novelty ≈ 0.37 → fires), and base32/base64
  have hard novelty floors of 0.751 / 0.938 → always fire. No documented file-encrypting
  family hex-armors its on-disk output (it doubles file size against the speed goal). No
  guard is added for this corner — adding one would engineer around a confirmed limit and
  add dead code for an unobserved case.
- **Format-preserving / low-expansion encryption** preserves the original's pair
  statistics by construction → low novelty → skip. Essentially never used by
  file-encrypting ransomware; theoretical only.
- **A bespoke per-block attack** that constrains ciphertext to the original block's own
  2-gram vocabulary can stay below theta for high-entropy originals while retaining ~50%
  of the block's entropy. Feasible but costly, unobserved, and — critically — a gate evasion
  only denies the conviction *accelerator*: the key is still resident in the writing
  process and remains capturable, so this reduces to the Constitution's confirmed limit
  (VIII.2), not a new hole.

### The novelty slice is a coarse hint

`sar_gate_result_t.novelty_off/novelty_len` report the bounding span of the firing blocks
within the written buffer (relative; the caller adds the write's file offset). For a
contiguous encrypted region this is exact; for strided encryption it over-covers. Finer
geometry is deliberately not computed here — the capture/recovery seam (the future capture
slice that fills the geometry source named in `SLICE2_DESIGN.md`) is its consumer, and
inventing a richer descriptor now would be speculative generality.

## Module layout and the freestanding boundary

`gate.c` is freestanding like the rest of `engine/` (caller buffers, no heap, no libc — it
uses `eng_mem.h`), so the same code drops into the kernel gate path unchanged. The 8 KiB
presence map is caller-provided scratch (`sar_gate_map_t`): too large for a kernel stack,
it is supplied from pool in the kernel and from static storage in the host tests. `gate.c`
is compiled under `-Wall -Wextra -Werror` plus `-Wconversion` (joining `battery.c` and
`keystore.c` — the bounds/index/length logic where `-Wconversion` is signal).

## What the tests prove

`tests/test_gate.c` (18 assertions, deterministic, no external dependencies):

- **identity write** → coverage 1 → skip (also confirms the overlapping-build superset).
- **uniform-random ciphertext over a low-entropy original** → novelty ≥ 0.97, fires (D1/M1).
- **k-byte benign point change** → novelty < theta, skip (D3 floor).
- **adversarial entropy** (high-entropy original overwritten by *unrelated* high-entropy
  bytes) → novelty ≥ 0.95, fires — the case an absolute-entropy gate would wrongly skip
  (IV.2.3).
- **intermittent slice not diluted** — a single 256-byte cipher block inside a 4096-byte
  write fires, and the novelty slice is located exactly at it (IV.2.4).
- **Black Basta-style 64-byte run** inside a block → novelty ≈ 0.25, fires (the thin-margin
  family).
- **base64-armored** ciphertext → fires (hard floor); **realistic hex-armored** ciphertext
  → fires (only the contrived corner misses).
- **edge cases** — a 100-byte short write fires; a 1-byte write skips (no 2-gram, and below
  the Oracle's minimum verifiable sample regardless).

They do not (and cannot, here) exercise the Windows write path, the live pre-image read, or
the kernel pool — all part of the greenfield deferrals below.

## Named deferrals (greenfield, against the clean contract)

Each is a finishable unit for a later slice; none is host-verifiable in this environment.

- **The Windows minifilter (driver) and user-mode service**, rebuilt from the Constitution:
  boot-start load, instance attach, the comm port with client authentication and the
  version handshake (VII.3), AUDIT/ENFORCE mode and the per-process whitelist (V).
- **The Gate D-axis** — the destruction-of-an-existing-original enumeration (IV.1.2:
  overwrite, truncate, delete, section write, rename-over, hardlink-replace, block-clone,
  destructive FSCTLs, with their legacy/`Ex` info-class pairs) feeding the portable T-gate
  delivered here.
- **The capture path** — register-first capture (XMM/`FXSAVE`) under a short IRP hold, the
  post-release memory-scan fallback, provenance binding for per-file keying, and the
  gate→battery seam that turns `sar_gate_result_t` into a battery request and fills the
  recovery geometry source.
- **Keystore self-protection** — kernel non-paged-pool residence, in-kernel MAC, and the
  TPM-sealed key with the external anchor (VII.1, VII.2); Slice 1 implemented the on-disk
  format and anchor *comparison* with a test key.
