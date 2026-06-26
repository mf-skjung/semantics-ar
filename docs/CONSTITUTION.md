# semantics-ar — IMPLEMENTATION CONSTITUTION

> **STATUS: RATIFIED.** This document is the authoritative specification for the
> semantics-ar defensive anti-ransomware system (Windows filesystem minifilter +
> user-mode service). It governs implementation. Where code and this document
> disagree, this document is right and the code is wrong.
>
> This is a complete document written from a single governing proposition (Part I).
> It is not a patch over an earlier design. Every clause below is either a
> derivation of that proposition or an explicit statement of where the proposition
> stops. There is no clause that exists for any other reason.

---

## PART 0 — HOW TO READ THIS DOCUMENT (binding)

### 0.1 This document is a closure boundary
You (the implementer, human or model) will re-research each area you build, and you
will reconstruct your own understanding. That is expected. This document does not
exist to re-teach you. It exists to fix the boundary of what your re-research is
allowed to **reopen**.

> Re-research answers **how** to implement a settled conclusion.
> It must not be used to reopen **whether** the conclusion holds.

### 0.2 Item types
Every normative item carries exactly one tag.

- **[INVARIANT]** — A structural property that must always hold. If your
  implementation can violate it under any input, timing, failure, or supported OS,
  the implementation is wrong.
- **[DECISION]** — A chosen method. Implement it as written. You may vary only
  within its stated latitude.
- **[BOUNDARY]** — A thing we deliberately do **not** defend against. You must not
  add defenses past this line.
- **[NEGATIVE]** — A thing you must not do.
- **[DEPLOY]** — An environment precondition whose closure is verification or
  configuration, not research.

Where a quantitative claim appears, it is tagged by epistemic status:
**[MEASURED]** (empirical), **[DERIVED]** (mathematical), **[DESIGN]** (a recorded
choice, not forced by fact). Keeping these distinct is mandatory; collapsing them
is how false certainty enters a specification.

### 0.3 The default state of a newly found gap is CLOSED
While implementing, you will discover new-looking attack surfaces. Before treating
any as open work, classify it:

1. Does it require kernel-code execution? → **CLOSED** by Part VIII (boundary).
2. Is it a case where the Oracle simply does not capture a key? → **CLOSED** by
   Part VIII (confirmed limit).
3. Is it already governed by an existing [INVARIANT] or [DECISION]? → **CLOSED.**

If and only if it escapes all three is it a genuine open item.

> **[NEGATIVE]** Do not escalate threat vectors indefinitely. "A trusted
> administrator who decides to attack," "the Oracle might be bypassed," "the
> capture read might be induced to fail without kernel access" are not defended.
> Defending the impossible is the slope this project is defined against. Saying
> *less* and defending *narrowly* is correct here, not a weakness.

### 0.4 What vs how
This document specifies **what must be true**, precisely enough to be mechanically
checkable. It does not specify **how** (data structures, libraries) except where a
how is itself load-bearing and promoted to a [DECISION]. Implementation freedom is
real; constraint satisfaction is mandatory.

### 0.5 No runtime-tunable policy
Every policy value in this document is a recorded design decision derived now, not a
knob exposed to runtime configuration. Where a value sits inside a derived safe
band, the band and its safety are [DERIVED] and the chosen point is [DESIGN]; both
are recorded here, neither is deferred to "measure later."

---

## PART I — THE GOVERNING PROPOSITION

Everything in this document is this sentence, unfolded:

> **Protection is the capture of the encryption key.**
> When the key is captured, any file encrypted under it is recovered by decrypting
> the ciphertext that already resides on disk. When the key is not captured, the
> system does not protect that file — and says so plainly.

This is the heart. The proof of encryption — the **Oracle** — is the system. Three
consequences follow, and the rest of the document is only their detail.

**[INVARIANT] I.1 — The Oracle is the sole locus of protection.**
There is no second mechanism that protects data when the Oracle fails. Any design
element that exists to hedge against the Oracle being wrong, missed, or bypassed is
not a safety net; it is a statement of distrust in the heart, and it is absent from
this system. A solution premised on "the Oracle might be bypassed" has no reason to
exist, because the Oracle is the reason it exists.

**What derives from the proposition.** The gate (Part IV) exists only to decide what
to ask the Oracle about. Recovery (Part III) is decryption with the captured key.
Response (Part V) is the Oracle's verdict plus the user's intent. Identity discipline
(Part VI) exists so the capture cannot be evaded by who issues a write.

**What is absent because it would distrust the proposition.** There is **no**
preservation of originals as a recovery source; no shadow store; no capacity limit;
no saturation signal; no key-less auto-block; no backup held "in case the Oracle was
wrong." Each of these was a hedge against the heart and is therefore not in this
system (Part III, Part V, Part VIII record this explicitly).

**Where the proposition stops.** When the Oracle does not capture the key, the file
is not protected. This is the **confirmed limit** (Part VIII). It is not a defect to
be engineered away; it is the honest edge of a key-capture defense, and the system
states it rather than pretending past it.

---

## PART II — THE ORACLE (the heart)

### II.1 What the Oracle proves
**[INVARIANT] II.1.1 — Behavioral proof by key recovery.**
The Oracle proves that a write is encryption by recovering a candidate key `K` and
verifying the transformation that produced the write. The proof is the relation
itself; it is identity-independent and content-independent. A recovered `K` that
satisfies the relation simultaneously proves *that this was encryption* and *yields
the means to undo it*. A wrong candidate simply fails to verify — a cost, never a
false accusation.

### II.2 Directionality — the load-bearing shape of the proof
**[INVARIANT] II.2.1 — Conviction is forward-only.**
Conviction holds **only** in the forward direction:

```
    Encrypt_K( the destroyed original )  ==  the bytes that were written
```

The reverse direction — decrypting ciphertext to plaintext — is **never** a
conviction, even when `K` is captured. Directionality is not an added test; it is
the shape of the single test the Oracle already performs. Three things follow
directly, with no extra machinery:

- **Benign decryption does not convict.** A user decrypting a file yields a captured
  key but no forward match (`Encrypt_K(ciphertext) ≠ plaintext`). The key
  accumulates harmlessly in the keystore (II.5).
- **The system never convicts itself.** Recovery (Part III) writes plaintext over
  ciphertext — the reverse direction — so the system's own recovery writes cannot be
  mistaken for an attack. No self-trust flag is required to prevent this; the
  direction does it.
