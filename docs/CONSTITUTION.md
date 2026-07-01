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

1. Does it require kernel-code execution? → **CLOSED** by Part IX (boundary).
2. Is it a case where the Oracle simply does not capture a key? → **CLOSED** by
   Part IX (confirmed limit).
3. Is it already governed by an existing [INVARIANT] or [DECISION]? → **CLOSED.**

If and only if it escapes all three is it a genuine open item.

> **[NEGATIVE]** Do not escalate threat vectors indefinitely. "A trusted
> administrator who decides to attack," "the Oracle might be bypassed," "the
> capture read might be induced to fail without kernel access" are not defended.
> Defending the impossible is the slope this project is defined against. Saying
> *less* and defending *narrowly* is correct here, not a weakness. The same
> discipline governs what is **recorded** as a limit: a declared boundary earns its
> place only when it is non-obvious, specific to this design, and able to change how
> the system is deployed or trusted. A tautology ("nothing is protected before the
> driver loads"), a boundary the entire class of filesystem filters shares (a
> direct-access volume mapped to byte-addressable memory raises no I/O for any filter
> to see), or a self-imposed implementation constant restated as fate are not limits
> and are not written here — recording them is the same noise as defending them.

### 0.4 What vs how
This document specifies **what must be true**, precisely enough to be mechanically
checkable. It does not specify **how** (data structures, libraries) except where a
how is itself load-bearing and promoted to a [DECISION]. Implementation freedom is
real; constraint satisfaction is mandatory.

### 0.5 No runtime-tunable detection; a resource envelope the user may own
Every value that governs **detection or conviction** — the gate threshold, the cipher
battery, the capture trigger, what counts as a destructive write — is a recorded design
decision derived now, not a knob exposed to runtime configuration. Where such a value
sits inside a derived safe band, the band and its safety are [DERIVED] and the chosen
point is [DESIGN]; both are recorded here, neither is deferred to "measure later."

A **resource envelope** is the one thing the user may set: the time and capacity bounds
of the preservation window (Part III, Part VI). This is not detection logic and changing
it never changes what the system proves or convicts — it sizes how much reversible
holding the user is willing to spend, exactly as a backup retention policy does. The
default is a recorded [DESIGN] value; the user's authority over it is deliberate and is
distinct from the forbidden tuning of detection.

---

## PART I — THE GOVERNING PROPOSITION

Everything in this document is this sentence, unfolded:

> **The strength of the response is proportional to the certainty of the evidence.**
> The system observes every destruction of an existing original and answers it with
> recovery and, under the operator's policy, prevention — graduated to what it can
> prove. When it captures the encryption key, the evidence is definitive: the file is
> recovered with certainty by decrypting the ciphertext already on disk, for as long as
> the key is kept, and a proven encryption may be blocked at its first instance. When it
> cannot capture the key, the evidence is only circumstantial — a destructive,
> unpredictable write by an unexempted process — and the response is correspondingly
> reversible: the original it was about to destroy is held in a bounded window so the
> destruction can be undone, and prevention waits until that window is exhausted. Where
> neither the key nor the original can be held, the system does not protect that data —
> and says so plainly.

This is the heart. Protection has **two assets, ranked by the certainty of the evidence
that feeds them**: the **Oracle** (key capture, Part II) is the definitive asset and the
only one whose recovery is unbounded; **preservation** (bounded copy-on-first-write,
Part III) is the circumstantial fallback for the residue the Oracle cannot reach. The
rest of the document is their detail. A third evidence channel — the **Phantom Witness
Layer** (Part VIII) — operates in parallel: virtual files that exist only in minifilter IRP
responses trap indiscriminate file-walking, providing behavioral evidence that is independent
of what the Oracle captures or preservation holds. Its evidence is cumulative (K independent
phantom touches convict) and feeds the response system as circumstantial conviction; it is
never a recovery source.

**[INVARIANT] I.1 — Response is graduated to evidence, never beyond it.**
The Oracle is the primary locus of protection; where it reaches, nothing else is needed,
because a captured key recovers every file under it without bound. Preservation exists
**only** for what the Oracle cannot capture, and it is deliberately the weaker asset:
bounded in time and capacity, reversible, and **never a basis for conviction**. The
system never responds more strongly than its evidence warrants — a captured key
(definitive) recovers and may block at its first instance; an uncaptured destruction
(circumstantial) is only held, reversibly, and provokes a block only when the bounded
resource it was given is exhausted under ENFORCE (Part V, Part VI). There is no response
that inverts this ordering: no preserved original treated as definitive, no conviction
from circumstantial evidence, no key in hand treated as a verdict without the forward
proof (II.2).

**What derives from the proposition.** The gate (Part IV) decides what to ask the Oracle
about and what to preserve. The Phantom Witness Layer (Part VIII) detects indiscriminate
file enumeration through virtual files that no legitimate process would touch, providing a
behavioral evidence channel parallel to the Oracle's cryptographic proof. Recovery
(Part III) is decryption with the captured key or restoration of the preserved original.
Response (Part V) is the system's evidence — from both the Oracle's cryptographic proof
and the Phantom Witness's behavioral proof — plus the user's intent. Identity discipline
(Part VI) ensures neither asset can be evaded by who issues a write.

**Why both assets exist, and why preservation is bounded.** The Oracle's reach is
irreducible: a key computed, used, and discarded faster than any memory snapshot can see
it — the regime of fast small-file and partial encryption, and of an adversary that
zeroizes the key before it writes — is not capturable (Part IX). Preservation covers
exactly that residue and only it, and it can, because the attacker must read the original
plaintext before it can encrypt: the original is therefore in hand at the destructive
write and can be held aside. It is **bounded** because holding originals without bound is
the retain-everything this system refuses; it is **reversible-only** because circumstantial
evidence must never convict. The Oracle relieves preservation — a captured key discards
the original it had held, which is no longer needed — so preservation spends its bounded
resource only where the Oracle failed.

**Where the proposition stops.** When neither the key was captured nor the original held
within its window — a destruction the Oracle could not see whose preserved original has
aged or been pushed out of the bounded store, or a write whose original could not be
copied at all — the data is not protected. This is the **confirmed limit** (Part IX):
the honest edge of a system that holds keys and a *bounded* window of originals, not an
infinite backup. It is stated rather than papered over.

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
capture target at that write: the write names the process to read, and because the key is resident
in committed-private memory at the moment the write executes, the Oracle reads that memory once —
deferred from the write — while the key is still live. The dominant case (a heap key object reused
across writes) means the key is resident at every write in the campaign; a key zeroed before the
deferred snapshot can reach it falls to the irreducible floor (IX.2). The write tells the Oracle
which process to read; each capture-eligible write is an independent capture opportunity.

**[INVARIANT] II.3.2 — The memory capture runs off the IRP; nothing scans the writer's memory from the write path.**
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
2. **Analysis on the frozen bytes.** The structural key-schedule scan, the cipher battery, and
   the forward (`P`,`C`) proof run on each copied buffer — never on live process memory and never
   on the IRP — so destruction of the live key after a snapshot cannot defeat what that snapshot
   already holds.
3. **Optional synchronous register grab.** Only the register file (XMM / `FXSAVE`) — kernel-saved,
   fault-free, no memory enumeration, no re-entrancy — may be taken during a short synchronous
   hold; it resists key destruction and process exit but is low-yield because registers are
   transient.
4. **The cipher-verification battery** (AES family incl. XTS, plus ChaCha/Salsa, 3DES, SM4,
   Camellia, ARIA, SEED, etc.) is offline on the snapshot and the in-message plaintext/ciphertext
   sample. It needs no live process.

> Because every snapshot is a prompt off-IRP copy and the analysis runs on frozen bytes, no timing,
> deadlock, or scan-fault can reach the IRP. The cost is the honest one: the deferred snapshot
> catches the key if it is still resident when the worker attaches — which is the dominant case for
> a heap key object reused across writes, since the key persists in committed-private memory across
> the encryption window. A key zeroed before the deferred snapshot can reach it — computed, used,
> and destroyed faster than the worker can attach, or the process exited before the snapshot — is
> not captured by the memory path (IX.2). The register grab provides a secondary channel for keys
> transiently in registers; what remains beyond both is the confirmed limit.

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
reduces to the confirmed limit (IX.2).

**[DECISION] II.4.2 — cumulative-N over writes.**
Each capture-eligible write is an independent capture opportunity for the key it uses. For a
single-opportunity capture probability `p > 0`, the chance of at least one capture across `N`
opportunities under that key is `1 − (1−p)^N → 1` rapidly. The relevant `N` is **per key** and it
accrues across the capture-eligible writes under the key. Where a campaign reuses one key across
many files, `N` is the whole campaign's writes and capture is near-certain; under per-file keying —
the dominant hybrid-envelope shape, where each file gets a fresh symmetric key wrapped by the
attacker's asymmetric key — `N` is the capture-eligible writes of *that file*, so a file encrypted
by very few writes has correspondingly fewer chances.

> **cumulative-N is load-bearing and precise:** it is *not* the accumulation of backups.
> It is the accumulation of
> (a) capture opportunities and (b) captured keys in the keystore. A key caught at
> *any one* of its writes recovers *every* file encrypted under that key — the whole set
> under key reuse, that one file under per-file keying — including data written before the
> catch. This is why a single missed write costs nothing within a key's write set: the
> recovery asset is the key, not a per-file backup.

### II.5 The keystore
**[DECISION] II.5.1 — The keystore is the definitive persistent recovery asset.**
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
append-oriented and must scale to many records. It is the center of gravity of the definitive
asset: if it survives, recovery by key is possible without bound; if it is destroyed, that
recovery is lost (the bounded preservation store, III.5, independently covers the residue it
did not capture). Its protection is a crown-jewel concern of Part VII.

---

