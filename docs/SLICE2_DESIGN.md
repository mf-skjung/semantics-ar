# Slice 2 — Recovery (Part III) — Design Notes

This records the rationale that the code cannot express. It is durable truth, not a
restatement of the source. It is subordinate to `CONSTITUTION.md`; where they disagree,
the Constitution governs. It builds on `SLICE1_DESIGN.md`.

## Scope delivered in this slice

The portable, host-verifiable **recovery core** and the contract extension it needs:

- **Contract extension** (`common/include/semantics_ar/keystore_format.h`,
  `engine/include/conviction_engine.h`): the engine now stops discarding the per-file
  IV/nonce it already recovers at conviction and persists it as a "parameter needed to
  use the key" (Constitution II.5.1). `sar_verdict_t` and the keystore record gain
  `iv[24]`, `iv_length`, `ctr_layout_tag`. The keystore format is bumped to **v2**.
- **IV/nonce emission** (`engine/src/modes.c`, `engine/src/battery.c`,
  `engine/src/ctr_layout.h`): the forward-conviction path now emits the recovered IV
  material into the verdict.
- **The decrypt transform** (`engine/src/recover.c`, `engine/include/sar_recover.h`):
  freestanding, caller-buffer, geometry-agnostic recovery of the encrypted byte ranges
  of a file from `{key, iv, mode_params, ctr_layout_tag}` + the on-disk ciphertext.
  Includes the cipher factory, per-mode decrypt, geometry expansion, the forward-relation
  confirm (the Oracle's check, reused at recovery), and the A2 IV locator.
- **File-level orchestration** (`engine/host/recover_file.c`, `…/sar_recover_file.h`):
  the libc-dependent crash-safe writeback (temp on same dir → fsync → atomic rename →
  fsync dir) and the no-clobber guard. Compiled as a separate host library
  (`semantics_ar_recovery_host`), kept out of the freestanding verify path.

Windows service/driver wiring (comm-port RECOVERY_REQUEST/DONE, volume enumeration,
ReFS/same-volume specifics, residue clearing) is **out** — a later slice. Family-specific
geometry parsers are **out** (one documented extension point only). Conviction-side cipher
variant coverage is **out** (recovery covers exactly the engine's current convicted set,
plus the CTR/stream counter-origin refinement).

## Why recovery is decryption of the on-disk ciphertext (Constitution III.1)

There is no preserved original, no shadow store, no backup. Recovery decrypts the
attacker's own on-disk ciphertext with the captured key. The keystore is the sole
persistent recovery asset; everything in this slice serves "apply a captured key to the
ciphertext it produced." Nothing here hedges against the Oracle being wrong.

## [DECISION] Stop discarding the IV — what is persisted, and where

The Slice 1 note "IV / nonce is not an engine concern" is **superseded** by this slice for
the *recovery-time* use; it remains true that conviction does not *need* the IV. The IV is
now persisted because *recovery* needs it. Per mode:

- **CTR**: the 16- or 8-byte counter block, **normalized to file offset 0**
  (`origin = sampled_counter_block − file_block_index`, under the matched layout), plus a
  1-byte `ctr_layout_tag`. A bare IV is insufficient to regenerate a CTR keystream: the
  counter discipline (which bytes are the counter, which endianness) is required, so the
  tag is mandatory. `iv_length` = block size (16 or 8).
- **OFB**: the IV, recovered by backward-chaining `O_{i-1} = D_k(O_i)` from the sampled
  keystream block down to `O_0`, then `IV = D_k(O_0)`. Deterministic; assumes full-block
  (OFB-128/-64) feedback, which is what the engine's OFB verifier already assumes.
- **CBC / CFB**: when the conviction sample includes file block 0 (`file_offset == 0`),
  the IV is computed from the **captured** `(P_0, C_0)` with no content assumption
  (CBC: `IV = D_k(C_0) ⊕ P_0`; CFB: `IV = D_k(P_0 ⊕ C_0)`). At a mid-file-only catch,
  `iv_length = 0` (IV not captured).
- **ChaCha / Salsa / XChaCha / XSalsa**: the recovered per-file nonce (12 / 8 / 24 / 24
  bytes). For X-variants the **original 24-byte nonce** is stored so recovery can re-run
  the standard `HChaCha20`/`HSalsa20` subkey derivation; the reported key stays the
  32-byte master.
- **ECB / RC4-short-key**: no IV (`iv_length = 0`); RC4 re-runs KSA+PRGA from the key.
- **XTS**: unchanged — both keys are already in `key[64]`, the data-unit is already in
  `mode_params`.

### [DECISION] `mode_params` reused for stream parameters

For `SAR_MODE_STREAM` (ChaCha/Salsa families) `mode_params` encodes
`(rounds << 8) | counter_origin`:

- **counter origin ∈ {0, 1}**: RFC 8439 permits either; assuming 0 only would miss
  counter-origin-1 families entirely. Conviction tries both and persists which matched.
- **rounds**: the engine's stream verifier already tries reduced-round ChaCha (8/12/20);
  persisting the matched round count makes recovery exact rather than silently assuming
  20. This is *not* a new conviction variant — it records which existing trial matched.

`ctr_layout_tag` is `0` for non-CTR modes. For CTR, `mode_params` is `0`. For XTS,
`mode_params` is the data unit (512 or 4096) and the stream encoding does not apply.

### [DECISION] The CTR layout table is the single source of truth

`engine/src/ctr_layout.h` holds the 8 counter layouts as `{offset, width, endianness,
block_size}` and is shared by the conviction emit (`generic_ctr`) and the recovery
keystream regenerator. The table is exactly the set the Slice 1 hand-rolled `generic_ctr`
branches already detected — it is a refactor, **not** an expansion of convicted variants.
The 8 layouts are: 16-byte block — `{12,4,BE}`, `{8,8,LE}`, `{8,8,BE}`, `{14,2,BE}`,
`{0,8,LE}`; 8-byte block — `{4,4,BE}`, `{0,8,LE}`, `{0,8,BE}`. Tag = table index + 1.

> **Layout ambiguity is harmless.** When a file's high counter bytes are zero (the common
> case for the first thousands of blocks), several layouts match the `+1` relation at the
> sample (e.g. a 96-bit-nonce/32-bit-BE counter is indistinguishable from a full 64-bit-BE
> counter while the high bytes stay zero). Conviction reports the first match; recovery
> under that layout regenerates an **identical** keystream for every block index the file
> actually uses (the field increment agrees until the narrower field overflows, which a
> real file's block count never reaches). Recovery is therefore correct regardless of which
> ambiguous layout is recorded.

## [DECISION] Keystore v2 is the format; no v1 migration

`SEMANTICS_AR_KEYSTORE_VERSION` is bumped to 2 and v2 **is** the on-disk format. v1 never
shipped, so there is no v1→v2 migration path — `sar_keystore_verify` continues to reject
any non-2 `protocol_version` with `SAR_KS_BAD_VERSION` (no silent misparse). The new
`iv[24] / iv_length / ctr_layout_tag` fields sit inside `record_i` and are therefore
covered by the existing record-MAC chain automatically; `keystore.c` only had to copy them
in `sar_keystore_record_init`. The keystore tests re-confirm append+dedup and all four
tamper attacks under v2, and add (v) an edit *inside the IV field* is caught by the record
MAC and (vi) a non-2 version is rejected.

## [DECISION] Slice-aware recovery — geometry is a recovery-time input, never stored

Modern families encrypt only parts of a file (head, dot/stride, smart/percent). Blindly
decrypting the whole file corrupts the skipped plaintext regions. The recovery core is
**geometry-agnostic**: it takes a canonical `sar_geometry_t` descriptor as an explicit
input, decrypts **only** the encrypted byte ranges, and leaves the rest byte-for-byte
(`pt` is seeded from `ct`, then only encrypted ranges are overwritten).

Geometry is a whole-file property, unknowable from a single conviction write, so it is
**not** in the keystore record. The canonical descriptor is the union of the real-world
taxonomy — `{full, head, stride, percent, explicit}` plus a per-chunk counter policy
`{continuous | reset_per_chunk}` and the chunk stride — and expands (with the file size)
to the encrypted-range set. For keystream modes the policy selects the keystream index:
`continuous` uses the absolute file offset; `reset_per_chunk` restarts the keystream at
each encrypted chunk.

Geometry sourcing is a defined input contract; this slice implements what is host-testable
(synthetic descriptors and an operator-supplied descriptor) and leaves the native source —
the capture-time novelty slices carried in `battery_seam.h`
(`novelty_slice_offset/length`) — to the future capture slice that produces them. The
"unknown geometry" path declines honestly. There are **zero** family-specific footer/JSON
parsers here: one documented extension point, `sar_geometry_mapper_fn` (family blob →
canonical descriptor), is declared and left unimplemented with its contract.

## [DECISION] Key↔file association and the forward-relation confirm

Two paths, both content-free:

- **Provenance-direct**: under per-file keying the record's provenance path *is* the file;
  no cryptographic re-check is needed (the binding was made at capture). This is the
  primary path; `sar_recover_buffer` decrypts directly.
- **Forward-relation confirm**: for a shared key applied to an operator-supplied target
  set — and for the no-clobber guard — a key "matches" a file only when the **forward
  Oracle relation** `Encrypt_K(P) == C` holds over a supplied known `(P, C)` sample. This
  is the *same* check the Oracle uses, reused at recovery. It is content-free: `P` is the
  operator-/capture-supplied true plaintext, never guessed and never an entropy/file-type
  heuristic.

> **Why a known `(P, C)` sample is necessary and sufficient.** Re-encrypting a
> just-decrypted buffer is tautological for every mode (encrypt and decrypt are inverses;
> keystream XOR is involutive), so a key cannot be validated against the ciphertext alone.
> An **independent** true `P` breaks the tautology: for any mode, a wrong key fails
> `Encrypt_K(P) == C` with probability ~2⁻¹²⁸ per block. This is exactly why the Oracle
> captures `(P, C)`, and why the no-clobber guard needs that sample. Without a sample,
> association falls back to provenance trust (and the writeback proceeds on that trust).

The no-clobber guarantee follows: `sar_recover_file` runs the confirm *before* it creates
any temp file; a wrong key returns `SAR_RECOVER_DECLINED_MISMATCH` and the target is never
touched. Self-conviction is prevented purely by direction (Constitution III.3.1): recovery
writes plaintext over ciphertext (the reverse direction), which the forward-only Oracle
cannot convict. No self-trust/identity flag exists (it would reintroduce the identity
dependence Part VI forbids).

## [DECISION] A2 — locate the IV on disk, verified only by the cryptographic relation

Where the IV is not persisted (CBC/CFB block 0 at a mid-file catch, or other files under a
shared key), the locator scans the HEAD and TAIL regions (256 bytes each) at widths
{8, 12, 16, 24} and verifies each candidate **only** by the forward relation under the
captured key — never by plaintext content/entropy. For CTR it additionally sweeps the 8
layout tags. The locator uses a **strict-IV** form of the relation: the IV-independent
key-confirmation fallback (the CBC/CFB block-1 check, valid for the match/no-clobber use)
is disabled, so a candidate is accepted only if it actually exercises the IV — otherwise a
too-short candidate would be "confirmed" by a check that ignores the IV. False-positive
probability is ~2⁻¹²⁸ per verified block. The locator declines for modes with no IV (ECB,
RC4).

## Recovery coverage and confirmed limits

Recoverable (each proven by a `convict → persist-IV → recover` round-trip that inverts the
engine's own encryptor): AES-128/192/256, 3DES, SM4, Camellia-128/256, ARIA-128/256, SEED
in ECB/CBC/CFB/OFB/CTR; AES-128/256 XTS; ChaCha20/Salsa20/XChaCha20/XSalsa20; RC4
short-key. Confirmed limits, declined honestly (never guessed):

- **CBC/CFB block 0 without a captured IV** — a **1-block** loss: recovery returns blocks
  ≥ 1 and leaves block 0 as the on-disk ciphertext. Block-0 IV is **not** reconstructed
  from file-type magic (a content assumption; also the file's `C_0` is often the
  attacker's own header). 1-block loss is the honest default.
- **RC4 S-box-state-only** (`key_length == 0`) — not whole-file recoverable; declined
  (`SAR_RECOVER_DECLINED_STATE_ONLY`).
- **Missing IV for a keystream mode** (`SAR_RECOVER_DECLINED_IV`), **unknown geometry**
  (`SAR_RECOVER_DECLINED_GEOMETRY`), **unusable algorithm/mode**
  (`SAR_RECOVER_DECLINED_KEY`).

### Intermittent CBC/CFB chaining (documented limitation)

For block-chaining modes the predecessor block `C_{i-1}` is read from the on-disk
ciphertext. This is correct for full-file encryption (the whole file is one chain) and for
`reset_per_chunk` (each chunk re-IVs). It is **not** correct for `continuous`-policy
chaining across a *skipped* (plaintext) gap, because the gap is not ciphertext — such a
construction does not occur in practice and is out of scope. Keystream modes
(CTR/OFB/stream) have no such constraint: each block's keystream depends only on the
key+IV+index, so intermittent recovery is exact under either counter policy.

## Module layout and the freestanding boundary

The decrypt **transform** (`recover.c`) is freestanding like the rest of `engine/`: caller
buffers, no heap, no libc (it uses `eng_mem.h`, the cipher vtable, and the stream/CTR
primitives). The **file orchestration** (`host/recover_file.c`) is the only part that needs
libc (read, `mkstemp`, `fsync`, `rename`); it is a separate library target so the
freestanding verify path stays clean and the same transform can later drop into the kernel
recovery slice unchanged. `sar_recover_buffer` operates on a whole-file in-memory buffer —
adequate for the host and the test harness; streaming for very large files belongs to the
deferred Windows slice and is noted, not built.

## What the tests prove — and what they do NOT

`tests/test_recover.c` (78 assertions). Read these claims literally so the reviewer does
not over-trust them:

- **A1 round-trips** (`convict → persist-IV → recover`) prove the *pipeline* end to end:
  conviction selects the mode and emits the IV, and recovery inverts it back to the exact
  plaintext. These are **self-consistency** against the engine's own encryptor — they show
  recovery correctly inverts the engine's primitives, **not** that those primitives match
  any external standard. (That is what the next item is for.)
- **Absorbed published KATs** prove the primitives against **independent** ground truth:
  Camellia-192/256 (RFC 3713), ARIA-256 (RFC 5794), ChaCha20 keystream block
  (RFC 8439 §2.3.2), and AES-128/256-XTS (cross-checked against OpenSSL 3.0.13 /
  python-`cryptography`). Slice 1 left several of these on self-consistency only; this
  closes that gap for the recovery-direction primitives. **Salsa20/XSalsa20 have no
  external KAT here** — they rest on self-consistency (round-trip) plus the shared
  ChaCha/HChaCha quarter-round, because no surveyed host tool ships a Salsa20 vector; this
  is a stated gap, not a silent one.
- **IV recovery** proves both the A1 persisted value equals the true IV and the A2
  locator finds the true IV/CTR-origin purely by the cryptographic relation.
- **Slice-aware** proves encrypted ranges are recovered *and* skipped ranges are
  byte-identical to the original, under STRIDE/continuous, HEAD/ChaCha, and
  STRIDE/reset-per-chunk; one assertion shows a whole-file decrypt *corrupts* a skipped
  region, i.e. the geometry input is load-bearing.
- **No-clobber** proves, on a real on-disk file via the crash-safe writeback path, that a
  wrong key fails the forward relation and leaves the file byte-for-byte intact, while the
  correct key recovers it. It does **not** test crash-durability of `fsync`/`rename`
  itself (that is OS behavior, not unit-testable here) — only that no write occurs on a
  declined recovery and an atomic replacement occurs on success.
- The tests run on a single host (Linux) with a whole-file in-memory model. They do not
  exercise the Windows filesystem path, streaming of multi-GB files, or the capture path
  that will fill the novelty slices — all deferred and named above.