- **The direction is decided in the Oracle, never in the gate.** Pushing
  directionality down into the gate would require a per-block entropy/structure
  measure, which is forbidden (Part IV); cryptographic direction is the Oracle's,
  cheaply and without blind spots.

> **[NEGATIVE] II.2.2** Do not verify the reverse direction, and do not treat a
> captured key as conviction absent a forward match. A key in hand is not a verdict;
> the forward relation is the verdict.

### II.3 When the Oracle runs
**[DECISION] II.3.1 — Anchored to the writer, captured across the encryption window.**
The Oracle's capture target is the write that produces ciphertext, because the causal order
`read → encrypt → write` guarantees the key is resident in the writing process's memory at
that write. Its residence is the writing process's committed-private memory — for the dominant
case a heap key object (e.g. CNG's `BCryptGenerateSymmetricKey` object) that holds the expanded
key schedule and is reused across writes — not its stack and not its registers, which hold the
key only transiently. A process that performs a capture-eligible destructive write is therefore a
capture target for as long as it remains an active destructive writer: the write names the process
to read, and because the key is resident across the encryption window — not only at the instant of
one write, and sometimes only in the interval between writes — the Oracle reads that memory both at
the write and again, on a behavior-paced cadence, while the process keeps destroying. The writes
tell the Oracle which process, and across what span, to read.

**[INVARIANT] II.3.2 — The memory capture runs off the IRP, on a behavior-paced cadence; nothing scans the writer's memory from the write path.**
Enumerating or reading the writer's address space from inside the filesystem write IRP can
re-enter the filesystem and the memory manager and deadlock; the capture must never do it
there. The permitted shape:

1. **Deferred snapshot at the write.** Once the write IRP is released, a worker attaches to the
   still-live writer's address space — under process-exit synchronization — and copies the bounded
   committed-private regions of its memory, the **heaps first** and then any other committed-private
   region, into a kernel buffer, touching resident pages only. The heaps are the primary target
   because the key object resides in one, so the heap list (enumerated from the process control
   block) gives the cheapest coverage of the dominant case; the remaining committed-private regions
   are swept within the same bounded budget so a key object the allocator placed outside the
   enumerable heaps is still covered. This runs off the IRP, holding no filesystem resource, so it
   cannot deadlock against the write.
2. **Periodic re-sample of the active writer.** A process whose destructive write was
   capture-eligible is *flagged* — a behavioral mark derived from the write itself, never an
   identity (VI.1) — and while it remains an active destructive writer a bounded-rate worker
   periodically re-attaches and re-snapshots its committed-private memory, off the IRP under the
   same exit synchronization. The cadence is paced by the writer's own destruction — sampled per a
   span of bytes destroyed, so it scales with attack throughput — and is revisited on a bounded
   wall-clock interval, so a key that lives only in the interval between writes (computed, used, and
   about to be zeroized) is still read. Each re-sample is an independent capture opportunity for
   whatever key is resident then; conviction remains the forward proof (II.2.1) against a recent
   (`P`,`C`) the writer has already produced, never key-in-hand. The snapshot at the write is the
   prompt first reading; the re-sample extends the reading across the residence of the key.
3. **Analysis on the frozen bytes.** The structural key-schedule scan, the cipher battery, and
   the forward (`P`,`C`) proof run on each copied buffer — never on live process memory and never
   on the IRP — so destruction of the live key after a snapshot cannot defeat what that snapshot
   already holds.
4. **Optional synchronous register grab.** Only the register file (XMM / `FXSAVE`) — kernel-saved,
   fault-free, no memory enumeration, no re-entrancy — may be taken during a short synchronous
   hold; it resists key destruction and process exit but is low-yield because registers are
   transient.
5. **The cipher-verification battery** (AES family incl. XTS, plus ChaCha/Salsa, 3DES, SM4,
   Camellia, ARIA, SEED, etc.) is offline on the snapshot and the in-message plaintext/ciphertext
   sample. It needs no live process.

> Because every snapshot is a prompt off-IRP copy and the analysis runs on frozen bytes, no timing,
> deadlock, or scan-fault can reach the IRP. The cost is the honest one: the write-time snapshot and
> the behavior-paced re-sample together hold the key in view for as long as it is resident, so a key
> reused or held across the encryption window is caught even when no single write coincides with its
> residence; a key resident only for a window shorter than the sampler can reach — computed, used,
> and zeroized between writes faster than any snapshot in that window, or the process exited before
> it — is not captured by the memory path (VIII.2). The cadence and the register grab shrink that
> window; what remains is the confirmed limit.

### II.4 Conviction-rate levers
**[DECISION] II.4.1 — Structural location and round-key inversion.**
The AES key schedule is self-identifying and invertible, and this is the dominant lever
for the AES-NI families. Self-identifying: an expanded schedule resident in memory
satisfies the key-expansion recurrence, so it is located over a whole snapshot cheaply,
at any byte offset, with no plaintext or ciphertext — the structural scan that makes a
large memory region tractable instead of a blind per-offset trial. Invertible: any one
captured round key reconstructs the master (the S-box, `RotWord`, and `Rcon` steps are
all reversible), so a single correct round key suffices, and a 16-byte register or
schedule fragment is treated as both a candidate master and a candidate round key at each
schedule position. Location is `(P,C)`-free; conviction is still the forward proof
(II.2.1), never key-in-hand. Declining to scan structurally or to invert is a code
limitation, not a fundamental limit. Keys with no such structure — stream-cipher and raw
block-cipher master keys (ChaCha/Salsa, and a bare AES master not accompanied by its
schedule) — are indistinguishable from random in memory and are located only by bounded
`(P,C)` trial over the snapshot; a key whose location falls outside that bounded trial
reduces to the confirmed limit (VIII.2).

**[DECISION] II.4.2 — cumulative-N over writes and samples.**
Each capture-eligible write is an independent capture opportunity for the key it uses, and so is
each periodic re-sample of the active writer (II.3.2) taken while that key is resident. For a
single-opportunity capture probability `p > 0`, the chance of at least one capture across `N`
opportunities under that key is `1 − (1−p)^N → 1` rapidly. The relevant `N` is **per key** and it
accrues along two axes — the capture-eligible writes under the key, and the behavior-paced samples
taken across the span the key is resident. Where a campaign reuses one key across many files, `N` is
the whole campaign's writes and samples and capture is near-certain; under per-file keying — the
dominant hybrid-envelope shape, where each file gets a fresh symmetric key wrapped by the attacker's
asymmetric key — `N` is the capture-eligible writes of *that file* plus the samples taken while that
file's key is resident, so a file whose key is written by very few writes and resident only briefly
has correspondingly fewer chances.

> **cumulative-N is redefined from the old design and this redefinition is
> load-bearing:** it is *not* the accumulation of backups. It is the accumulation of
> (a) capture opportunities and (b) captured keys in the keystore. A key caught at
> *any one* of its writes recovers *every* file encrypted under that key — the whole set
> under key reuse, that one file under per-file keying — including data written before the
> catch. This is why a single missed write costs nothing within a key's write set: the
> recovery asset is the key, not a per-file backup.

### II.5 The keystore
**[DECISION] II.5.1 — The keystore is the system's sole persistent recovery asset.**
Captured keys (with the algorithm and parameters needed to use them) are written to
a keystore. The keystore holds **one record per encrypted region** — the byte range a
single convicting write destroyed: each record carries the key, that region's
{offset, length} and per-region cipher parameters (IV/nonce/counter — which a family may
mutate per chunk), and a **verification tag** — a one-way hash of a sample of *that
region's* original taken from the Oracle's input at the write that convicted (III.2) — so
a recovery can confirm it reconstructed the true original before it replaces those bytes
(III.4); the tag is a check value, never a preserved original (III.1.1). A whole-file
encryption is the special case of a single region spanning the file. The key bytes repeat
across a key's regions and across files it reused on, but each record binds them to a
distinct (file, range) with its own provenance, parameters, and tag — so a key caught at
any one of its writes recovers **every** region in its set (II.4.2), each verified against
its own tag, whether those regions are spread across many files (key reuse) or scattered
within one file (intermittent/partial encryption). The record is therefore the
(key, file, range) binding, never a bare key: collapsing a key's regions to one record
would discard every sibling region's recovery anchor and silently deny it recovery, which
II.4.2 forbids. Its size is proportional to the number of encrypted regions observed —
thousands to hundreds of thousands of records, megabytes in scale; the on-disk format is
append-oriented and must scale to many records. It is the new center of gravity: if it
survives, recovery is possible; if it is destroyed, recovery is lost. Its protection
is therefore the crown-jewel concern of Part VII, replacing every protection the old
design spent on a shadow store.

---

## PART III — RECOVERY (key-based; no preservation)

### III.1 The recovery sources
**[DECISION] III.1.1 — Recover from the key and the on-disk ciphertext. Preserve
nothing.**
There are exactly two recovery sources, and neither is a backup of the original:

1. **The keystore** — the captured keys (II.5).
2. **The ciphertext on disk** — which the attacker has already written, at no cost
   to us.

Recovery is: decrypt the on-disk ciphertext with the captured key, write the
plaintext back. No original is ever preserved to disk or held as a recovery copy.

**[INVARIANT] III.1.2 — Recovery decrypts where the key already lives: in the kernel.**
Recovery is operator-initiated. The captured key never leaves the kernel pool to
perform it. The minifilter reads the on-disk file, decrypts in kernel — with the key it
holds (VII.1.1) — **only the captured encrypted regions** of that key (II.5.1), each at
its own offset with its own parameters, and writes a temporary file beside the target in
which those regions are restored to plaintext and **every byte outside a verified region
is left exactly as it was on disk**; the user-mode service then performs the final
metadata-preserving atomic replacement of the target by that temporary file. A whole-file
encryption restores as a single region spanning the file; an intermittently encrypted
file restores its ciphertext slices while its untouched plaintext spans are never
rewritten. The service drives recovery by key identifier and
target path only — it never receives the key and never receives the recovered plaintext.
The communication port therefore carries, for recovery, only the key identifier, the
target path, and a status; never key material and never plaintext. No recovery step
relaxes the privilege boundary of VII.1.1, and the atomic replacement preserves the
target's security descriptor, alternate streams, object identifier, and creation time so
recovery restores the file, not merely its bytes. The kernel checks the decrypted result
against the capture-time verification tag (III.4) before it produces the temporary file,
so a wrong key, geometry, or key↔target pairing never reaches the atomic replacement.

> **Why this is the consistent endpoint, not a cost-saving shortcut.** A preserved
> original could only save one case a captured key cannot: a file whose key was
> never captured at any of its writes. But the verdict "never captured" never
> terminates, so saving that case requires holding the backup indefinitely —
> retain-everything, which the governing proposition rejects. The single benefit of
> preservation is realizable only through a premise this system has discarded;
> therefore preservation is discarded with it. The thing worth holding was always
> the key.

**[DECISION] III.1.3 — Enumeration projects non-secret recovery metadata from the
keystore; no shadow.**
The keystore is also the sole source of truth for *enumerating* what is recoverable —
an operator must be able to see what the system can restore in order to direct recovery
(III.1.2). No second copy of that catalog is maintained in user mode; a user-mode shadow
would diverge from the keystore (most visibly, it would not survive a service restart
while the keystore does) and would be a second thing to protect. On the authenticated
communication port — and only after the client handshake completes (VII.3) — the kernel
projects, per record, the **non-secret** tuple {key identifier, algorithm, mode,
provenance offset, provenance path}. It never projects key material, the IV, or the
verification tag. This is the same non-secret tuple the post-conviction notification
already carries (II.5), pulled on demand rather than pushed; enumeration therefore widens
the recovery exchange of III.1.2 by exactly these non-secret fields and by nothing else,
and III.1.2's "key identifier, target path, status only" continues to bind the *recovery*
exchange unchanged. Because the keystore holds one record per encrypted file (II.5.1), the
catalog enumerates one entry per captured file, so a reused key's whole recoverable set is
visible, not just a single representative.

### III.2 The transient Oracle input
**[DECISION] III.2.1 — The only transient artifact is the Oracle's input, in
memory.**
To attempt the forward proof, the Oracle needs a sample of the original (`P`) and
the written bytes (`C`). `C` is the write buffer. `P` is the matching plaintext as it
stood before the write, and it is sourced from the originator's *own* read of those
bytes — sampled at the read seam into a per-stream buffer and correlated to the write
by stream and offset — never by reading the file at the write IRP. A synchronous read
of the same stream from within its own write re-enters the cache and memory manager
under locks already held and wedges the volume; the originator's read is the honest,
hazard-free source, because a writer that overwrites a file in place must first read
the plaintext it destroys. A write whose plaintext was never observed yields no Oracle
attempt — a missed attempt inside the confirmed limit (VIII.2), never a read forced
onto the IRP. A single block suffices to verify a key. This sample lives in memory
only, for the span of the proof attempt. It is **not** a backup, has no recovery role,
and is never persisted.

**[DECISION] III.2.2 — Under pressure, drop the sample; do not spill or hold.**
At attack throughput the in-memory Oracle-input queue is bounded by a fixed cap.
When the cap is reached, the oldest pending sample is dropped. A dropped sample is a
**missed Oracle attempt**, absorbed by cumulative-N (II.4.2) — the same key recurs
on other writes of the campaign — and is therefore inside the confirmed limit, never
a loss beyond it. There is no disk spill and no hold-the-IRP fallback, because there
is no preservation guarantee to keep.

### III.3 Self-recovery is sound by direction, not by trust
**[INVARIANT] III.3.1.** The system's recovery writes (plaintext over ciphertext)
are the reverse direction (II.2.1) and therefore cannot convict the system of
encryption. No self-trust identity flag is needed or used for this; if one were used
it would re-introduce the identity dependence Part VI forbids.

### III.4 Recovery is verified before it replaces
**[INVARIANT] III.4.1 — Recovery writes back only verified regions; everything else is
left byte-for-byte.**
Recovery decrypts each captured encrypted region (II.5.1) with that region's key and
parameters and, before it writes that region into the result, checks the decrypted
region-sample against the **verification tag** recorded for that region at capture (II.5):
the one-way hash must equal the recorded tag. **Only regions that pass are applied; every
byte outside a passing region is left exactly as it was on disk.** A region whose
decryption cannot be confirmed — wrong key, cipher, mode, geometry, parameters, or a
region that in fact belongs to a different file under a reused key — is simply not
applied. Two safety properties follow structurally, not by heuristic: recovery can never
overwrite a file with a result it cannot confirm is the original, and recovery of an
intermittently encrypted file can never corrupt the plaintext spans the attacker left
untouched (they have no record and are never rewritten). If no region verifies for the
target, recovery declines and the file is left byte-for-byte intact. A region the Oracle
never captured stays as on-disk ciphertext — an honest **partial recovery** (VIII.2), the
same edge as the confirmed limit, never a destructive replace. This is the recovery
direction's analogue of the Oracle's forward proof: conviction never trusts a key in hand
(II.2.2), and recovery never trusts a decryption in hand.

The tag is a hash of a bounded original sample, never the sample itself — a check value
with no recovery role, and not a preserved original (III.1.1). It rides in the keystore
record and is covered by that record's keyed MAC (VII.1.1), and the Oracle captures one
for each file it observes, so each file recovers against its own anchor. Where recovery
is directed at a target for which no verification anchor was captured, it declines rather
than perform an unverifiable destructive replace — the same honest edge as the confirmed
limit (VIII.2): the system replaces only what it can prove it is restoring.

---

## PART IV — THE GATE (D ∧ T)

The gate decides one thing: **is this write worth asking the Oracle about?** It is a
cost filter, not a classifier. Whether the write is encryption, and whether it is an
attack, are the Oracle's question and the user's question respectively — never the
gate's.

### IV.1 D — destruction of an existing original
**[DECISION] IV.1.1 — D is the destruction-of-an-existing-original axis.**
The gate considers only writes that destroy bytes of an existing file. A pure new
write (no existing original) has nothing to recover and does not enter. The
existence of an "old" against which novelty is measured (IV.2) is itself the D
premise: with no original, there is no transition to measure.

**[DECISION] IV.1.2 — The destruction surface is a decidable enumeration.**
Because the ways to destroy an original are the filesystem interface, they are
enumerable, and the gate must see each. Members (each: the encryption-bearing write
on this path is a capture candidate):

- In-place overwrite — `IRP_MJ_WRITE` (cached, non-cached, paging writeback).
- Truncate — `FileEndOfFileInformation` shrink **and** `FileAllocationInformation`
  shrink.
- Delete — `FileDispositionInformation` **and** `FileDispositionInformationEx`.
- mmap / section write — captured at section creation
  (`ACQUIRE_FOR_SECTION_SYNCHRONIZATION`), surfaced as paging write.
- Rename-over-existing — `FileRenameInformation`(`Ex`) + `ReplaceIfExists`.
- Hardlink-replace — `FileLinkInformation`(`Ex`) replace.
- Block-clone / ODX — `FSCTL_DUPLICATE_EXTENTS_TO_FILE`, `FSCTL_OFFLOAD_WRITE`.
- Destructive / detaching FSCTL class — `FSCTL_LOCK_VOLUME`,
  `FSCTL_DISMOUNT_VOLUME` (and legacy force-dismount), `FSCTL_FILE_LEVEL_TRIM`,
  `FSCTL_SET_ZERO_DATA`, `FSCTL_SET_SPARSE`, VSS shadow-deletion.
- BypassIO — not a destruction path; do not declare
  `SUPPORTED_FS_FEATURES_BYPASS_IO` (or veto it) so write visibility is preserved;
  offload writes are mediated in the FSCTL handler, not via the registry value.

> The legacy/`Ex` pairing is not two code paths for compatibility's sake; it is one
> classifier the OS feeds via whichever info-class it emits. Codes an OS does not
> implement are never delivered and are inert there (Part IX).

**[DECISION] IV.1.3 — Destruction without encryption is out of scope.**
A destruction that is not encryption (a plain delete, a wiper, a truncate-to-zero)
yields no recoverable key and is not a target of this system (Part VIII, wiper
boundary). D-completeness exists to make every *encryption-bearing* destruction a
capture candidate, not to act on destruction in itself. A missed member is therefore
a missed capture opportunity (confirmed limit), not a data loss — there was never a
backup to lose.

> **The read↔write unification (one model for all destruction shapes).** Ransomware
> must read plaintext, encrypt, and write ciphertext, destroying the original by
> overwrite or by delete-plus-new-file. The gate treats both as the same question —
> *does the write fail to be predicted from the original?* — and the Oracle input is
> the (`P`, `C`) pair:
> - **In-place:** read location = write location; `P` and `C` are in hand at one IRP.
> - **Write-then-delete:** at the delete, the original `P` is still on disk; pair it
>   with the recent ciphertext write(s).
> - **Delete-then-write:** at the delete, `C` does not yet exist; hold `P` as an
>   Oracle-input candidate (III.2) in a small per-process ring until the matching
>   ciphertext appears, or drop it on window expiry (a missed attempt, confirmed
>   limit). This ring is Oracle input, not preservation; its lifetime is the same
>   bounded constant as cumulative-N, a recorded [DESIGN] value, not a runtime knob.
>
> `gzip`-style compression (delete original, write `.gz`) is simply an instance of
> delete-plus-new-file. It needs **no special handling**: it enters as a capture
> candidate, the Oracle finds no key (compression is not encryption), and nothing
> further happens — there is no preservation to waste and no verdict to record. The
> old debate over whether compression "passes D" is moot here; it mattered only when
> entering the gate implied preserving, which this system does not do.

### IV.2 T — the meaningful-transition filter
**[DECISION] IV.2.1 — T is a direction-blind cost filter, never a classifier.**
T measures whether the write is a *meaningful* transition from the original. It does
not, and must not, attempt to separate edit from encryption, benign from malicious,
or one direction from another. Everything above the floor — edit or ciphertext, in
either direction — goes to the Oracle. Discrimination is the Oracle's job.

**[DECISION] IV.2.2 — The measure is conditional novelty, not absolute entropy.**
The signal is

```
    novelty  = 1 − coverage
    coverage = ( the written range's non-overlapping 2-grams found in the
                 original block's 2-gram set ) / ( the written range's
                 non-overlapping 2-gram count )
```

measured on the **written range** (not a file average) in fixed blocks. The judgment
question is *"given the original, is the new content predictable?"* — never *"is the
new content random?"*

> **[NEGATIVE] IV.2.3** Absolute entropy, entropy-delta, an ML classifier as the
> gate, and an always-preserve / always-pass stub are all forbidden as the gate. The
> conditional measure is required precisely because absolute entropy misses the case
> that matters: a high-entropy original (already compressed or encrypted) overwritten
> by unrelated high-entropy ciphertext. An entropy gate sees "high → high" and
> passes; the conditional measure sees unrelated 2-gram sets and fires.

#### IV.2.4 The lower-bound mathematics (settled by derivation; do not reopen)
- **[MEASURED]** Full-ciphertext novelty is ≥ ~0.97 across thousands of trials,
  including high-entropy originals (compressed → cipher, cipher → cipher); the mean
  is ~0.998 at a 256-byte block. A `k`-byte change yields novelty ≈ `k` / (block
  2-gram count), with very low variance. The measured variance matches the binomial
  prediction at every block size from 64 B to 4096 B.
- **[DERIVED]** For uniform ciphertext, `E[novelty] = 1 − |S| / 2^16`, where `|S|` is
  the original's distinct-2-gram count (≤ 255 at 256 B), giving `E[novelty] ≥ 0.996`.
  By a Hoeffding bound, the probability that a true ciphertext write is mistaken for a
  meaningless change at skip threshold `θ` is
  `≤ exp( −2·n·( min_cipher_novelty − θ )^2 )`; at a 256 B block (`n = 128`) and
  `θ = 0.10`, this is on the order of `10^−90` per block. The meaningful-change floor
  is `≤ k/128` for a `k`-byte change. The admissible band for `θ` is therefore wide;
  the binding constraint is the lower (meaningless) end, not the ciphertext end.
- **[DESIGN]** The skip threshold is **`θ = 0.10`**, chosen at the low end of the
  derived safe band, biasing toward asking the Oracle (asking more is cheap; missing
  a real encryption is forbidden). The band and its safety are derived; this point
  within the band is the recorded decision. The judgment unit is the **written
  range**, capped at a small fixed block (256 B). The small block is chosen so that an
  intermittently-encrypted slice is not diluted below the floor by an unchanged
  majority — **not** to keep the concentration bound valid (the bound holds at every
  block size; only the mean drifts with size). The earlier conjecture that a larger
  block "breaks the bound" is incorrect and is not carried forward.

> **[NEGATIVE] IV.2.5** Do not push directionality into T (it would require the
> forbidden absolute-entropy measure and would inherit its false positives, false
> negatives, and encoding-based evasions). Direction lives in the Oracle (II.2),
> where it is free and blind-spot-free.

### IV.3 The gate never blocks
**[INVARIANT] IV.3.1.** The gate emits **capture-candidate-or-skip**. It never blocks
an operation. A loose gate cannot create a false block, because it cannot block at
all. Over-asking the Oracle is a cost absorbed downstream; under-asking risks a
missed capture (confirmed limit). Tune toward over-asking. Blocking, where it happens
at all, is the ENFORCE-mode response (Part V), strictly downstream of a verdict.

---

## PART V — RESPONSE AND USER AUTHORITY (two layers)

User authority is exercised on two **distinct** layers. Conflating them is the
central error this document is written against. One governs *blocking*; the other
governs *whether the system observes at all*.

### V.1 Layer one — global mode (governs blocking, not observation)
**[DECISION] V.1.1 — AUDIT and ENFORCE.**
Key capture (the Oracle) is **always on** in both modes. The mode governs only the
response to a conviction:

- **AUDIT** — observe; capture keys; **never block.** The system records which
  originators would trigger a block, and recovers by key if asked.
- **ENFORCE** — observe; capture keys; on conviction, **block the originator's
  further writes** (the already-written, key-captured files remain recoverable).

The transition between modes is the **user's policy decision**. The system never
infers "now is the time to start blocking." This is the clean partition of
responsibility: the system mechanically proves *encryption*; the user decides *when
proven encryption should be treated as an attack*.

> A legitimate bulk in-place encryption (e.g. a database enabling transparent
> encryption) is behaviorally identical to ransomware and is **not** special-cased
> (special-casing it would be the intent-inference Part VI forbids). It is captured
> like everything else; whether it is blocked is the mode's and the user's call. In
> AUDIT it proceeds and is recoverable; in ENFORCE the user has chosen to block
> unrecognized mass encryption.

> **[NEGATIVE] V.1.2** There is no capacity limit, no saturation signal, and no
> key-less auto-block. Nothing accumulates as a "fill up and then it must be an
> attack" reservoir (there is no preservation to accumulate, III.1). A block
> originates only from a forward Oracle conviction under ENFORCE — never from a count,
> a percentage of bytes, or elapsed time. These mechanisms were hedges that
> distrusted the Oracle and are absent (Part I).

### V.2 Layer two — per-process whitelist (a full exemption the user owns)
**[DECISION] V.2.1 — Whitelist means: not protected.**
A whitelisted process is **not** key-captured and **not** adjudicated. By the user's
explicit, informed choice it is exempt from the system entirely. This is a protection
gap the user owns. The industry term and the honest meaning coincide: to whitelist is
to stop protecting.

> **Why a whitelist is a full exemption and not a quiet "observe-but-don't-block."**
> A system that kept secretly capturing a process the user declared trusted would be
> misrepresenting the control it claimed to grant — pretending to honor the user's
> judgment while overriding it. That deception is indistinguishable in effect from the
> behavior we defend against. The cost a whitelist removes (continuous capture on a
> sustained legitimate bulk-encryption process) is real, and the user is entitled to
> remove it and to own the consequence. Protecting a process whose protection the user
> switched off is not protection; it is deceit.

**[DECISION] V.2.2 — Whitelist identity is resolved at process creation.**
A whitelist entry is matched at process-creation time against verified identity (full
image path **and** code signature **and** content hash), never against the unreliable
per-IRP requestor identity (VI.1). The entry is **narrow** (a specific verified
process, ideally a specific scope), **revocable**, and **last-resort**. The default
is **no whitelist**, i.e. universal capture.

---

## PART VI — IDENTITY DISCIPLINE

### VI.1 Capture is identity-independent
**[INVARIANT] VI.1.1.** Key capture never depends on who issued a write. The requestor
identity available at a write IRP is unreliable: under IRP pending by a higher filter,
the callback may run in a system-worker-thread context. Therefore every destructive,
T-passing write is a capture candidate regardless of requestor, and the set of active
destructive writers the Oracle periodically re-samples (II.3.2) is likewise behavioral —
derived from the writes themselves, never from requestor identity — so it only ever *adds*
capture and never withholds it, and where an attacker splits the work across many short-lived
processes every one that issues such a write is flagged and re-sampled. This is not a
limitation to work around; it is why capture is anchored to behavior, not identity.

### VI.2 Identity is used in exactly one place
**[DECISION] VI.2.1.** The whitelist (V.2) is the only use of identity, it is
creation-time verified, and it governs **exemption only** — never the universal
capture default, never recovery, never the Oracle's proof.

### VI.3 Confused-deputy resolution
**[INVARIANT] VI.3.1.** Because capture is the identity-independent default and the
whitelist is the only identity-based exemption:

- A **hijacked non-whitelisted** process (injection into any ordinary process) is
  **fully covered** — its encryption writes are captured like any other; there is no
  exemption to steal.
- A **hijacked whitelisted** process is the **gap the user opened**. The system does
  not re-assume the risk the user took, and does not attempt to detect the hijack.

> **[NEGATIVE] VI.3.2** Do not build process-injection detection as a defense
> (incomplete arms race, and a different discipline from this minifilter). The only
> place hijack *prevention* belongs is the protection of the system's **own** process
> (Part VII), where it is OS-provided, incomplete, and bounded by the kernel line
> (Part VIII).

---

## PART VII — SELF-PROTECTION (protect the keystore and the observation point)

The keystore (II.5) is now the sole recovery asset; protecting it is the whole of
this part. The system must also keep its hook attached and its trusted state trusted.
These properties ride on platform security primitives that are a function of the OS
generation; where a primitive is absent the boundary it guards descends (VII.5) and
the descent is recorded, never silently assumed.

### VII.1 Keystore secrecy, integrity, and persistence
**[DECISION] VII.1.1.** The authoritative keystore and the whitelist live in the
minifilter's **kernel non-paged pool**, which a user-mode actor — even SYSTEM — cannot
read without kernel-code execution, by the CPU's user/kernel privilege boundary
(VII.2). The on-disk keystore (needed because recovery may occur after the attack or
after reboot) is **integrity-sealed with a keyed MAC computed and verified in kernel**,
is append-oriented, and is tamper-evident so that any edit, reorder, truncation, or
rollback of captured keys is detected on load (the external anchor of VII.1.2 closes
rollback). **Confidentiality of the on-disk copy is exactly the strength of the
platform seal:** where the platform seals the key (VII.1.2) the records are encrypted
under that sealed key and an attacker without kernel-code execution cannot read
captured keys; where the platform cannot seal, the on-disk copy is integrity-protected
but its confidentiality descends to the kernel-line boundary and the descent is
recorded (VII.5). The live in-kernel copy is confidential by the privilege boundary
regardless, and no operation exports a captured key to user mode — recovery itself
decrypts in kernel (III.1.2). The MAC key is held in kernel pool; it exists in user mode
only inside the single bounded boot-time unsealing window of VII.1.2, and where the
platform cannot seal it its persisted secrecy descends to that same boundary and is
recorded.

**[DECISION] VII.1.2.** The seal is rooted in the platform, never synthesized in
software. A Windows kernel driver has no TPM access — the TPM is reached only through
user-mode TBS — so where the platform provides a TPM the MAC / keystore key is
**TPM-sealed under a PCR policy and unsealed once at boot by the PPL-protected
service**, which delivers it to the kernel over the authenticated comm channel
(VII.3) and zeroizes its user-mode copy immediately; the gating application-PCR is then
extended so no later code can re-unseal it in that boot session. Where a VBS enclave is
present it may hold the sealing key in VTL1 in place of that service window. The
external rollback anchor (`{generation, head_mac}`) is held apart from the keystore
file — in a TPM monotonic-counter NV index where available, else in the sealed key
blob. Where neither a TPM nor a VBS enclave is present, the regression terminates at
the highest residence the platform allows and the system records that the
hardware-rooted guarantee is not claimed there (VII.5).

### VII.2 Kernel-pool secrecy precondition
**[DECISION] VII.2.1.** Reading the minifilter's pool from user mode requires
kernel-code execution — this is the CPU's user/kernel privilege boundary and holds
whether or not HVCI is present. HVCI's role is upstream: by blocking unsigned and
vulnerable kernel code it makes *obtaining* that kernel-code execution harder, keeping
the attacker inside the in-scope user-mode zone (Part VIII). Where HVCI is absent on a
supported OS the pool's secrecy against a user-mode attacker is unchanged, but the ease
of escalating past the kernel line rises; that descent is recorded (VII.5), not
silently weakened and not synthesized in software.

### VII.3 The observation point stays attached and authentic
**[DECISION] VII.3.1.** The minifilter loads boot-start at an appropriate altitude,
attaches by policy to all volumes (including ReFS / Dev Drive where present), and the
service runs as a protected process (PPL) where the platform supports it. The kernel
authenticates the user-mode client connecting to the filter communication port
(verifying protection/signing level and expected identity), and the port's security
descriptor restricts access to that identity. Accepting the first connector and
granting it trusted-channel status is a trust-bootstrap hole and is forbidden.

### VII.4 Our own process is where hijack-prevention lives
**[DECISION] VII.4.1.** Code-integrity guards (e.g. CIG / ACG where the platform and
the process model allow) and PPL protect the **system's own** service and driver
against injection. This is OS-provided, incomplete, and explicitly bounded by the
kernel line (Part VIII). It is **not** extended to arbitrary user processes, and it is
**not** required for the recovery guarantee — universal capture already covers a
hijacked non-whitelisted process (VI.3.1).

### VII.5 OS-generation scoping
**[INVARIANT] VII.5.1.** Each self-protection primitive (HVCI, TPM stack, PPL,
Dev Drive policy) is detected at runtime. Where present it is used; where absent the
dependent boundary descends exactly as far as the missing primitive would have raised
it, and the descended posture is recorded **explicitly**. The recovery mechanism (key
capture) does not depend on any of these and is unaffected.

> **[NEGATIVE] VII.5.2** Do not frame a missing platform primitive as a defect of this
> system or of the OS, and do not synthesize a missing hardware guarantee in software.

---

## PART VIII — BOUNDARIES (declared non-defenses)

Two lines bound this system. The default state of any newly found gap is closed unless
it escapes both (0.3).

### VIII.1 [BOUNDARY] Kernel-code execution is game over
A user-mode service plus minifilter structurally cannot defend against an
equal-or-higher-privilege kernel attacker: such an attacker bypasses the hooks, reads
the pool, and can destroy the keystore. Every "what if the attacker gets below us"
question — inducing a capture-read to fail before the pre-op, bypassing the hook,
reading the kernel pool, deleting the keystore directly — reduces here.

> **Rationale.** This is the architectural ceiling of all minifilter defenses, not a
> defect of ours. Defending past it is impossible by construction; attempting to is the
> slope. Keys captured before the compromise instant remain usable; the cost of a
> compromise is loss of *future* observation, not retroactive undoing of past captures
> (subject to keystore survival, VII.1).

### VIII.2 [BOUNDARY] The confirmed limit — Oracle-miss is unprotected
If the Oracle does not capture the key, the file is not protected. The following all
reduce here and are **not** new open problems:

- **Key isolation** (SGX enclaves, Key Locker): the key never enters scannable
  memory/registers. This is the structural-`p≈0` family; the field confirms it is rare
  in the wild because it costs the attacker reliability and operational burden.
- **Per-file keys never captured** at any of a file's writes (a unique key, every
  write's scan cold). cumulative-N across the file's chunk-writes makes this small, but
  it is not zero, and it is accepted.
- **An intermittently encrypted region whose write was never convicted**: under partial
  encryption each ciphertext slice is an independent capture opportunity (II.4.2), but a
  slice whose key was not recovered at its write leaves *that region* as on-disk
  ciphertext. Recovery restores the captured regions and leaves the rest byte-for-byte —
  a **partial recovery** (III.4.1), never a corruption of the surrounding plaintext.
  cumulative-N over the campaign's writes shrinks the uncaptured set; whatever remains
  reduces here.
- **Read/write split across processes** that defeats (`P`,`C`) pair construction: this
  delays or denies the conviction *accelerator*, not capture itself — the key is in the
  writing process's memory and is captured there. Where it does deny capture, it
  reduces to this boundary.
- **Structureless keys outside the bounded trial**: stream-cipher and raw block-cipher
  master keys carry no schedule structure (II.4.1), so they are located only by bounded
  `(P,C)` trial over the snapshot; a key whose memory location falls outside that budget
  on every capture-eligible write is a miss. The structural scan removes this cost for the
  AES-schedule case but not for structureless keys.
- **Key resident only shorter than the sampler can reach**: the memory capture is a prompt
  off-IRP snapshot at the write plus a behavior-paced re-sample of the active writer (II.3.2),
  never an in-IRP scan; together they hold a key in view for as long as it is resident, so a key
  reused or held across the encryption window is caught even when it is freed before any single
  write's snapshot — and under ENFORCE one such capture is enough to convict and block, bounding
  the damage of a pre-write-zeroizing attacker. What still reduces here: a key computed, used, and
  zeroized between writes within a window shorter than any sample in it; and, distinctly, a
  *per-file* key under pre-write zeroization — because conviction is the forward proof and that
  key's only matching (`P`,`C`) is its own write, at which the key is already gone (a sibling
  file's (`P`,`C`) is a different key), so the re-sample, even when it sees the key resident,
  cannot convict it. A key *reused* across files escapes this, since any sibling's (`P`,`C`)
  convicts it; the immediate per-file zeroize of a tiny file does not. The behavior-paced cadence
  and the synchronous register grab shrink the window; whatever still falls outside it reduces
  here.

> **Rationale.** This is the honest edge of a key-capture defense, and it is the
> ratified premise of the whole system (Part I): the Oracle is the protection, and where
> the Oracle cannot see, there is no protection. Engineering "around" this would
> re-introduce retain-everything, which is rejected.

### VIII.3 [BOUNDARY] Wiper / no-key destruction
Plaintext-over-plaintext destruction, pure deletion, and zeroing produce no recoverable
key and are not targets. A wiper is not viable ransomware (no key, no ransom). Disguised
encryption still fails the conditional test toward the high end and is captured like any
encryption; a genuine no-key wipe is out of scope.

### VIII.4 [BOUNDARY] Out-of-volume / geometry corruption
Partition-table or out-of-volume corruption makes a volume unaddressable but does not
destroy file *data* (recoverable by partition repair). Defending partition geometry is a
different product.

> **[NEGATIVE] VIII.5** Do not add any defense past these lines, and do not escalate
> threat vectors above them. A scenario that requires kernel access, or that is simply
> "the Oracle did not capture the key," is closed here — not an invitation to invent a
> new mechanism.

---

## PART IX — COMPATIBILITY AND DEPLOYMENT

### IX.1 [DECISION] Single binary, runtime feature detection
One source tree, one binary per CPU architecture, targeting every minifilter-capable
Windows release. Newer DDIs / structures / info-classes are visible at compile time and
gated at runtime; any routine that would otherwise fail to resolve on a down-level
kernel is resolved at runtime with a down-level alternative.

> **[NEGATIVE] IX.1.1** No dead code and no compatibility fallback for its own sake. A
> down-level branch exists only where the up-level path would fail to load or function on
> a supported OS. A superset of operation callbacks, FSCTL codes, and info-classes is
> correct: a code an OS never issues never reaches the handler and is inert there.

### IX.2 [DEPLOY] Load, signing, and primitive preconditions
- The driver is linked with `/integritycheck`; the boot-start signature is embedded in
  the `.sys`. Failure of the process-notify registration (which the
  identity-at-creation logic of V.2.2 depends on) is fatal-to-feature, not swallowed.
- Self-protection primitives (HVCI, TPM 2.0 + kernel TBS, PPL, Dev Drive policy) are
  runtime-detected; where present they are verified/configured, where absent the
  dependent boundary descends (VII.5) and is recorded.
- The supported floor is a recorded deployment decision. The recovery mechanism is
  deliverable down to the FltMgr / CNG / process-notify-Ex floor; the self-protection
  model is intact only from the OS generation that supplies its primitives. The
  recommended production floor is the lowest point where the self-protection model holds
  and a single attestation/WHCP-signed binary suffices.

---

## PART X — CONFORMANCE CHECKLIST

An implementation is constitutional iff all hold.

**The heart (Parts I, II)**
- [ ] Protection is key capture; no mechanism protects data when the Oracle fails (I.1).
- [ ] Conviction is forward-only (`Encrypt_K(destroyed original) == written`); the
      reverse never convicts; the system cannot convict its own recovery (II.2).
- [ ] The Oracle anchors capture to the writer but reads its memory off the IRP, on a
      behavior-paced cadence: a prompt deferred worker attaches to the still-live writer (under
      exit synchronization) and copies bounded resident committed-private regions (heaps first,
      then the rest), and while the process stays an active destructive writer a bounded-rate
      worker periodically re-samples it (progress-paced + wall-clock revisit) so a key resident
      between writes is still read; the structural scan, cipher battery, and forward proof run on
      each frozen snapshot; nothing enumerates or scans the writer's memory from inside the write
      IRP (II.3).
- [ ] Structural key-schedule location and round-key inversion are implemented; conviction
      stays the forward proof, never key-in-hand; cumulative-N is per-key capture-opportunity
      over both writes and resident-window samples + keystore accumulation, never backup
      accumulation (II.4).
- [ ] The keystore holds one record per encrypted region — (key, file, {offset,length})
      with that region's parameters and verification tag; a key's regions (across reused
      files or scattered within one file by partial encryption) each keep their own
      anchor, so the whole set recovers and no region is silently denied (II.5.1).

**Recovery (Part III)**
- [ ] No original is preserved as a recovery source; recovery is decryption of the
      on-disk ciphertext with the captured key (III.1).
- [ ] Recovery decrypts in kernel; neither the captured key nor the recovered plaintext is
      exported to user mode; the port carries only key_id + target path + status, and the
      writeback is a metadata-preserving atomic replace (III.1.2).
- [ ] Enumeration projects only the non-secret {key_id, algorithm, mode, provenance} tuple
      from the keystore, post-handshake, with no user-mode catalog shadow; one entry per
      captured file (III.1.3).
- [ ] Recovery verifies each captured region against its own capture-time tag and writes
      back only passing regions, leaving every other byte exactly as on disk (so an
      intermittently encrypted file's plaintext spans are never corrupted); if no region
      verifies it declines and leaves the file byte-for-byte intact (III.4).
- [ ] The only transient is the in-memory Oracle input; under pressure it is dropped (a
      missed attempt, confirmed limit), never spilled to disk and never held on the IRP
      (III.2).

**The gate (Part IV)**
- [ ] The gate is `D ∧ T`; D requires destruction of an existing original; every
      enumerated destruction member (legacy and `Ex`) is a capture candidate (IV.1).
- [ ] T is conditional novelty = 1 − coverage on the written range, floor `θ = 0.10`; it
      is direction-blind and is not a classifier; absolute-entropy / entropy-delta /
      ML-as-gate / always-pass are absent (IV.2).
- [ ] The gate never blocks (IV.3).
- [ ] `gzip`-style compression is handled as an ordinary delete-plus-new-file candidate
      with no special case (IV.1).

**Response and authority (Part V)**
- [ ] Capture is always on; mode (AUDIT / ENFORCE) governs blocking only; the mode
      transition is the user's, never inferred (V.1).
- [ ] No capacity limit, no saturation, no key-less auto-block; a block originates only
      from a forward conviction under ENFORCE (V.1.2).
- [ ] Whitelist is a full exemption (no capture, no adjudication); it is a user-owned
      gap, matched on creation-time verified identity, narrow and revocable; the default
      is no whitelist (V.2).

**Identity (Part VI)**
- [ ] Capture never depends on requestor identity (VI.1); identity is used only for the
      whitelist exemption (VI.2); hijacked non-whitelisted processes are fully covered; no
      injection-detector is used as a gate (VI.3).

**Self-protection (Part VII)**
- [ ] Keystore is in kernel pool; the on-disk copy is keyed-MAC'd in kernel; it is
      TPM-sealed where available and tamper-evident against key erasure (VII.1).
- [ ] The comm-port authenticates the client; the first connector is not auto-trusted
      (VII.3).
- [ ] Each primitive is runtime-detected; absence descends the boundary explicitly
      (VII.5).

**Boundaries (Part VIII)**
- [ ] No defense is added past the kernel line or the confirmed limit; threats are not
      escalated above them (VIII.5).

**Compatibility (Part IX)**
- [ ] One binary per architecture; down-level branches only where a routine would
      otherwise fail; no fallback for its own sake (IX.1).

**Process (Part 0)**
- [ ] Every quantitative claim is tagged MEASURED / DERIVED / DESIGN and none is deferred
      to runtime configuration (0.2, 0.5).
- [ ] No CLOSED conclusion is reopened on a basis already inside the boundaries of
      Part VIII (0.3).

---

### Closing note (non-normative)

This document has one load-bearing sentence (Part I) and nothing that contradicts it.
The gate asks the Oracle a question; the Oracle answers by capturing a key; the key
recovers the file; the user decides when a proven encryption is an attack; and where the
key is never caught, the system says so instead of pretending otherwise. Everything the
old design spent on preserving originals, sizing shadow stores, counting saturation, and
hedging against the Oracle is gone — not refactored, gone — because each of those existed
only to distrust the one sentence this system is built to trust.