## PART III — RECOVERY AND PRESERVATION (two assets)

### III.1 The recovery sources
**[DECISION] III.1.1 — Recover from a captured key, or from a bounded preserved original.**
Recovery has two assets, ranked by evidence certainty (I.1):

1. **The keystore and the on-disk ciphertext** — the definitive asset. The captured keys
   (II.5) decrypt the ciphertext the attacker already wrote, at no cost to us, with
   **unbounded** retention: a key recovers every region under it for as long as the
   keystore survives. This is recovery by *proof*.
2. **The preservation store** — the circumstantial asset (III.5). For a destruction whose
   key the Oracle did not capture, the original the write was about to destroy was copied
   aside — once, before the destruction — and held in a **bounded** window; recovery
   restores that copy. This is recovery by *holding*, bounded and reversible precisely
   because the evidence is only circumstantial.

The definitive asset is preferred wherever it applies and it **cancels** the circumstantial
one: when a key that recovers a region is captured at any time, that region's preserved
original is discarded (III.5.4) — it was only ever a hedge for the case the key was never
caught. Neither asset is the other's backup; together they are the graduated response of
I.1.

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

> **Why preservation is bounded, not infinite.** A preserved original answers exactly the
> case a captured key cannot: a region whose key was never captured at any of its writes.
> That verdict — "never captured" — never terminates, so an *unbounded* preserved copy
> would be retain-everything, which I.1 refuses. The resolution is the bounded window
> (III.5): the original is held only long enough to make the destruction reversible while
> the user can still notice and act, and beyond that window the confirmed limit (IX.2)
> applies honestly. The thing worth holding forever is still the key; the original is held
> only briefly, and only when the key was missed.

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
visible, not just a single representative. The preservation store is enumerated the same way
(III.5.6): per held original the kernel projects only the non-secret {provenance path, region
offset and length, capture time, age, size, status}, never the preserved bytes — so the
operator can see and direct what is reversibly held without the store ever leaving the kernel.

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
attempt — a missed attempt inside the confirmed limit (IX.2), never a read forced
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
never captured stays as on-disk ciphertext — an honest **partial recovery** (IX.2), the
same edge as the confirmed limit, never a destructive replace. This is the recovery
direction's analogue of the Oracle's forward proof: conviction never trusts a key in hand
(II.2.2), and recovery never trusts a decryption in hand.

The tag is a hash of a bounded original sample, never the sample itself — a check value
with no recovery role, and not a preserved original (III.1.1). It rides in the keystore
record and is covered by that record's keyed MAC (VII.1.1), and the Oracle captures one
for each file it observes, so each file recovers against its own anchor. Where recovery
is directed at a target for which no verification anchor was captured, it declines rather
than perform an unverifiable destructive replace — the same honest edge as the confirmed
limit (IX.2): the system replaces only what it can prove it is restoring.

### III.5 Preservation — the circumstantial asset

**[DECISION] III.5.1 — Copy-on-first-write, on the same gate, for the residue the Oracle cannot reach.**
A write that passes the gate (D ∧ T, Part IV) by an unexempted process is, by construction, a
destruction of an existing original whose content is unpredictable from that original — the same
population the Oracle is asked about. Before that original is destroyed, a copy of the destroyed
region is taken **once** and placed in the preservation store as a *probation* hold, bound to the
destroying actor. Preservation runs on the gate's output, never as a second classifier: it adds a
*holding*, it does not judge, it never blocks (IV.3), and a held original is never a conviction
(I.1). The two pools a hold may occupy — probation and protected (III.5.5) — are a resource-and-
retention distinction, not an evidence distinction: nothing about which pool a copy sits in is ever
read back as a signal.

**[INVARIANT] III.5.2 — The copy is taken off a deadlock-free path, never via the cached same-file read.**
The original is obtained without the cached in-IRP same-file read that wedges the volume (III.2.1),
from sources chosen by destruction shape:

1. **In-place overwrite — the write region read non-cached at the write seam, before the cached write
   lands.** In the write pre-operation, before the file system applies the write, the doomed region is
   read through **non-cached paging I/O** on the operation's own file object. Non-cached paging reads
   bypass the cache-manager recursion that makes a *cached* same-file read wedge (III.2.1) and return
   the committed on-disk original coherently — never a torn mix of pre- and post-write bytes — so the
   pre-image is captured intact in one synchronous read and frozen in a kernel buffer for the worker
   to stage. The copy is taken only when the write is *novel against the actor's own prior read* of
   that region: read → encrypt → write is causal, so the head of the write is compared to the head the
   actor last read, and a write that barely changes what was read is a benign identical rewrite, not a
   destruction, and never reaches the copy path. This causal pre-screen, not a second file read, is
   what keeps benign in-place rewriters off the preservation path.
2. **The doomed region, read at the destruction seam while still intact.** A delete, truncate,
   allocation-shrink, range-zero, block-clone, or ODX offload-write is observed in its pre-operation
   before the file system has processed it, so the region it is about to discard is still fully on
   disk; the destroyed extent — whole file for a delete, the shrunk tail for a truncate or allocation
   cut, the target extent for a clone or offload — is read by a worker through a distinct file object
   and staged. The read is gated on the object having been read by an untrusted actor (the read →
   destroy causality of encryption), so a content-less benign delete that never consumed the original
   is not held.
3. **A distinct file object, off the IRP — and, for a mapped section, ahead of the first store.**
   For a destruction whose original does not live on the handle in hand — a rename-over or
   hardlink-replace, where the doomed file is the *destination* — a worker copies the still-intact
   original through a *distinct* kernel file object, at PASSIVE level, holding no filesystem resource
   of the destroying operation: the destination of a replace is opened below our own instance and read
   before the rename commits, never as a held resource on the IRP.

   A mapped-section write has no write IRP at the moment it happens — a process maps the file writable
   and stores into memory, and the change reaches disk only later, as an asynchronous paging write
   issued by the system writer. That paging write is the wrong seam to copy from: it runs above
   PASSIVE, where reading the same file faults the memory manager, and it carries the ciphertext, not
   the original; the individual store carries nothing at all, raising no IRP. The original is read at
   the one seam the OS surfaces *before any store can occur*: the section-synchronization acquire
   (`ACQUIRE_FOR_SECTION_SYNCHRONIZATION`) that precedes a writable mapping. In that pre-operation —
   which runs before the file system acquires the file for the section, so it contends no section lock
   — a distinct **non-cached** kernel file object reads the whole still-clean region synchronously and
   freezes it for the worker to stage, before the mapping is usable. The mapped path is thus held by
   first-write-wins (III.5.3) like every other member, not best-effort: recovery for a mapped
   destruction becomes available once the encrypting flush lands, which is asynchronous, but no
   original is lost to the race.

Every destruction member of IV.1.2 that has a recoverable file region — in-place overwrite,
truncate, allocation-shrink, delete, rename-over, hardlink-replace, block-clone, ODX offload-write,
range-zero, file-level-trim, and mapped-section write — is a preservation trigger by the same
enumeration that makes it a capture candidate; the original is held under whichever source applies.
Volume-level destructions (volume lock, dismount) have no single-file region to hold and are met by
detection and the capacity response alone (V.1.2); making a file sparse destroys nothing by itself
(its zeroing arrives as a separate range-zero, which is held).

**[INVARIANT] III.5.3 — First write wins; a held original is never overwritten by what destroys it.**
The store holds **one copy per (file, region)** — the earliest observed state of that region within
the window. A second destructive write to a region already held (double encryption, a re-encrypting
pass, chunked rewrites) does not replace the held original: the original is the pre-attack plaintext
and the later write carries only ciphertext, so the store admits the first and rejects the rest for
that region. A region first observed as already ciphertext (encrypted before observation began) has
no true original to hold; that is the pre-observation edge, recorded, not a false hold.

**[DECISION] III.5.4 — A captured key discards the original it would have recovered.**
The preserved original hedges against the key being missed; the moment a key that recovers its region
enters the keystore — at a convicting write or via a sibling region under a reused key (II.4.2) — the hedge is spent and the held original for that region is discarded.
Reconciliation is driven by keystore append: each new record cancels the preserved originals its
(file, region) now covers. Preservation therefore consumes its bounded resource only on the residue
the Oracle never caught, and where the Oracle succeeds the store trends to empty. Beyond cancelling
the recovered region, a conviction **promotes**: when the Oracle forward-proves any write of an actor,
every remaining probation hold of that actor is moved to the protected pool (III.5.5). A proven
encryptor's originals whose keys were never captured are the residue most worth keeping; promotion
exempts them from capacity reclamation while leaving an unconvicted actor's holds in probation.

