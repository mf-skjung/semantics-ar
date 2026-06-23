# Slice 1 — The Heart and Its Contract — Design Notes

This records the rationale that the code cannot express. It is durable truth, not a
restatement of the source. It is subordinate to `CONSTITUTION.md`; where they disagree,
the Constitution governs.

## Scope delivered in this slice

- The offline **conviction engine** (`engine/`): a portable-C core (C11, freestanding-
  friendly — no libc dependency in the verify path, no heap, caller-provided buffers)
  that, given captured 16-byte register candidates and/or a scanned memory buffer plus a
  `(P, C)` sample and its file offset, returns a forward-only verdict
  `{algorithm, mode, key, key_length, mode_params}` or "no conviction".
- The **keystore** (`engine/src/keystore.c` + `common/include/semantics_ar/keystore_format.h`):
  the append-oriented, MAC-chained on-disk format and its build/verify logic.
- The **contract** (`common/`): `protocol.h`, `errors.h`, `keystore_format.h`. The shadow
  design (`shadow_format.h` and the shadow/journal/preserve/eviction/saturation message
  set) is deleted, not refactored.
- The driver-side **gate→battery seam** struct (`driver/include/battery_seam.h`), header
  only.

## [DERIVED] Why trying many hypotheses is safe — the 2^-128 license

A wrong key hypothesis (a wrong candidate, a wrong round-key-inversion guess, a wrong
cipher/mode) passes the forward test only if it reproduces a full 16-byte block of `C`
from `P`. For an independent wrong hypothesis this happens with probability ~2^-128 per
verified block. The engine verifies at least one full block (block ciphers/XTS) or ≥16
keystream bytes (stream/CTR/CFB/OFB), so each false-conviction probability is ~2^-128.
This is the mathematical license to run a wide battery — every cipher, every mode,
round-key inversion at every schedule position, both byte orders — without fear of false
conviction. The cost of a wrong hypothesis is wasted cycles, never a false accusation
(Constitution II.1.1).

## Forward-only conviction (Constitution II.2)

Conviction requires `Encrypt_K(P) == C`. The engine only ever calls the cipher in the
encrypt direction to decide guilt; the block-mode relations that happen to use the
cipher's decrypt primitive (CBC, CTR) are still forward proofs of the *write* (they
confirm `C` was produced by encryption under `K`), not reverse decryptions of `C`. A
reverse-direction sample (a benign decryption: plaintext written over ciphertext) yields
no forward match and does not convict; a wrong candidate does not convict. These three
negatives are tested.

## [DECISION] AES round-key inversion

The AES key schedule is invertible. Rather than only concatenating register pairs, the
engine implements the true inverse key schedule (`aes_master_from_window`):

- AES-128: a single captured 16-byte round key at schedule round `t` inverts to the
  master by running the key-expansion recurrence backward with `Rcon[t]..Rcon[1]`. The
  engine tries `t = 1..10`.
- AES-256: two schedule-adjacent round keys (32 contiguous bytes = one 8-word schedule
  window `W[8m..8m+7]`) invert to the master; the engine tries `m = 1..7`.
- AES-192: a 24-byte (6-word) window per the 6-word schedule step, `m = 1..7`, fed from
  the register-pair concatenations. At 16-byte register granularity AES-192 windows are
  formed from pairs; this is best-effort but the dominant AES-NI cases are 128 and 256.

Each candidate is also tried in its per-32-bit-word byte-reversed form (endianness
insurance). Register-pair concatenation is retained as a direct-master path but is no
longer the only AES-256 path. Verified by feeding a round key (AES-128) and an adjacent
round-key pair (AES-256) — not the master — and confirming the master is recovered and
convicts.

## Cipher battery — what is recoverable, and what is demoted

Implemented (KAT-anchored against published vectors, not self-consistency):
- Block: AES-128/192/256, 3DES, SM4, Camellia-128/192/256, ARIA-128/192/256, SEED.
- Modes: ECB, CBC, CFB, OFB, CTR, XTS. GCM is recovered as its CTR keystream — the engine
  recognizes the GTR/GCM big-endian 32-bit counter via the CTR verifier and reports it as
  CTR; the GCM authentication tag is ignored for recovery (it has no recovery role).
- Stream: ChaCha20/XChaCha20, Salsa20/XSalsa20 (key verified, keystream derived from the
  sample); RC4 verified from a captured S-box state `(S, i, j)` via PRGA in the scan path,
  plus a short-key path (key lengths 5/8/16) for small or hardcoded keys.

The relocated reference ciphers (SM4, Camellia, ARIA, SEED, ChaCha/Salsa/RC4) were found
to have corrupted constant tables — Camellia and SEED were internally self-consistent
(encrypt/decrypt mutually inverse) but did not match their standards, and ARIA's S2 box
was non-bijective and its decrypt path was wrong. A self-consistent-but-non-standard
cipher cannot convict real ransomware, so Camellia, ARIA and SEED were reimplemented and
are now verified against RFC 3713 / RFC 5794 / RFC 4269 vectors. This is exactly why the
test suite uses published KATs, not round-trips against our own encryptor.

[CONFIRMED LIMIT] **HC-128 and SOSEMANUK are demoted** and have no verification path in
the engine. Their internal state is not recoverable at 16-byte register granularity, and
no surveyed ransomware family uses them. Per Constitution VIII.2 this is a confirmed
limit, not an open problem; it is recorded here rather than engineered around.

[NOTE] **XTS is rare against file ransomware** (it is a full-disk / volume construction;
Constitution II.3.1 names it). It is implemented because the Constitution names it, but
its weight in the threat model is low. The engine tries the two standard data-unit sizes,
512 and 4096 bytes, and records the matched size in `mode_params`.