**[DECISION] III.5.5 — The window is a bounded resource envelope: time and capacity, across two pools.**
Held originals live under two bounds, a recorded [DESIGN] default the user may set as a resource
envelope (0.5, V.1.3): a **retention time**, after which any held original is reclaimed (the
destruction is old enough that the user has had the window to notice and recover), and a **total
capacity**, accounted by held-region bytes (II.5.1's region model, not a file count). Within that
envelope a hold sits in one of two pools:

- **Probation** — the default on staging (III.5.1). Probation absorbs the large benign population of
  high-novelty in-place rewrites (a compressor or transcoder rewriting in place, a database or image
  store) that the gate cannot tell from encryption and the Oracle never convicts. Probation is bounded
  by oldest-first slide and **excluded from the capacity-exhaustion block**: under pressure it yields
  silently, newest-first-kept, so a benign bulk rewrite never blocks the user and never evicts a
  protected original.
- **Protected** — entered only by promotion on conviction (III.5.4). Protected holds are reclaimed by
  retention age alone, never by capacity pressure, and it is the **protected** bytes against the total
  capacity that constitute the exhaustion condition.

What happens at protected-pool exhaustion is the response question and belongs to the mode (V.1.2):
AUDIT slides — commits the oldest protected original, accepting that beyond the envelope a
capacity-exceeding attack by an already-convicted actor is not prevented; ENFORCE blocks that actor.
Probation never reaches this question. The bounds size reversible holding; the pool a hold occupies is
never a detection or conviction signal.

A third bound sizes the **capture pipeline** itself: the off-IRP staging work in flight is capacity-
bounded, and under saturation — gated destructions arriving faster than they drain — excess captures
are **shed, not blocked**. The shed is deliberately fail-open: it yields an unconvicted actor's copy
rather than block the benign high-volume rewriters probation exists to absorb (III.5.1) — blocking
them is the false positive the two-pool design is built to avoid, and it is the more likely harm than
the burst it would prevent. The shed is bounded by the conviction asymmetry: sustained high-volume
destruction trips conviction (Part IV, VIII.4) and the actor is then blocked upstream and never
re-enters the pipeline, so a shed copy reaches a genuinely-malicious original only in an actor's
pre-conviction burst that *also* evades the phantom (Part VIII) and key (Part II) layers — the
stacked-residual corner of IX.2, not a standalone hole.

**[INVARIANT] III.5.6 — Restore is operator-directed, verified, and metadata-preserving; the store never leaves the kernel.**
Restoration of a held original is initiated by the operator, never automatically — circumstantial
evidence holds, it does not decide. Before a held original replaces on-disk bytes the kernel verifies
the copy against its own integrity tag and its (file, region) binding, decrypts it where it lives (the
store is encrypted at rest, VII.1.3), and writes a temporary file in which only the held regions are
restored and every other byte is left exactly as on disk; the service then performs the metadata-
preserving atomic replacement (III.1.2), receiving neither the preserved bytes nor the store key. A
held original that fails its integrity or binding check is not applied. The preserved bytes never cross
the communication port.

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
- mmap / section write — the destructive flush surfaces as an asynchronous paging
  write, but the original is captured at the writable-section acquire
  (`ACQUIRE_FOR_SECTION_SYNCHRONIZATION`) that precedes the first store (III.5.2).
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
> implement are never delivered and are inert there (Part X).

**[DECISION] IV.1.3 — Destruction without encryption is out of scope.**
A destruction that is not encryption (a plain delete, a wiper, a truncate-to-zero)
yields no recoverable key and is not a target of this system (Part IX, wiper
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

**[DECISION] IV.2.2 — The measure is diff-restricted conditional novelty, not absolute entropy.**
The signal is

```
    novelty  = 1 − coverage
    coverage = ( the changed bytes' overlapping 2-grams found in the
                 original block's 2-gram set ) / ( the changed bytes'
                 overlapping 2-gram count )
```

A 2-gram is *changed* when at least one of its two bytes differs from the co-located
original at the same offset. Coverage is measured only over changed 2-grams, in fixed
blocks (not a file average): bytes the writer left byte-identical to the original
contribute to neither numerator nor denominator. The judgment question is *"given the
original, is the newly written content predictable?"* — never *"is the new content
random?"*

Restricting the measure to the changed bytes is what makes partial and intermittent
encryption visible without a co-located original to compare against being optional. A
writer that leaves most of a block untouched and rewrites only a thin ciphertext
stripe cannot dilute the score with the unchanged majority: the unchanged bytes are
excluded by construction, and the stripe's 2-grams stand alone at novelty ≈ 1. This
holds down to a single stripe per block — a 16-byte cipher run in a 256-byte block
(6.25% of the block) fires exactly as a full-block overwrite does.

> **[NEGATIVE] IV.2.3** Absolute entropy, entropy-delta, an ML classifier as the
> gate, and an always-preserve / always-pass stub are all forbidden as the gate. The
> conditional measure is required precisely because absolute entropy misses the case
> that matters: a high-entropy original (already compressed or encrypted) overwritten
> by unrelated high-entropy ciphertext. An entropy gate sees "high → high" and
> passes; the conditional measure sees unrelated 2-gram sets and fires.

#### IV.2.4 The firing rule and its lower-bound mathematics (settled by derivation; do not reopen)
A block fires when **both** hold over its changed 2-grams: the novelty ratio is at or
above the skip threshold `θ`, **and** the count of novel changed 2-grams is at or above
a volume floor `μ`. The ratio test rejects in-place edits that rewrite bytes with
content drawn from the original's own 2-gram vocabulary; the volume floor rejects a
handful of high-novelty changed bytes (a point edit to a structureless file) that would
otherwise satisfy the ratio on a near-empty denominator. Encryption — which rewrites
whole regions, or thin stripes that recur across every block — clears both.

- **[MEASURED]** Full-ciphertext novelty (every byte changed) is ≥ ~0.97 across
  thousands of trials, including high-entropy originals (compressed → cipher,
  cipher → cipher); the mean is ~0.998 at a 256-byte block. A contiguous `k`-byte
  ciphertext run yields ~`k` novel changed 2-grams at novelty ≈ 1, independent of how
  much of the block is left untouched.
- **[DERIVED]** For uniform ciphertext, `E[novelty] = 1 − |S| / 2^16`, where `|S|` is
  the original block's distinct-2-gram count (≤ 255 at 256 B), giving `E[novelty] ≥
  0.996`. Because only changed 2-grams enter the ratio, the unchanged majority cannot
  pull the ratio down; a true ciphertext write therefore clears `θ` with probability
  `≥ 1 − exp( −2·n_c·( min_cipher_novelty − θ )^2 )` over its `n_c` changed 2-grams,
  on the order of `1 − 10^−90` once `n_c ≥ μ`. A benign point edit of `j` changed
  bytes produces ≤ `j+1` changed 2-grams and is rejected by the floor whenever
  `j + 1 < μ`.
- **[DESIGN]** The skip threshold is **`θ = 0.10`** and the volume floor is
  **`μ = 12` novel 2-grams** (≈ one 16-byte cipher block). `θ` sits at the low end of
  the derived safe band, biasing toward asking the Oracle (asking more is cheap;
  missing a real encryption is forbidden); `μ` is set just under the novel-2-gram
  yield of a single AES block so that the smallest unit of real encryption fires while
  sub-block point edits do not. The judgment unit is a small fixed block (256 B), used
  to locate the novelty slice within a large write; dilution is defeated by the
  diff restriction (IV.2.2), not by block size.

> **[NEGATIVE] IV.2.5** Do not push directionality into T (it would require the
> forbidden absolute-entropy measure and would inherit its false positives, false
> negatives, and encoding-based evasions). Direction lives in the Oracle (II.2),
> where it is free and blind-spot-free.

### IV.3 The gate never blocks
**[INVARIANT] IV.3.1.** The gate emits **capture-candidate-or-skip**, and a candidate feeds
both consumers — the Oracle (II) and preservation staging (III.5). It never blocks or alters
an operation; the write proceeds unchanged whether or not it is a candidate. A loose gate
cannot create a false block, because it cannot block at all, and it cannot corrupt a write,
because it never redirects one. Over-asking the Oracle and over-preserving are costs absorbed
downstream — the latter bounded and reclaimed (III.5.5); under-asking risks a missed capture
and a missed hold (confirmed limit). Tune toward over-asking. Blocking, where it happens at
all, is the ENFORCE-mode response (Part V), strictly downstream of a verdict or of capacity
exhaustion.

---

## PART V — RESPONSE AND USER AUTHORITY (two layers)

User authority is exercised on two **distinct** layers. Conflating them is the
central error this document is written against. One governs *blocking*; the other
governs *whether the system observes at all*.

### V.1 Layer one — global mode (governs blocking, not observation)
**[DECISION] V.1.1 — AUDIT and ENFORCE.**
Observation, key capture (the Oracle), preservation, recovery, and visibility are **always
on** in both modes. The mode governs only whether the system blocks autonomously:

- **AUDIT** — observe; capture keys; preserve; **never block autonomously.** The system
  records which originators would trigger a block, and recovers by key or by held original
  if asked. At capacity exhaustion the preservation window **slides** — the oldest held
  original is committed (III.5.5) — so AUDIT does not prevent a capacity-exceeding attack;
  it is the posture for discovering the whitelist and minimizing false positives, not a
  protective guarantee.
- **ENFORCE** — observe; capture keys; preserve; and **block the originator's further
  destructive writes** on either of the two triggers of V.1.2. The already-written,
  key-captured or held files remain recoverable.

The transition between modes is the **user's policy decision**. The system never infers
"now is the time to start blocking." This is the clean partition of responsibility: the
system mechanically proves *encryption* and reversibly holds what it cannot prove; the user
decides *when* proven or capacity-exhausting destruction should be treated as an attack.

> A legitimate bulk in-place encryption (e.g. a database enabling transparent encryption) is
> behaviorally identical to ransomware and is **not** special-cased (special-casing it would
> be the intent-inference Part VI forbids). It is captured and preserved like everything
> else; whether it is blocked is the mode's and the user's call. In AUDIT it proceeds and is
> recoverable; in ENFORCE the user has chosen to block unrecognized mass encryption.

**[DECISION] V.1.2 — Under ENFORCE a block originates from exactly three triggers, each matched to its evidence.**
1. **Forward conviction (definitive).** A forward Oracle proof (II.2) blocks the originator
   at its **first** instance — the strongest response, because the evidence is certain and
   the blocked files are independently recoverable by the captured key.
2. **Capacity exhaustion (circumstantial).** When the preservation window's capacity is
   exhausted by unreconciled held originals — destructions the Oracle never convicted — and a
   further such destruction would force the window to slide and lose an original, the active
   destructive writers are blocked rather than that original lost. This is the cautious
   extreme: a key-less block, it fires only at the bound the user set (V.1.3), only under
   ENFORCE, and it convicts nothing — it refuses to spend the last of a resource the user
   reserved for reversibility. Preservation never blocks below this bound; below it, it
   only holds.
3. **Phantom conviction (behavioral).** When a process accumulates K ≥ 3 independent
   content writes to phantom backing files — virtual files that no legitimate process would
   write to (Part VIII); reconnaissance such as listing or opening a phantom is not scored —
   the pattern establishes indiscriminate destructive
   file-walking. This is circumstantial evidence: stronger than capacity exhaustion (it
   identifies the specific destructive process rather than a general resource pressure) but
   weaker than the Oracle's key-based proof. The blocked process's files are recoverable
   only by the Oracle's captured key or a held original, not by the phantom layer itself.

The set blocked is the behavioral set of active destructive writers (VI.1), never a requestor
identity. No other quantity blocks: not a count, not a byte percentage, not elapsed time. The
capacity trigger is not a "fill up and it must be an attack" inference — it is the explicit
exhaustion of a declared resource envelope, and the choice it forces (block vs slide) is the
mode's, not the system's guess. The phantom trigger is not a heuristic count — it is the
structural impossibility of K independent false phantom touches (VIII.4.2).

**[DECISION] V.1.3 — The preservation resource envelope.**
The window's bounds (III.5.5) — a **retention time** and a **total capacity** — are a resource
envelope the user may set (0.5). Their default is a recorded [DESIGN] value; the band is the
user's to widen or narrow as a backup-retention decision, changing only how much reversible
holding is spent, never what the system detects or convicts or how it proves. It is the user's
only quantitative control, and it is a resource decision, not a detection knob.

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
T-passing write is a capture candidate regardless of requestor, and the capture trigger is
behavioral — derived from the writes themselves, never from requestor identity — so it only
ever *adds* capture and never withholds it, and where an attacker splits the work across many
short-lived processes every one that issues such a write triggers a capture snapshot. This is
not a limitation to work around; it is why capture is anchored to behavior, not identity.

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
> (Part IX).

---

## PART VII — SELF-PROTECTION (protect the two recovery assets and the observation point)

The keystore (II.5) and the preservation store (III.5) are the recovery assets; protecting
both is the whole of this part. The system must also keep its hook attached and its trusted
state trusted. These properties ride on platform security primitives that are a function of
the OS generation; where a primitive is absent the boundary it guards descends (VII.5) and
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

**[DECISION] VII.1.3 — The preservation store is encrypted at rest under the same kernel-held seal.**
The preservation store holds plaintext copies of user originals and is far larger than the
keystore, so it lives on disk, not in pool. It is **encrypted at rest in the kernel** — the
minifilter encrypts each held original with the platform's in-kernel symmetric primitives
before it touches the disk and decrypts only in kernel during a restore (III.5.6), under a
store key held in non-paged pool and rooted exactly as the keystore's MAC key is (VII.1.2):
unsealed once at boot by the protected service, delivered over the authenticated port, never
persisted in the clear. To any in-scope actor (user mode up to SYSTEM without kernel code) the
store is therefore ciphertext — the same confidentiality the keystore has — and its plaintext
is reachable only across the kernel line (IX.1). Each held original carries an integrity tag
and its (file, region) binding (III.5.6) so a tampered or substituted entry is detected and not
applied. The store key never leaves the kernel, and no operation exports a held original to
user mode.

**[DECISION] VII.1.4 — Integrity, anti-deletion, and anti-rollback of the on-disk assets against a SYSTEM actor.**
Both on-disk assets must survive an in-scope SYSTEM actor that can run the filesystem against
them. Three mechanisms, each bounded honestly:
- **Anti-deletion through the I/O manager.** The minifilter denies destructive operations
  (overwrite, truncate, delete, rename-over) targeting its own store and keystore paths from
  any unexempted requestor — the IV.1.2 destruction enumeration turned on the system's own
  files. This defeats every deletion routed through the filesystem stack (the
  `vssadmin`/`wmic`/scripted-delete class).
- **Tamper-evidence against offline edit.** Both files are sealed with a chained keyed MAC
  computed and verified in kernel, with an external freshness anchor (`{generation, head_mac}`)
  committed at **coarse checkpoint boundaries** to a platform monotonic root where present
  (TPM NV) or to the sealed-key blob where not. On load the anchor is compared to the chained
  head; a mismatch **halts and alerts** rather than trusting rolled-back or edited state. The
  anchor is committed at checkpoints, never per write, because a hardware monotonic root cannot
  sustain per-write updates; the guarantee is detection of rollback, not its prevention.
- **The honest residual (recorded, not closed).** A SYSTEM actor may lawfully dismount or lock
  the volume to reach the on-disk store beneath the minifilter without executing kernel code;
  the dismount is itself in IV.1.2 and is detected, and on remount the store's freshness is
  re-verified — but a SYSTEM actor determined to **destroy** the on-disk store can do so, as it
  can destroy any on-host backup. Confidentiality (encryption) and integrity (tamper-evidence)
  hold against this actor; **availability of the on-disk store does not fully hold against
  SYSTEM**, and the system's answer there is detection and a truthful report of lost
  reversibility, not a false claim of protection. This is the recorded edge where on-host
  holding ends and off-host immutability — a different product — begins; the keystore, being
  tiny, survives more readily, and a captured key recovers without the store at all.

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
the attacker inside the in-scope user-mode zone (Part IX). Where HVCI is absent on a
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

The service's protected status is the anti-malware PPL level, established by a signed
early-launch (ELAM) driver that registers the signer of the service binaries; this raises the
bar against ordinary user-mode tampering (no handle injection, termination, or user-mode debug
of the service). It is **defense-in-depth, not a boundary against SYSTEM**: the platform itself
treats an administrator as inside the trusted base, so PPL is used where eligible, and its
absence — or its bypass by a SYSTEM actor — descends to the kernel line (IX.1) and is recorded
(VII.5), never claimed as a guarantee against SYSTEM. The recovery assets do not depend on it.

### VII.4 Our own process is where hijack-prevention lives
**[DECISION] VII.4.1.** Code-integrity guards (e.g. CIG / ACG where the platform and
the process model allow) and PPL protect the **system's own** service and driver
against injection. This is OS-provided, incomplete, and explicitly bounded by the
kernel line (Part IX). It is **not** extended to arbitrary user processes, and it is
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

## PART VIII — THE PHANTOM WITNESS LAYER

The Phantom Witness Layer supplements the gate (Part IV) with an orthogonal evidence
channel: virtual files that exist only in minifilter IRP responses and are invisible to
trusted processes. A process that *writes* to a phantom provides evidence of indiscriminate
destruction — the behavioral signature of ransomware encrypting its way through a directory —
because an invisible decoy has no legitimate writer. Passive contact (listing, opening,
reading) is reconnaissance and is deliberately not scored: it has benign sources and this
system convicts destruction, not traversal. The evidence is cumulative: each content write to
a phantom raises a per-process counter, and conviction requires K independent writes (K ≥ 3),
so a single incidental write does not reach the threshold. The layer produces **only**
evidence; it does not block or convict on its own — it feeds the response system (Part V) with
an additional signal graduated to its certainty.

**[INVARIANT] VIII.0 — Kernel-privilege closure.**
The phantom witness layer closes every user-mode path by which a process could distinguish
a phantom from a real file. Without kernel-code execution (Part IX), a process that
enumerates a directory, opens a file, queries its attributes, reads the USN journal, or
inspects file identity receives results that are indistinguishable from those a real file
would produce. The closure covers the IRP response path (VIII.3), the metadata query path
(VIII.6), and the raw-access blocking path (VIII.3.3, VIII.9). A process that can load a
kernel driver or execute code in ring 0 can read the MFT directly and discover the
absence — this falls to the kernel-code boundary (IX.1) and is not defended.

This is the closure the layer is built to. Three user-mode surfaces are specified but not
yet active in the current build and are **declared residuals**, each enumerated at its
section and each backstopped by the core engine (Parts II–IV), which detects real-file
destruction independently of the phantom layer: USN-journal injection (VIII.8.1a), the
`FileAllInformation`/`FileHardLinkInformation` query classes (VIII.6.3), and structural
image validity for image-decoding selectors (VIII.7.3b). They do not breach data
protection; they let a sufficiently specialized user-mode actor *identify and avoid* the
decoy, after which it is caught by the core rather than by the phantom.

Three axioms constrain the design:

### VIII.1 Axioms

**[INVARIANT] VIII.1.1 — Physical absence (P.1).**
A phantom file has no MFT entry, no disk clusters, no USN journal history prior to the
minifilter's intervention. It exists **only** as entries injected into
directory-enumeration IRP responses, as synthetic file objects redirected through
STATUS_REPARSE to hidden backing files, and as synthetic USN records injected into
journal-read responses. The filesystem never learns of the phantom's existence; the
minifilter fabricates it entirely within the filter stack.

**[INVARIANT] VIII.1.2 — Indistinguishability (P.2).**
Every observable attribute of a phantom — name, extension, size, timestamps, content,
reported file identity, security descriptor — is drawn from the same distribution as
legitimate files in the containing directory. The name is deterministic but unpredictable
(VIII.2); timestamps and sizes are derived from the directory's real entries; content is
plausible for the chosen extension. No attribute may distinguish a phantom from a real file
to a process that has not been told which files are phantoms.

**[INVARIANT] VIII.1.3 — Transparency (P.3).**
Trusted processes — those on the whitelist (V.2) and the system's own processes — never
see phantoms. The minifilter suppresses phantom entries in directory enumerations, rejects
phantom creates, omits synthetic USN records, and hides phantom metadata for all trusted
processes. The phantom layer is invisible to normal operation and adds no files to any view
a trusted process sees.

> **[NEGATIVE] VIII.1.4** Do not break transparency to gather evidence against a trusted
> process. A whitelisted process is exempt (V.2); undermining that exemption through the
> phantom layer is the same forbidden re-assumption of risk the identity discipline
> prohibits (VI.3.2).

### VIII.2 Phantom file generation

**[DECISION] VIII.2.1 — Keyed realistic naming, recomputed for recognition.**
The phantom name for index `i` in a directory is *generated*, never stored:

```
seed  = HMAC-SHA256(volume_secret, canonical_dir_path || i)
stem  = G_stem(seed)
ext   = palette[ seed mod |palette| ]
name  = stem "." ext
```

where `volume_secret` is a per-volume key derived from the keystore MAC key and the volume
serial number at keystore load, held in kernel pool alongside the MAC key (VII.1); `G_stem`
is a deterministic generator that emits a pronounceable, file-like stem (syllabic
consonant–vowel structure with occasional numeric or compound-word templates) of varying
length and shape; and `palette` is a fixed set of common, high-value file extensions
(VIII.2.4). The construction is:

- **deterministic** — the same `(volume_secret, directory, index)` always yields the same
  name, across reboots, with no stored per-directory state;
- **unpredictable** — without `volume_secret` the name cannot be computed (HMAC), so the
  phantom set cannot be enumerated, forged, or brute-forced by a key-less process;
- **fingerprint-free** — there is no fixed name shape to filter on (a fixed hex-string form
  would itself be a fingerprint), so a process that knows the algorithm still cannot
  distinguish a phantom from a real file by the form of its name;
- **collision-safe** — the stem carries enough keyed entropy that a real user file
  independently colliding with a phantom stem is negligible (an integrity concern, since a
  collision would shadow a real file, so the keyspace floor is set by collision-avoidance,
  not by guessing-resistance, which the key already provides).

Recognition is by **recomputation, not lookup**: at a create whose name could be a phantom,
the minifilter recomputes the directory's `name[0..K)` and compares case-insensitively
(matching filesystem semantics). No name cache or persistent naming state exists. A cheap
extension-in-palette pre-filter — not a phantom fingerprint, since real files share those
extensions — gates the recompute so the common create path is untouched.

**[INVARIANT] VIII.2.1.1 — `canonical_dir_path` is the volume-relative path.**
The name is derived at two independent sites: directory enumeration (VIII.3.1), where the
target directory is open and its name resolves to the full normalized form
(`\Device\HarddiskVolumeN\docs`), and the pre-create that recognizes a phantom open
(VIII.7), where the target is not yet opened and the volume device prefix is **not
resolvable** — only the volume-relative form (`\docs\`) is available. For the two sites to
produce the same name, both reduce the path to a single canonical form before hashing:
the device prefix (`\Device\<volume>\`, when present) is stripped and any trailing
separator is removed, yielding the volume-relative path with no trailing separator
(`\docs`). The per-volume `volume_secret` keeps names unique within the volume despite the
dropped prefix; cross-volume name reuse is immaterial because backing files redirect
per-directory (VIII.7) and conviction is per-process (VIII.4.4).

> **[NEGATIVE] VIII.2.1.2** Do not derive the name from the full device path at either
> site. The pre-create site cannot reconstruct the device prefix, so a full-path derivation
> makes the enumeration name and the open-recognition name disagree and the phantom becomes
> an inert decoy that is never recognized when touched.

**[DECISION] VIII.2.2 — Density rules.**
The number of phantoms per directory scales with directory population:

| Real file count | Phantom count |
|:-:|:-:|
| 0 | 0 |
| 1–5 | 1 |
| 6–20 | 2 |
| 21+ | 3 |

Empty directories receive no phantoms. Three is the ceiling.

> **[NEGATIVE] VIII.2.3** Do not inject more than 3 phantoms per directory. Higher density
> increases the false-positive surface without improving detection and risks violating the
> indistinguishability axiom (VIII.1.2).

**[DECISION] VIII.2.4 — Extension from a keyed palette; content materialized.**
The extension is selected deterministically — `palette[seed mod |palette|]` (VIII.2.1) —
from a fixed palette spanning the common, high-value file types ransomware targets
(documents, spreadsheets, presentations, PDFs, images, archives, media, databases). It is
**not** sampled from the directory's live contents: a content-derived extension could not be
recomputed at the recognition, metadata-query (VIII.6), and USN (VIII.8) sites, which see
only the backing handle and not the directory's files. A comprehensive palette closes the
"encrypt only the types you don't cover" bypass for every valuable type; per-directory
variety comes from the key, not from sampling. (Residual: the extension follows the key, not
the directory's exact present mix, so a single-type directory may host a phantom of a
plausible but different common type — a defense-in-depth tell, not a breach, bounded by the
conviction asymmetry.)

The backing file is **materialized on first access** (VIII.7.3) so the phantom is openable
and reads back consistently:
- a valid magic / structural header for the chosen type at the front, so a header- or
  image-validating selector accepts it;
- a body of **low-entropy** synthetic bytes filling to the reported size — low entropy is
  required, since a high-entropy body would look already-encrypted and be skipped by an
  entropy-screening encryptor, which would then never write the phantom and never convict
  (VIII.4);
- timestamps set to the phantom's keyed metadata (VIII.2.5).

The body is synthesized from the per-volume key alone (a keyed deterministic byte stream
shaped to a low-entropy distribution); it never copies any real user file — a verbatim copy
would both duplicate-content-tell the phantom and place real data in the hidden store.

**[DECISION] VIII.2.5 — Per-phantom metadata is keyed, varied, and plausibly aged.**
Each phantom's reported size and timestamps derive deterministically from
`HMAC-SHA256(volume_secret, canonical_dir_path || i)`: size from a realistic range above
common ransomware size floors (never a uniform constant); creation/modify/access times
spread across a believable past window (never the deploy instant), ordered
create ≤ modify ≤ access. The same values are reported in enumeration **and** set on the
materialized backing, so an attacker who screens by size or recency without opening, or who
opens the handle and queries times, sees consistent real-file-like metadata. Uniform or
freshly-stamped metadata is both a tell and a metadata selection filter that would route
around the entire phantom field without ever touching a backing file.

### VIII.3 Interception surface

The minifilter intercepts four priority tiers of I/O to maintain phantom consistency.
Higher tiers close progressively rarer detection vectors. All four are mandatory for the
kernel-privilege closure of VIII.0.

**[DECISION] VIII.3.1 — P0 (core IRP).**
- **IRP_MJ_DIRECTORY_CONTROL / IRP_MN_QUERY_DIRECTORY (swap-buffer):** the pre-op swaps in
  a private NonPagedPool buffer (with its MDL) so the filesystem fills it instead of the
  caller's; the post-op rebuilds the caller's buffer as a **sorted merge** of the real
  entries and the phantom entries, preserving 8-byte alignment and NextEntryOffset linking
  (generic offset helpers abstract the layout across info classes). The merge is:
  - **sort-correct** — each phantom is inserted at its collation position relative to the
    filesystem's own returned entries (compared with the system case-insensitive
    comparison), never appended at the end where its order would betray it;
  - **emit-once** — phantoms are injected exactly once per enumeration, tracked in a
    per-handle `FLT_STREAMHANDLE_CONTEXT` and reset on `SL_RESTART_SCAN`, never duplicated
    across the multiple `QUERY_DIRECTORY` calls of one enumeration (a duplicate would be an
    immediate tell, since real files do not repeat across chunks);
  - **pattern-honoring** — the first-call search expression is captured and a phantom is
    presented only when its name matches it (`FsRtlIsNameInExpression`), so a filtered query
    never returns a non-matching phantom.

  **Every** directory-information class the object store will answer is handled identically,
  so a process cannot diff one class against another to find an entry that appears under one
  but is absent under another — a cross-class detection oracle. The covered classes are
  FileDirectoryInformation, FileFullDirectoryInformation, FileBothDirectoryInformation,
  FileNamesInformation, FileIdFullDirectoryInformation, FileIdBothDirectoryInformation,
  FileIdExtdDirectoryInformation, FileIdExtdBothDirectoryInformation, and
  FileIdGlobalTxDirectoryInformation; the ID-bearing classes receive a synthetic 64-bit and
  128-bit FileId (VIII.8). (Residual: the Windows 11 23H2 `FileIdAllExtd*` classes are added
  when that platform floor is targeted; they are not queryable below it.)
- **IRP_MJ_CREATE (pre-op):** When an untrusted process opens an *existing* phantom name
  (any disposition that opens or overwrites), the minifilter materializes the backing if
  absent (VIII.7.3) and redirects to it via STATUS_REPARSE (VIII.7). A pure `FILE_CREATE`
  (create-new) of a phantom name is **yielded** — it is benign by construction (creating a
  new file is not destruction of an existing original, V/IV), so the minifilter does not
  reparse and lets the real file be created; the negligible-probability name collision is
  resolved in favor of the user's data. A trusted process opening a phantom name receives
  STATUS_OBJECT_NAME_NOT_FOUND. DASD opens (empty filename on a volume device) by untrusted
  processes are denied (VIII.3.3).
- **IRP_MJ_READ, IRP_MJ_WRITE (pre-op):** For handles redirected to phantom backing
  files, reads return backing-file content; writes increment the per-process phantom
  evidence counter (VIII.4).

**[DECISION] VIII.3.2 — P1 (metadata).**
- **FSCTL_ENUM_USN_DATA:** The minifilter injects synthetic USN_RECORD_V2 entries for
  phantoms whose parent directory appears in the NTFS output, using synthetic
  FileReferenceNumbers in a reserved range (VIII.8).
- **FSCTL_READ_USN_JOURNAL:** Sequential journal reads receive synthetic USN records for
  phantoms, timestamped consistently with the phantom's declared creation time.
- Both FSCTLs go through IRP_MJ_FILE_SYSTEM_CONTROL and are intercepted in post-op.
  Trusted processes are exempt — the minifilter suppresses synthetic record injection for
  them to avoid breaking system services that monitor the journal.

**[DECISION] VIII.3.3 — P2 (advanced).**
- **FSCTL_GET_NTFS_FILE_RECORD:** Denied for untrusted processes. No shipping ransomware
  uses this path; the block is a closure.
- **DASD opens:** IRP_MJ_CREATE on a volume device object (empty filename, no related
  file object) is denied for untrusted processes. A successful DASD open permits raw disk
  reads that bypass the minifilter stack entirely; blocking the handle prevents the gap
  from opening.

**[DECISION] VIII.3.4 — P3 (memory-mapped).**
- Memory-mapped file access to phantom backing files is observed through
  IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION. The section creation is an evidence event
  (VIII.4).

> **[NEGATIVE] VIII.3.5** Do not attempt to intercept below the minifilter stack. Raw disk
> reads after a DASD open, direct NTFS metadata manipulation via kernel drivers, and
> hardware DMA are beyond the minifilter's reach and fall to the kernel-code boundary
> (Part IX). The DASD-open block (VIII.3.3) is the minifilter's last line.

### VIII.4 Conviction mechanism

**[DECISION] VIII.4.1 — Cumulative K-threshold conviction on the encryption-chain touch.**
The phantom layer maintains a per-process evidence accumulator
`phantom_evidence[pid]`, initially zero. The single event that increments it by 1 is a
**content write to a phantom backing file** — the encryption-chain touch of a decoy. Merely
listing a directory, opening, querying, or reading a phantom is reconnaissance and is **never
scored**: passive traversal has too many benign sources (backup, search indexing, a one-off
`grep`) to carry evidence, and it is destruction, not enumeration, that this system convicts.
An invisible decoy has no legitimate writer, so a content write to one is near-conclusive on
its own; the K-threshold exists only to absorb the rare incidental write (a wildcard tool that
happens to overwrite a decoy that surfaced in a listing). When `phantom_evidence[pid] ≥ K`, the
process is **phantom-convicted**. K = 3 [DESIGN].

**[INVARIANT] VIII.4.2 — False-positive bound.**
If the per-phantom false-write probability is `p`, the K-threshold false-conviction
probability is `p^K`. Because the scored event is a content write to an invisible decoy — not a
mere touch — `p` is already vanishing; with phantoms generated at the density of VIII.2.2 and
K = 3, the false-conviction rate is bounded at `p^3 ≈ 10^(-9)` [DERIVED] — a structural
consequence of cumulative independent evidence, not a measured quantity.

**[DECISION] VIII.4.3 — Phantom conviction feeds the response system.**
A phantom conviction is an evidence event delivered to the response system (Part V)
as the third blocking trigger (V.1.2). Under AUDIT it is logged; under ENFORCE it blocks
the convicted process. The Oracle remains the definitive evidence channel; phantom
conviction is the behavioral fallback. It proves *indiscriminate file modification*, not
*encryption*; recovery depends on the Oracle's key or the preservation window.

**[INVARIANT] VIII.4.4a — Blocking a convicted originator is full destructive containment.**
Under ENFORCE, blocking a phantom-convicted (or otherwise blocked, V.1) originator denies
**every** path by which it could destroy an existing original, not writes alone: content
writes (IRP_MJ_WRITE), truncating opens (IRP_MJ_CREATE with a SUPERSEDE / OVERWRITE /
OVERWRITE_IF disposition), and destructive metadata (IRP_MJ_SET_INFORMATION — delete,
rename, set-EOF, set-allocation) are all refused with STATUS_ACCESS_DENIED. A block that
denied only writes would leave the truncate-on-open and the metadata-delete paths open, so
a convicted actor could still zero or remove a file; closing all three is what makes
containment complete. Decoy backing writes by an unconvicted process are exempt from the
protected-store guard and proceed normally so the deception remains convincing (VIII.3.1).

**[DECISION] VIII.4.4 — Evidence counter is process-scoped and monotonic.**
The counter follows the process, not the thread. It is monotonic: no event decreases it;
process exit zeroes it. Child processes do not inherit the parent's counter. A process
that spawns children to distribute touches achieves nothing: each child that touches K
phantoms is independently convicted; a child below K is not convicted.

### VIII.5 Trust model

**[DECISION] VIII.5.1 — Three trust states.**
Every process is in exactly one of three phantom-visibility states:

| State | Sees phantoms? | Touches counted? | Entry condition |
|:------|:-:|:-:|:---|
| TRUSTED | No | No | Whitelist (V.2) or system's own process |
| UNTRUSTED | Yes | Yes | Default for all non-whitelisted processes |
| TAINTED | Yes | Yes | Demotion from TRUSTED on unsigned module load |

TAINTED is triggered by `PsSetLoadImageNotifyRoutine`: when a TRUSTED process loads a
module whose `ImageSignatureLevel` is below SE_SIGNING_LEVEL_AUTHENTICODE, it is demoted
to TAINTED and begins seeing phantoms.

**[INVARIANT] VIII.5.2 — Demotion is irreversible within a process lifetime.**
Once TAINTED, a process remains TAINTED until exit. The demotion is recorded.

> **[NEGATIVE] VIII.5.3** Do not promote untrusted processes to trusted on the basis of
> behavior. Trust is conferred by the whitelist at creation time (V.2, VI.2.1), never
> earned.

### VIII.6 Metadata query fixup

**[INVARIANT] VIII.6.1 — Opened phantom handles report phantom identity, not backing-file identity.**
When an untrusted process holds a handle to a phantom backing file (obtained through
STATUS_REPARSE redirection) and queries its name or id, the minifilter's
IRP_MJ_QUERY_INFORMATION post-op rewrites the response to the phantom's declared identity:

- **FileNameInformation, FileNormalizedNameInformation:** the returned path is replaced
  with the phantom's directory and name.
- **FileInternalInformation:** the file id is replaced with the synthetic
  FileReferenceNumber assigned to this phantom (the same id that appears in the ID-bearing
  directory classes and in synthetic USN records).

Without this, a process could call `GetFinalPathNameByHandle` or `NtQueryInformationFile`
on an opened phantom and observe the hidden backing path, revealing the phantom's nature.
This is the metadata-query closure required by VIII.0.

**[DECISION] VIII.6.2 — Identity recovery via a keyed reparse map.**
The backing filename is a one-way HMAC of `(canonical_dir_path, index)` (VIII.7.2) and is
therefore not reversible to the phantom's directory or name. To rewrite the query response,
the pre-create that performs the reparse (VIII.7.1) — where the original phantom directory,
name, and index are all in hand — records the mapping `backing-filename → (directory, name,
index)` in a bounded, lock-protected in-memory table; the query-info post-op looks the
backing handle's leaf name up in that table. The table is a fixed-size ring (entries are
deterministic, so eviction merely forces a recompute on the next open); it holds no secret
and survives only in memory. This is the **only** state the phantom layer keeps beyond the
per-volume key, and it exists solely because the backing identifier is intentionally
irreversible.

> **[NEGATIVE] VIII.6.3** `FileAllInformation` (which embeds a name sub-field) and
> `FileHardLinkInformation` are not yet rewritten; a process querying those specific classes
> on an opened phantom can still observe the backing path. They are closed by extending the
> VIII.6.2 mechanism to those classes; until then they are a declared residual, bounded by
> the conviction asymmetry (a process that opens and inspects but does not destroy is
> unscored, and one that destroys is convicted regardless of what it learned).

### VIII.7 Backing-file management

**[DECISION] VIII.7.1 — STATUS_REPARSE redirection to hidden store.**
When an untrusted process opens a phantom name, the minifilter's pre-create callback:
1. Calls `IoReplaceFileObjectName` to replace the target with the hidden backing file path.
2. Sets `Data->IoStatus.Status = STATUS_REPARSE`, `Information = IO_REPARSE`.
3. Calls `FltSetCallbackDataDirty`, returns `FLT_PREOP_COMPLETE`.

The I/O manager re-issues the create against the backing file, which is a real file in a
hidden directory protected by the minifilter's anti-deletion mechanism (VII.1.4). The
calling process receives a valid handle and observes normal file semantics.

**[DECISION] VIII.7.2 — Backing-file store layout.**
Backing files are stored under the minifilter's protected directory alongside the
keystore and preservation store. Each backing file is named by the HMAC of its phantom
identity `(canonical_dir_path, index)` and is materialized on first access (VIII.7.3). The
store's disk footprint is bounded by density rules (VIII.2.2) and the per-file size cap
(VIII.2.5).

**[DECISION] VIII.7.3 — Backing materialization on first access.**
The reparse (VIII.7.1) requires the backing file to exist with consistent content and
metadata, so that an `FILE_OPEN` of a phantom — the ordinary "open existing to read/encrypt"
path that ransomware uses — succeeds rather than failing with OBJECT_NAME_NOT_FOUND (a tell,
and a conviction miss). At the recognizing pre-create, before the reparse, the minifilter
materializes the backing file if absent: it writes the type's magic/structural header, fills
to the keyed reported size (VIII.2.5) with low-entropy synthetic body bytes, and stamps the
keyed timestamps. Materialization is a kernel-mode write to the backing; the write path
treats kernel-mode phantom-backing writes as non-evidence (VIII.4) so the minifilter's own
materialization can never accrue conviction against the opening process. The work is
one-time per phantom (subsequent opens find the file) and bounded by the per-file size cap.

> **[NEGATIVE] VIII.7.3a** Backing content is synthesized from the per-volume key only; it
> must never be copied from a real user file (a verbatim copy both creates a duplicate-content
> tell and stages real data in the hidden store).

> **[NEGATIVE] VIII.7.3b — Residual: structural image validity.** A minority of selective
> encryptors (the documented case is Cerber) *decode* candidate images and skip a directory
> whose image is malformed, treating it as a canary. For image-extension phantoms the body
> currently carries the type's magic header but is not a fully decodable image, so such an
> encryptor skips the directory — the user's real files there are incidentally protected, but
> the phantom is not written and no conviction fires. Closing this requires materializing a
> structurally valid minimal image per image type (a data-table template, attached to the
> low-entropy body), which must be added with image-decode verification so an invalid blob is
> never shipped. Until then it is a declared residual: data is protected by directory-skip,
> and any encryption that proceeds elsewhere is caught by the core gate/Oracle/preservation
> (Part III–IV), which are independent of the phantom layer. A fully content-parsing encryptor
> (validating ZIP central directories, decoding every format) is not observed in the
> ransomware population and is a forensic-adversary behavior outside this model.

**[DECISION] VIII.7.4 — IoReplaceFileObjectName availability.**
`IoReplaceFileObjectName` is available since Windows 7. On the supported floor it is
always present. The routine is resolved via `MmGetSystemRoutineAddress` at initialization;
if absent the phantom layer is disabled entirely and the descent is recorded (VII.5).

> **[NEGATIVE] VIII.7.5** Do not implement a manual name-replacement fallback for systems
> where `IoReplaceFileObjectName` is absent. The supported floor guarantees its presence.

### VIII.8 USN journal closure

**[DECISION] VIII.8.1 — Synthetic USN records.**
For each phantom the minifilter maintains a synthetic USN_RECORD_V2 containing:
- A synthetic `FileReferenceNumber` from a reserved range (`0xFFFF_xxxx_xxxx_xxxx`) that
  does not collide with real MFT entries (real references use the low 48 bits; the
  reserved range sets the high 16 bits as a discriminator).
- The phantom's declared file name, parent directory reference, timestamps, and a
  plausible Reason field.

The synthetic records are injected into the FSCTL_ENUM_USN_DATA output in post-op. Once the
operation is safe (FltDoCompletionProcessingWhenSafe), the locked output buffer is extended with
synthetic USN_RECORD_V2 entries for the directories resolved from the real records' parent
references, so the same phantoms a directory enumeration of those directories would surface also
appear in the volume-wide MFT walk. They are invisible to trusted processes (VIII.1.3). An untrusted
process that acts on an injected phantom — opening it by name (the parent reference resolves to a real
directory whose phantom set the open matches, VIII.2) or by its synthetic `FileReferenceNumber`
(`FILE_OPEN_BY_FILE_ID`, reparsed to the backing) — reaches the backing file and is convicted on the
encryption-chain write (VIII.4), exactly as through directory enumeration. A phantom's reference, name,
and backing are identical across the directory, query, and USN surfaces, so cross-referencing one
against another cannot distinguish a phantom from a real file.

> **[NEGATIVE] VIII.8.1a — Residual: the change journal and the 128-bit record format are not
> surfaced.** Two USN surfaces remain outside the injection. `FSCTL_READ_USN_JOURNAL` returns a
> *temporal* stream of change records, not a file listing; a process that discovers targets solely by
> reading the change journal is not shown phantom change-events there. And the injection synthesizes
> the V2 record; an enumerator that requests the 128-bit V3 format (`MFT_ENUM_DATA_V1`, the
> `FileIdAllExtd` class of VIII.8) is not surfaced phantoms in that call. Both are bounded by the
> conviction asymmetry and, decisively, by the **core engine**: a USN-driven encryptor that avoids
> every phantom still destroys real originals, which the gate / Oracle / preservation (Parts II–IV)
> detect, preserve, and (under ENFORCE at capacity) block independently of the phantom layer. The
> phantom layer is one evidence channel, not the sole defense.

**[INVARIANT] VIII.8.2 — No integrity mechanism protects USN records.**
USN records carry no signature, checksum, or integrity seal. Injection of syntactically
correct records is indistinguishable from genuine records to any consumer that does not
independently verify against the raw MFT — and raw MFT access is blocked for untrusted
processes (VIII.3.3).

> **[NEGATIVE] VIII.8.3** Do not inject records into the on-disk USN journal
> ($UsnJrnl:$J). The minifilter intercepts the query path (FSCTL responses), not the
> storage path. On-disk injection requires NTFS-internal writes below the minifilter stack
> and would violate the kernel-code boundary (Part IX).

### VIII.9 MFT defense

**[DECISION] VIII.9.1 — The MFT gap is closed by blocking, not by faking.**
Phantoms have no MFT entries. The minifilter blocks the two paths by which an untrusted
process could discover the absence:
1. **FSCTL_GET_NTFS_FILE_RECORD** is denied for untrusted processes (VIII.3.3).
2. **DASD opens** are denied for untrusted processes (VIII.3.3).

No shipping ransomware uses MFT parsing for file enumeration; only the DriveSlayer wiper
has been observed using FSCTL_GET_NTFS_FILE_RECORD, and wipers are out of scope
(IX.3). The block closes a theoretical vector.

> **[NEGATIVE] VIII.9.2** Do not inject fake MFT entries. MFT manipulation requires
> filesystem-internal writes architecturally below the minifilter. The blocking strategy
> is complete: an untrusted process that cannot read the MFT cannot discover the absence.

### VIII.10 Integration with the pipeline

**[DECISION] VIII.10.1 — Position in the defense pipeline.**
The phantom layer operates **in parallel with** the gate–Oracle–preservation chain, not
in series. A process that enumerates directories encounters phantoms regardless of whether
its writes pass the gate. The two evidence channels converge at the response system
(Part V).

**[INVARIANT] VIII.10.2 — The phantom layer does not alter the gate, the Oracle, or preservation.**
Phantom files are not real files. They do not pass through the gate (their backing-file
writes are internal). The Oracle does not scan phantom writes. Preservation does not hold
phantom originals. The phantom layer is an evidence source, not a participant in the
capture–recovery pipeline.

> **Rationale.** If the Oracle scanned phantom writes it would find ciphertext produced by
> ransomware encrypting the phantom's content — and the key capture would succeed. But the
> "original" is fabricated, and "recovering" it serves no purpose; it would inflate the
> keystore with entries that recover nothing the user owns. The phantom layer feeds
> *conviction evidence*, not *recovery data*.

---

## PART IX — BOUNDARIES (declared non-defenses)

Two lines bound this system. The default state of any newly found gap is closed unless
it escapes both (0.3).

### IX.1 [BOUNDARY] Kernel-code execution is game over
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

### IX.2 [BOUNDARY] The confirmed limit — beyond both assets
Where the Oracle does not capture the key **and** preservation does not hold the original
within its window, the data is not protected. An Oracle miss **alone** is not the limit: the
same destructive write the Oracle could not key-capture had its original held by preservation
(III.5), so it is *reversibly protected within the window*. The limit is reached only when both
assets fail — the key was never captured **and** the held original has aged or been pushed out
of the bounded store, could not be copied at all, or the on-disk store was destroyed by a
SYSTEM actor (VII.1.4). The following are the **Oracle-miss** cases; each falls to preservation
within the window and reduces to this limit only beyond it. They are **not** new open problems:

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
- **Key zeroed before the deferred snapshot**: the memory capture is a prompt off-IRP snapshot
  deferred from each capture-eligible write (II.3.2), never an in-IRP scan. A key reused or held
  across multiple writes is caught at any write whose deferred snapshot finds it still resident —
  and under ENFORCE one such capture is enough to convict and block, bounding the damage. What
  still reduces here: a key computed, used, and zeroized before the deferred worker can attach;
  and, distinctly, a *per-file* key under immediate zeroization — because conviction is the
  forward proof and that key's only matching (`P`,`C`) is its own write, at which the key is
  already gone (a sibling file's (`P`,`C`) is a different key). A key *reused* across files
  escapes this, since any sibling's (`P`,`C`) convicts it; the immediate per-file zeroize of a
  tiny file does not. The synchronous register grab provides a secondary channel; whatever still
  falls outside both is held by preservation (III.5) for the window, and reduces here only
  beyond it.
- **The held original could not be copied, or its store was destroyed**: a destruction whose
  original was never on disk to read — a region first observed already as ciphertext, with no
  pre-attack original to hold (III.5.3) — one whose capture the pipeline shed under saturation
  (III.5.5), one whose held copy aged or was evicted past the window (III.5.5), or a held copy whose
  on-disk store a SYSTEM actor destroyed (VII.1.4). A captured key still recovers regardless; only the *held-only* residue reduces here,
  and it is detected and reported, never silently dropped.

> **Rationale.** This is the honest edge of a system that holds keys and a *bounded* window of
> originals (Part I). The Oracle is the definitive protection and preservation the bounded
> reversible one; where the key was never caught **and** the window has passed, there is no
> protection, and engineering "around" *that* would re-introduce the unbounded retain-everything
> the system refuses. The edge is stated, not hidden: recovery is offered only where a key or a
> held original can prove it.

### IX.3 [BOUNDARY] Wiper / no-key destruction
Plaintext-over-plaintext destruction, pure deletion, and zeroing produce no recoverable
key and are not targets. A wiper is not viable ransomware (no key, no ransom). Disguised
encryption still fails the conditional test toward the high end and is captured like any
encryption; a genuine no-key wipe is out of scope.

### IX.4 [BOUNDARY] Out-of-volume / geometry corruption
Partition-table or out-of-volume corruption makes a volume unaddressable but does not
destroy file *data* (recoverable by partition repair). Defending partition geometry is a
different product.

> **[NEGATIVE] IX.5** Do not add any defense past these lines, and do not escalate
> threat vectors above them. A scenario that requires kernel access, or that is simply
> "the Oracle did not capture the key," is closed here — not an invitation to invent a
> new mechanism.

---

## PART X — COMPATIBILITY AND DEPLOYMENT

### X.1 [DECISION] Single binary, runtime feature detection
One source tree, one binary per CPU architecture, targeting every minifilter-capable
Windows release. Newer DDIs / structures / info-classes are visible at compile time and
gated at runtime; any routine that would otherwise fail to resolve on a down-level
kernel is resolved at runtime with a down-level alternative.

> **[NEGATIVE] X.1.1** No dead code and no compatibility fallback for its own sake. A
> down-level branch exists only where the up-level path would fail to load or function on
> a supported OS. A superset of operation callbacks, FSCTL codes, and info-classes is
> correct: a code an OS never issues never reaches the handler and is inert there.

### X.2 [DEPLOY] Load, signing, and primitive preconditions
- The driver is linked with `/integritycheck`; the boot-start signature is embedded in
  the `.sys`. Failure of the process-notify registration (which the
  identity-at-creation logic of V.2.2 depends on) is fatal-to-feature, not swallowed.
- Self-protection primitives (HVCI, TPM 2.0 + kernel TBS, PPL, Dev Drive policy) are
  runtime-detected; where present they are verified/configured, where absent the
  dependent boundary descends (VII.5) and is recorded.
- The protected service runs at the anti-malware PPL level via a signed ELAM driver that
  registers the service-binary signer (VII.3.1); where AM-PPL eligibility is absent the service
  runs unprotected and the descent is recorded (VII.5). The recovery assets do not depend on it.
- The preservation store is encrypted at rest with the in-kernel symmetric primitives (VII.1.3),
  present on every supported OS, and opened unbuffered + write-through for crash consistency. Its
  retention-time and capacity bounds are the resource envelope of V.1.3; the throughput of
  write-through encrypted holding at the chosen capacity is a deployment characteristic measured
  on the target, not a correctness property.
- The supported floor is a recorded deployment decision. The recovery mechanism is
  deliverable down to the FltMgr / CNG / process-notify-Ex floor; the self-protection
  model is intact only from the OS generation that supplies its primitives. The
  recommended production floor is the lowest point where the self-protection model holds
  and a single attestation/WHCP-signed binary suffices.

---

## PART XI — CONFORMANCE CHECKLIST

An implementation is constitutional iff all hold.

**The heart (Parts I, II, III)**
- [ ] Response strength is graduated to evidence certainty: a captured key (definitive) →
      unbounded recovery + block at first instance; an uncaptured destruction (circumstantial)
      → bounded reversible preservation + block only at capacity; nothing responds beyond its
      evidence (I.1).
- [ ] Conviction is forward-only (`Encrypt_K(destroyed original) == written`); the
      reverse never convicts; the system cannot convict its own recovery (II.2).
- [ ] The Oracle anchors capture to the writer but reads its memory off the IRP: a prompt
      deferred worker attaches to the still-live writer (under exit synchronization) and copies
      bounded resident committed-private regions (heaps first, then the rest) into a frozen
      snapshot; the structural scan, cipher battery, and forward proof run on each frozen
      snapshot; nothing enumerates or scans the writer's memory from inside the write IRP (II.3).
- [ ] Structural key-schedule location and round-key inversion are implemented; conviction
      stays the forward proof, never key-in-hand; cumulative-N is per-key capture-opportunity
      over writes + keystore accumulation, never backup accumulation (II.4).
- [ ] The keystore holds one record per encrypted region — (key, file, {offset,length})
      with that region's parameters and verification tag; a key's regions (across reused
      files or scattered within one file by partial encryption) each keep their own
      anchor, so the whole set recovers and no region is silently denied (II.5.1).

**Recovery and preservation (Part III)**
- [ ] Recovery has two assets: decryption with the captured key (definitive, unbounded), and
      restoration of a bounded copy-on-first-write preserved original (circumstantial); a
      captured key cancels the held original for its region (III.1, III.5.4).
- [ ] Preservation copies the original off a deadlock-free path (in-place: the write region read
      non-cached/paging at the write seam before the cached write lands; destruction seams and
      replace/mapped: a distinct file object), one copy per (file, region), first-write-wins, never
      overwriting a held original with what destroys it; it triggers on every IV.1.2 destruction
      member and never blocks (III.5.1–III.5.3).
- [ ] The window is bounded by a time + capacity resource envelope; reclamation is by age;
      restore is operator-directed, verified, and metadata-preserving, and the store never
      leaves the kernel (III.5.5, III.5.6).
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
- [ ] T is diff-restricted conditional novelty = 1 − coverage over the changed bytes only,
      ratio floor `θ = 0.10` and volume floor `μ = 12` novel 2-grams; it is direction-blind
      and is not a classifier; absolute-entropy / entropy-delta / ML-as-gate / always-pass are
      absent (IV.2).
- [ ] The gate never blocks (IV.3).
- [ ] `gzip`-style compression is handled as an ordinary delete-plus-new-file candidate
      with no special case (IV.1).

**Response and authority (Part V)**
- [ ] Capture is always on; mode (AUDIT / ENFORCE) governs blocking only; the mode
      transition is the user's, never inferred (V.1).
- [ ] Under ENFORCE a block originates from exactly two triggers: a forward conviction at first
      instance (definitive), or preservation capacity exhaustion (circumstantial, key-less, the
      cautious extreme); the blocked set is behavioral; no other quantity blocks (V.1.2).
- [ ] The preservation window's time + capacity bounds are a user-ownable resource envelope,
      not a detection knob (V.1.3, 0.5).
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
- [ ] The preservation store is encrypted at rest in kernel under a pool-resident sealed key;
      its plaintext never leaves the kernel; held entries are integrity-tagged and bound to
      (file, region) (VII.1.3).
- [ ] Both on-disk assets deny destructive IRPs on their own paths and are chained-MAC'd with a
      checkpoint-committed freshness anchor that halts on rollback; the SYSTEM volume-dismount
      availability gap is detected and recorded, not falsely claimed closed (VII.1.4).
- [ ] The comm-port authenticates the client; the first connector is not auto-trusted
      (VII.3).
- [ ] Each primitive is runtime-detected; absence descends the boundary explicitly
      (VII.5).

**Phantom Witness (Part VIII)**
- [ ] Physical absence: phantoms have no MFT entry, no disk clusters, no USN history prior
      to minifilter intervention; they exist only in IRP responses (VIII.1.1).
- [ ] Indistinguishability: phantom names, sizes, timestamps, content, reported file
      identity, and security descriptors match the containing directory's distribution;
      no observable attribute distinguishes a phantom from a real file (VIII.1.2).
- [ ] Transparency: trusted processes never see phantoms in any enumeration, create,
      metadata query, or USN read (VIII.1.3).
- [ ] Naming is a keyed realistic generator over HMAC-SHA256(volume_secret,
      canonical_dir_path || index): pronounceable stem + palette extension, deterministic,
      unpredictable, fingerprint-free, recomputed (not cached) for recognition (VIII.2.1).
- [ ] Per-phantom size and timestamps are keyed, varied, plausibly aged, and consistent
      between enumeration and the materialized backing; backing is materialized on first
      access with a valid header + low-entropy body sized to the reported size (VIII.2.4,
      VIII.2.5, VIII.7.3).
- [ ] Enumeration injects exactly once per scan (per-handle state), in sorted order, only
      for names matching the query pattern, across every directory-information class the
      object store answers — no duplicate, mis-ordered, or cross-class-diff tell (VIII.3.1).
- [ ] Conviction is cumulative K-threshold (K ≥ 3) over content writes to phantom backing
      files; reconnaissance (listing/opening/reading a phantom) is not scored; a single
      write does not convict; phantom conviction feeds the response system as the third blocking
      trigger (VIII.4, V.1.2).
- [ ] The phantom layer does not alter the gate, the Oracle, or preservation
      (VIII.10.2).
- [ ] Every user-mode metadata query on an opened phantom (file name, file ID, hard
      links) reports phantom identity, not backing-file identity (VIII.6.1).
- [ ] DASD opens and FSCTL_GET_NTFS_FILE_RECORD are denied for untrusted processes
      (VIII.3.3); no fake MFT entries are injected (VIII.9).
- [ ] Kernel-privilege closure: without kernel-code execution, no user-mode path
      distinguishes a phantom from a real file (VIII.0).

**Boundaries (Part IX)**
- [ ] No defense is added past the kernel line or the confirmed limit; threats are not
      escalated above them (IX.5).
- [ ] The confirmed limit is "beyond both assets": an Oracle miss is reversibly protected by
      preservation within the window and reduces to the limit only beyond it; held-only data
      lost to a SYSTEM store-destruction is detected and reported, never silently dropped (IX.2).

**Compatibility (Part X)**
- [ ] One binary per architecture; down-level branches only where a routine would
      otherwise fail; no fallback for its own sake (X.1).

**Process (Part 0)**
- [ ] Every quantitative claim is tagged MEASURED / DERIVED / DESIGN and none is deferred
      to runtime configuration (0.2, 0.5).
- [ ] No CLOSED conclusion is reopened on a basis already inside the boundaries of
      Part IX (0.3).

---

### Closing note (non-normative)

This document has one load-bearing sentence (Part I) and nothing that contradicts it: the
strength of the response is proportional to the certainty of the evidence. The gate asks one
question; the Oracle answers it by capturing a key, and where it does the key recovers every
file under it without bound; where the key is too fleeting to catch, the original the attacker
had to read is held aside in a bounded window so the destruction can be undone; where the
attacker walks files indiscriminately, virtual witnesses that it cannot distinguish from real
files detect the pattern and the system blocks the walker; the user decides when proven,
capacity-exhausting, or behaviorally convicted destruction is an attack; and where neither the
key nor a held original remains, the system says so instead of pretending otherwise. The two
recovery assets are ranked, not redundant — the definitive key capture and the bounded
reversible hold — and the behavioral witness is orthogonal to both, providing evidence for
conviction but never for recovery. Nothing in the system responds more strongly than the
evidence it has.