## IV / nonce is not an engine concern

The IV/nonce is neither an engine input nor a keystore field. Conviction needs no IV: for
CTR/CFB/OFB/stream the keystream is `P XOR C` over the sample, and for CTR the counter
structure is recovered from two adjacent keystream blocks. The per-file IV is a
recovery-time concern handled in a later slice.

## [DESIGN] Keystore integrity — HMAC-SHA256 MAC chain

- The keystore is **append-oriented** and **never caps-and-drops or silently overwrites**
  a key. Dedup is by `key_id` (a no-op under per-file keys). The old `slot = MAX-1`
  overwrite behavior is gone.
- Records are indexed by `key_id`, a keyed MAC of the key material
  (`HMAC-SHA256(K, 0x01 || algorithm || mode || key_length || key_bytes)`), never by the
  raw key.
- Tamper-evidence is a **MAC chain**:
  `mac_i = HMAC-SHA256(K, record_i.fields || mac_{i-1})`, with `mac_{-1}` = zero. The
  header carries magic, protocol_version, record_count, a monotonic generation counter,
  and the running head MAC (`= mac_{n-1}`).
- HMAC-SHA256 is the recorded [DESIGN] choice (keyed, standard, kernel-implementable; the
  MAC key never exists in user mode per Constitution VII.1).
- **Verification order** (`sar_keystore_verify`): magic/version → declared size vs buffer
  length (short buffer = truncation) → each record MAC recomputed and chained → head MAC
  vs last record → then, if an external anchor is supplied, generation and head MAC vs the
  anchor. The four attacks are distinguished and tested: a single record edit and a
  reorder break a record MAC; a tail truncation shortens the buffer below the declared
  size; a whole-file rollback to an older but internally-valid chain passes internal
  checks and is caught only by the generation/head-MAC mismatch against the anchor.
- The **external anchor** (kernel pool + TPM seal, Constitution VII.1/VII.2) is a later
  slice. Here the format and the full verification (including the anchor comparison) are
  implemented and proven with a test key; the test plays the role of the anchor holder.

## [DECISION] Battery location = kernel; contract seam

The cipher-verification battery runs in the kernel driver, in a PASSIVE_LEVEL worker
thread **after** the write IRP is released (Constitution II.3.2) — never on the IRP path.
Consequence for the contract: **plaintext keys, candidate registers, and the `(P, C)`
sample never cross the driver↔service boundary.**

- The driver↔service message set (`protocol.h`) is therefore only: verdict notification
  (`key_id`, algorithm, mode, mode_params, provenance — **no** plaintext key, **no**
  candidates, **no** `(P, C)`), recovery request, recovery complete, mode control
  (AUDIT/ENFORCE), whitelist control (by creation-time verified identity: image path +
  code signature subject + content hash), and status. Every message starts with a header
  carrying `protocol_version` and a per-message byte length.
- The gate→battery seam is **internal to the kernel** and is defined as
  `semantics_ar_battery_request_t` in `driver/include/battery_seam.h`: it carries the
  `(P, C)` sample, the destruction-member tag, and the gate's high-novelty slice
  offset/length. This struct is defined now so the seam is correct; the gate's novelty
  math and the register-first capture that fill it are later slices.
- In the kernel the portable-C battery is compiled into the driver; AES/3DES/HMAC there
  use kernel-mode CNG (Cng.lib) at PASSIVE_LEVEL, while the engine's portable AES/3DES/
  SHA-256 are what the host test harness uses (no CNG on the host). The engine's mode
  logic is cipher-agnostic over a block-cipher vtable, so the CNG-backed primitives drop
  into the same path in the driver.

### Connect-time version handshake (specified, not implemented here)

The authenticated comm port (Constitution VII.3) is a later slice. When built, the first
exchange on the port is a handshake: the client sends a `semantics_ar_msg_header_t` with
`message_type = GET_STATUS` and its `protocol_version`; the driver replies with
`semantics_ar_status_reply_t` carrying its own `protocol_version`. A mismatch closes the
connection with `SEMANTICS_AR_ERROR_VERSION_MISMATCH`. Every subsequent message is
validated against the header's `protocol_version` and `message_length` before its body is
read. This slice defines that header/version contract; it does not open the port.

## Build and warnings

`engine/` and `tests/` build on this Linux host via CMake (the Windows service remains
gated behind `if(WIN32)`). Everything compiles under `-Wall -Wextra -Werror`.
`-Wconversion` is applied to `battery.c` and `keystore.c` (the bounds/length/index logic
where it is signal); it is not applied to the cipher primitives, whose pervasive
intentional byte/word narrowing makes `-Wconversion` noise rather than signal. This is the
"where practical" reading of the directive.

## Deferred (later slices) — known traps

The Windows driver/service source tree (`driver/src/**`, `service/src/**` except the
relocated portable ciphers) is the subject of the later slices named in the directive:
the kernel register-first capture, the gate's statistical engine, the comm port, TPM
sealing, kernel-pool keystore residence, and recovery decryption. Those files still
contain the old shadow/preserve/eviction/journal machinery and the superseded user-mode
oracle (`service/src/conviction/oracle*.cpp`, `mode_verify.*`, the old `keystore.cpp`,
`chain_handler.cpp`, and `ciphers/hc128.*`, `ciphers/sosemanuk.*`). They do not build on
this host and are not part of this slice's build; each later slice rewrites its files
against the new contract and removes the residual shadow machinery. The contract change
(deleting `shadow_format.h`, removing its two includes, and re-founding `protocol.h`/
`errors.h`) is complete now; the Windows tree's rewrite to match is deferred.
