# semantics-ar — Constitution

**STATUS: RATIFIED.** This document is the authoritative specification for the semantics-ar defensive data-preservation system (a Windows filesystem minifilter plus a user-mode service). It governs implementation. **Where code and this document disagree, this document is right and the code is wrong.**

This is a complete document written from a single governing proposition (Part I). It is not a patch over an earlier design. Every clause below is either a derivation of that proposition or an explicit statement of where the proposition stops. There is no clause that exists for any other reason.

---

## PART 0 — HOW TO READ THIS DOCUMENT (binding)

### 0.1 This document is a closure boundary

You (the implementer, human or model) will re-research each area you build, and you will reconstruct your own understanding. That is expected. This document does not exist to re-teach you. It exists to fix the boundary of what your re-research is allowed to **reopen**.

Re-research answers **how** to implement a settled conclusion. It must not be used to reopen **whether** the conclusion holds. A clause tagged below is settled. If your implementation cannot satisfy it, the implementation is wrong; the clause is not up for renegotiation from inside a coding task.

### 0.2 Item types

Every normative item carries exactly one tag.

- **[INVARIANT]** — A structural property that must always hold. If your implementation can violate it under any input, timing, failure, or supported OS an adversary can reach, the implementation is wrong.
- **[DECISION]** — A chosen method. Implement it as written. You may vary only within its stated latitude.
- **[BOUNDARY]** — A thing we deliberately do **not** defend against. You must not add defenses past this line.
- **[NEGATIVE]** — A thing you must not do.
- **[DEPLOY]** — An environment precondition whose closure is verification or configuration, not research.

Where a quantitative claim appears, it is tagged by epistemic status: **[MEASURED]** (empirical, from a run), **[DERIVED]** (mathematical), **[DESIGN]** (a recorded choice, not forced by fact). Keeping these distinct is mandatory; collapsing them is how false certainty enters a specification.

### 0.3 The default state of a newly found gap is CLOSED

While implementing, you will discover new-looking surfaces. Before treating any as open work, classify it. It is **CLOSED** — no boundary, no open item, no mechanism, no clause — unless it escapes all of the following:

1. It requires kernel-code execution or a privilege at or above the line this system defends → CLOSED by Part IX.
2. It is a case where the Oracle simply does not observe a key → CLOSED as a confirmed limit (Part IX).
3. It is already governed by an existing **[INVARIANT]** or **[DECISION]** → CLOSED.

If and only if it escapes all three is it a genuine open item.

> **[NEGATIVE] 0.3.1 — Do not manufacture a security boundary out of a condition an adversary cannot reliably weaponize.** Scope is decided by the **response** to a condition and the **capability** required to induce it — never by whether the condition "sounds environmental."
>
> A condition is **IN scope and must be handled fail-closed** (refuse the protected operation rather than lose data or admit an unprotected write) if BOTH hold: **(a) Reachability** — it can be induced using only capabilities the threat model already grants: in-band, software-only, at or below the privilege this system claims to withstand, with no appeal to physical access, hardware fault, or higher privilege; AND **(b) Consequence** — inducing it would otherwise cause a loss or an unprotected write. Reachability explicitly includes **amplification and retry**: the adversary need not aim one rare event. Raising a failure's probability across a window they control (pool/memory pressure, resource priming) or simply re-running the same protected operation until the failure lands both count as reliable weaponization. **Every bounded resource this system maintains or consumes on the protection path** — the preservation store, region/context tables, work queues, pool — is adversary-pressurable by construction and is in scope; "resource shortage" is never automatically out of scope.
>
> A condition is **OUT of scope** — delete the clause, add no mechanism — only if it fails (a): it cannot be induced without capabilities that already moot the protection (pulling power, physically damaging or controlling the medium, or a compromise above the defended privilege line). A condition that fails (b) — its existing response already refuses and preserves — also gets no clause; there is nothing left to defend, and adding a bespoke mechanism is forbidden gold-plating. **Fail-closed handling of a resource failure on the protection path is not such a mechanism — it is the uniform default response, and its absence is a defect, not a "defense against the environment."**
>
> **Mechanical self-check (run it literally before recording any limit):** *Using only in-band software access at or below the privilege I claim to withstand, can I write an automated test — pressure and retry allowed — that induces this condition and catches this system losing data or admitting an unprotected write?* **YES →** in scope; make the response fail-closed; write no boundary language. **NO, because reproducing it needs physical access / hardware fault / higher privilege →** out of scope; delete it. **NO, because the response already refuses and preserves →** no clause; adding a mechanism is gold-plating.

### 0.4 One idea

The entire system is one sentence, stated in Part I and unfolded through Parts II–VIII. Read Part I first. Every mechanism that follows is the minimal thing that serves it. If a mechanism cannot be traced back to Part I, it does not belong.

### 0.5 No runtime-tunable detection; one resource envelope the user may own

Every value that governs **detection or conviction** — the gate threshold, the cipher battery, the phantom count, the capture trigger — is a recorded design decision derived here, not a knob exposed to runtime configuration. Exposing detection as a setting is forbidden: it turns a proof into a preference.

One thing, and only one, the user may set: the **resource envelope** of the preservation window — its retention time and its capacity in bytes. This is not detection logic; changing it never changes what the system proves, convicts, or blocks. It is a backup-retention policy, a recorded **[DESIGN]** default with deliberate user authority over it (Part III, Part V).

---

## PART I — THE GOVERNING PROPOSITION

Everything in this document is this sentence, unfolded:

> **The system observes every destruction of an existing original that a process read, answers it with recovery graduated to the certainty of its evidence, and — because normal operation is the overwhelming case — imposes nothing on anyone it has not convicted.**

Three commitments follow, and they are co-equal. They are ordered only for the rare moment they conflict.

- **FN = 0 (recoverability).** No file whose original a process read is left unrecoverable against a recoverable-destruction attacker. Either the destructive key is captured (the Oracle) or the exact destroyed region is preserved before the write commits. Under ENFORCE this is absolute: when neither can be guaranteed for a destructive operation, the operation is **refused** (fail-closed) — refused rather than served by evicting a not-yet-superseded original.
- **FP ≈ 0 (invisibility).** In normal operation the system is not felt. It blocks only on definitive evidence (Oracle forward proof or phantom conviction) or on the ENFORCE resource bound, and the resource bound refuses only the individual operation, never the process. An exempt identity is monitored **zero**. This is a first-class commitment, not a courtesy: the system exists to protect data without degrading the ordinary use of the machine, and ~99.9% of what it sees is ordinary use.
- **Cost → the theoretical minimum**, subject to the two above.

> **[INVARIANT] I.1 — Response is graduated to evidence, never beyond it.** The strength of every response is a function of the certainty of the evidence for it. Recovery is offered wherever an original can be reconstructed. A destructive operation is *blocked* only where the evidence is definitive (a forward cryptographic proof, or accumulated phantom conviction) or where a bounded resource is exhausted; the latter refuses one operation and never terminates or blanket-blocks a process. The system never terminates a process.

> **[INVARIANT] I.2 — Priority applies only on conflict.** FN=0, FP≈0, and minimal cost are pursued together. Only when a design choice genuinely trades one against another does order apply: a change that trades FN=0 for cost is wrong; a change that trades FP≈0 for cost is the owner's to ratify, not the implementer's to assume.

The three witnesses that make I.1 possible, each with a different epistemic weight:

- The **Oracle** (Part II) — a *forward cryptographic proof* that recovers the key. Definitive; unbounded recovery.
- **Preservation** (Part III) — a *copy of the original bytes* taken before they are destroyed. Circumstantial; storage-bounded recovery; it never convicts.
- The **Phantom Witness** (Part VIII) — *decoys* that only an indiscriminate file-walker touches. Behavioral evidence; it never recovers.

A passive **Gate** (Part IV) decides only what is worth capturing. It never blocks.

---

## PART II — THE ORACLE (the heart)

The Oracle is the definitive witness. When a process destroys an original by transforming it, the transform used a key, and that key is in the writer's own memory at the moment of the write. If the system can hold that key and prove it, the original is recoverable without bound, forever, from ciphertext alone.

> **[INVARIANT] II.1 — Conviction is a forward proof against the destroyed original.** The system convicts only when it can show `Transform_K(the original that was read) == what was written`, for a key `K` recovered from the writing process. The pre-image is the bytes the process read (Part IV establishes read-precedence); the post-image is the bytes it wrote. Recovering a key is not conviction; a forward match is.

> **[NEGATIVE] II.1.1** Do not convict on the reverse direction, on the mere presence of key-shaped bytes in memory, or on any property of the writer that is not a forward match against the specific destroyed original. Do not let the system convict its own recovery activity.

### II.2 Where the key is found

**[DECISION] II.2.1 — Anchored to the writer, snapshotted off the write path.** When the gate marks a write a candidate, the system snapshots the writing process's private, committed, readable memory (its heaps first, then other committed private regions) once per process, at PASSIVE_LEVEL, off the I/O path. The forward battery runs over that snapshot. Anchoring to the writer — not to a global scan — is what makes the search small and the proof specific.

**[DECISION] II.2.2 — The battery is structural first, then bounded brute force.** Over the snapshot the system runs, in order: a scan for a valid expanded key schedule (block ciphers), a scan for the stream-cipher block constant `"expand 32-byte k"` that structurally locates a ChaCha/Salsa key and nonce, a scan for an initialized RC4 permutation, and a bounded windowed brute force. Recovered candidates are proven by forward match under an inferred mode.

**[DECISION] II.2.3 — The cipher and mode coverage is fixed [DESIGN].** Block ciphers: AES-128/192/256, 3DES, SM4, Camellia, ARIA, SEED. Stream ciphers: ChaCha20, XChaCha20, Salsa20, XSalsa20, RC4. Modes inferred by forward match: ECB, CBC, CFB, OFB, CTR (over a fixed table of counter layouts), and XTS. This set is a recorded decision, not a runtime option, and is not tunable (0.5).

### II.3 Recovery is verified before it replaces

> **[INVARIANT] II.3.1 — The target is never overwritten unless the recovery is proven correct.** At capture time the system records a tag (`SHA-256`) of a sample of the true original. Recovery decrypts with the recovered key, recomputes the tag over the recovered sample, and compares it in constant time. Only on a match does it write. On mismatch it declines and leaves the target byte-for-byte intact.

**[DECISION] II.3.2 — The sample authenticates the key; the key recovers the file.** Verification proves the *key* against a known original sample; the whole affected range is then reconstructed with that proven key. Recovery writes plaintext to a sibling temporary file, flushes it, and replaces the original atomically. It is never an in-place edit of the target.

### II.4 The keystore

**[DECISION] II.4.1 — Append-only, MAC-chained, rollback-anchored.** Captured keys are held in an append-only ledger (capacity `16384` records [DESIGN]). Each record is HMAC-chained to its predecessor; the chain head is anchored in a separately persisted blob. On load, a broken chain, a wrong magic, or a generation/head that regresses against the anchor is treated as tampering: the records are discarded and a tamper flag is raised (Part VII). An emptied-after-committed store is tampering, not a benign empty.

> **[NEGATIVE] II.4.2** When the ledger is full, do not drop a not-yet-superseded preserved original to record a key. A full ledger means the key is not stored; the preserved floor for that region must remain (Part III).

---

## PART III — RECOVERY AND PRESERVATION (two assets)

Preservation is the bounded floor beneath the Oracle. Where the Oracle has not (yet) proven a key, the original survives because a copy of the exact destroyed bytes was taken **before** the destroying write committed. Preservation is circumstantial: it recovers, it never convicts.

### III.1 What is captured, and when

> **[INVARIANT] III.1.1 — The pre-image is the true original, taken before commit.** For a top-level destructive write the pre-op callback runs at PASSIVE_LEVEL, holding no cache or memory-manager locks, before the write reaches the file system. The system reads the destination range's current on-disk bytes there — those bytes are still the original — and preserves them. The pre-image is never the post-write bytes.

**[DECISION] III.1.2 — Region-only, never whole-file.** The system preserves the specific byte range a destructive operation targets, not the whole file. Whole-file capture at open was shown to lock files under ordinary rewrite and is forbidden (III.5.1). Every dirty user-data byte originates at one of three seams — a top-level write, a cached/mapped-section write, or a metadata destruction (truncate, delete, rename-over) — and each seam preserves its own range.

> **[INVARIANT] III.1.3 — First-write-wins.** The first preserved copy of a region is the authoritative original. A later destructive write to an overlapping range never replaces it; the earlier copy stands.

> **[INVARIANT] III.1.4 — Staging is containment-aware: the uncovered remainder is always preserved.** When a destructive write spans a region that is only *partially* already preserved, the system preserves exactly the sub-ranges not yet held, reading each uncovered sub-range's current on-disk bytes, and leaves the already-held sub-ranges untouched. A partial overlap never causes the unheld remainder to be skipped. The persisted store — not any per-handle bookkeeping — is the single source of truth for what is already held, so this holds even across a close and reopen of the file. *(The gap computation is a pure function, unit-verified.)*

### III.2 What is destroyed without a key still survives

**[DECISION] III.2.1 — Metadata destruction is preserved by content.** Truncation (shrinking end-of-file or allocation), deletion, and rename-over-an-existing-target each destroy an original with no recoverable key. Before such an operation commits, the system preserves the affected content — the truncated tail, the whole file to be deleted, the destination to be overwritten — by a cheap intra-volume hard link where the file system allows it, falling back to a synchronous content copy where it does not.

### III.3 Two pools: probation and protected

**[DECISION] III.3.1 — Preserved originals begin in probation and are promoted on conviction.** A newly preserved region is in *probation*. When the Oracle or the phantom layer convicts the actor that produced it, all of that actor's probation holds are promoted to *protected*. Promotion records that these originals belong to a proven destructive actor.

**[DECISION] III.3.2 — A captured key reconciles the holds it now recovers.** When a key is captured and forward-proven for a range, the preserved originals fully contained in that range are discarded — the definitive asset supersedes the bounded one — regardless of pool. Reconciliation is by containment: a key covering only part of a held region does not discard it.

### III.4 The resource envelope and fail-closed capacity

> **[INVARIANT] III.4.1 — Under ENFORCE, capacity is fail-closed: block before evicting a live original.** When preserving a region would exceed the capacity envelope, ENFORCE **refuses the destructive operation** (`STATUS_INSUFFICIENT_RESOURCES`, completed without passing the operation down) rather than making room by discarding a not-yet-superseded original. A protected (convicted-actor) original is never evicted to make room. This is the direct consequence of 0.3.1: store exhaustion is adversary-pressurable, so its response is fail-closed. Likewise a resource failure on the preservation path (an allocation that cannot be met while staging an original) is fail-closed — the operation is refused, not admitted uncaptured.

**[DECISION] III.4.2 — Retention is the user's resource policy and applies to every hold.** The capacity and retention bounds are the one user-owned envelope (0.5). Retention expiry reclaims *any* hold whose age exceeds the window — probation and protected alike — because it is a backup-retention policy, not an eviction-to-make-room. This is distinct from III.4.1: expiry is the user's declared time budget; making-room-by-discard is forbidden.

**[DECISION] III.4.3 — AUDIT does not block and does not promise recovery.** In AUDIT the system never refuses an operation; under capacity pressure it may slide the oldest *probation* hold. AUDIT is a measurement posture and makes no FN=0 promise. FN=0 is an ENFORCE property.

### III.5 Mapped-section (memory-mapped) writes

A process may destroy a file by mapping it writable and storing into the view; the file system writes those dirty pages back on its own schedule, as paging I/O the filter cannot refuse. This seam is captured region-only, on the same wiring as every other seam. The design fact that shapes it: an OS background page write-back cannot be denied without corrupting the mapping, so the fail-closed lever is moved *earlier*, to the moment the writable mapping is created.

> **[INVARIANT] III.5.1 — Whole-file capture at section-arm is forbidden; the fail-closed lever is the reservation.** When a process acquires a writable section over an existing file, the system reserves capacity for the file's worth of pre-images and, under ENFORCE, **refuses the section acquisition** if that reservation cannot be met. This is the fail-closed point for the mapped seam: an adversary who would exhaust the store to force mapped loss is refused the mapping. Reserving is not copying: the whole-file eager copy that an earlier design used is forbidden — it locks files under ordinary rewrite.

**[DECISION] III.5.2 — Capture at the paging write, region-only, against the reservation.** The section-arm builds the file's on-disk extent map once. At each paging write-back the system raw-reads the affected region's on-disk pre-image (still the original, pre-commit) at PASSIVE_LEVEL and preserves it, releasing reservation as it stages. The arm is gated to genuine user-mode writable mappings (both the requestor mode and the previous mode are user) so that cache-manager and other kernel data sections do not trip it. Paging write-backs are observed at PASSIVE_LEVEL universally (256/256, under memory pressure) [MEASURED].

**[DECISION] III.5.3 — New writes are told from overwrites by extent type, not by read-precedence.** A mapped writer cannot be observed reading, so the mapped seam distinguishes destruction from new data by the file's own layout: a real allocated extent under the written offset is an overwrite (preserve it); a sparse hole, or an offset beyond the file's covered length, is new data that destroys nothing (skip it).

---

## PART IV — THE GATE (D ∧ T)

The gate is the cost filter. It decides only whether a write is worth the Oracle's attention and the preserved floor. It is passive.

> **[INVARIANT] IV.1 — The gate never blocks.** The gate classifies; it never denies an operation, never delays it, never changes its result. Every block in this system comes from a conviction or a resource bound, never from the gate.

The gate fires on **D ∧ T**.

### IV.2 D — destruction of an original the writer read

**[DECISION] IV.2.1 — D is destruction of an existing, read-preceded original.** D holds when the operation overwrites, truncates, deletes, or renames-over content that already exists. For the non-mapped path the system tracks, from the open, whether the writer has read the stream; a writer that opened for write, never read, and never mapped writable is *confidently blind* — it is overwriting bytes it never read.

> **[BOUNDARY] IV.2.2 — A confidently-blind overwrite is outside FN=0 by definition.** FN=0 protects an original *a process read*. A writer that never read the original it overwrites is not reconstructing that data from a proof of its own making, and a keyless blind overwrite is a wipe, not recoverable-destruction (Part IX.3). The system does not capture the confidently-blind seam, and this is correct scope, not a gap.

### IV.3 T — conditional novelty of only the changed bytes

**[DECISION] IV.3.1 — T is diff-restricted 2-gram novelty.** Over the sample, the system builds the set of 2-grams present in the original, then examines only the byte positions where the written bytes differ from the original. Among those changed positions it counts 2-grams that are *novel* — absent from the original's 2-gram set. T is the novelty of the change, measured against the original itself.

**[DECISION] IV.3.2 — The gate fires at θ over a volume floor μ.** In a 256-byte block the gate fires when the count of novel changed 2-grams is at least `μ = 12` [DESIGN] and is at least a fraction `θ = 0.10` [DESIGN] of the changed 2-grams. Full-ciphertext novelty is ≈ 0.97 [MEASURED]; for uniform ciphertext expected novelty is `1 − |S|/2^16` where `S` is the original's 2-gram set [DERIVED]. The threshold sits far below encryption and far above ordinary edits.

> **[DERIVED] IV.3.3 — Diff-restriction defeats partial and intermittent encryption.** Because novelty is measured only over the changed bytes and against the original's own content, encrypting any region, however small a fraction of the file, fires the gate for that region. There is no "encrypt a little to stay under a whole-file entropy threshold" evasion, because there is no whole-file threshold.

> **[NEGATIVE] IV.3.4** Absolute entropy, entropy-delta, an ML classifier as the gate, and an always-preserve or always-pass stub are all forbidden as the gate. Absolute and delta entropy miss partial encryption and fire on compression; a classifier is untunable-by-contract and unprovable; the stubs abandon the cost commitment or the FN commitment.

---

## PART V — RESPONSE AND USER AUTHORITY (two layers)

### V.1 The two postures

**[DECISION] V.1.1 — AUDIT and ENFORCE.** AUDIT observes and records; it never refuses or blocks. ENFORCE adds the two active responses below. Mode is the only response-affecting control the driver accepts from the service, and it accepts exactly the two values.

### V.2 Graduated response

> **[INVARIANT] V.2.1 — Circumstantial pressure refuses one operation; definitive evidence blocks the destructive operation; nothing terminates a process.** A capacity bound (III.4.1) refuses the single operation that cannot be preserved, and paging write-backs are never the thing refused. A definitive conviction — an Oracle forward proof, or phantom accumulation to threshold — blocks the convicted actor's subsequent destructive operations. No response is process termination, and no response denies a process's non-destructive I/O.

**[DECISION] V.2.2 — A convicted actor's destructive operations are denied; its ordinary I/O is not.** Once convicted, the actor's user-mode destructive writes and destructive metadata operations are denied. Its reads, its non-destructive writes, and the system's own kernel-mode preservation writes are not affected — the deny is scoped to the destructive, user-mode operations of the convicted actor.

### V.3 User authority

**[DECISION] V.3.1 — The user owns the resource envelope and the posture; nothing else.** The user sets retention time, capacity, and AUDIT/ENFORCE. The user does not tune detection, conviction, or the gate (0.5). Recovery is offered to the user as a graded certainty — definitive (a proven key), bounded (a preserved region within budget), or unavailable — and the system never claims more certainty than it holds.

---

## PART VI — IDENTITY DISCIPLINE

Exemption is the mechanism of FP≈0. An exempt identity is monitored **zero** — no gate, no preserve, no Oracle, no capacity refusal, no phantom. Because exemption removes all observation, the entire investment is in making the exemption anchor **unforgeable**, never in watching the exempt.

> **[INVARIANT] VI.1 — Exemption is an unforgeable contract, decided in the kernel.** An identity is exempt only through an anchor the requester cannot forge: an OS-owned path under the system root, a process the OS itself protects (PP/PPL), or an operator-authorized allow-list entry bound to the specific process instance. The service may *report* an identity; the driver alone *decides* exemption, and re-checks every basis itself.

### VI.2 The anchors

**[DECISION] VI.2.1 — OS-owned path.** Files under the system root matching a fixed prefix set (the configuration hive, the event logs, the catalog store, the servicing datastore, and the like) are OS-owned and exempt as targets. The prefix set is fixed [DESIGN]; the match is normalized and anchored to the resolved system-root, and is resolved lazily at the first write seam for files opened before the filter attached.

**[DECISION] VI.2.2 — OS-protected process (Tier-1).** A process the OS runs as protected (PP or PPL) whose signer is WinSystem, WinTcb, Windows, or Antimalware is exempt, unconditionally and immutably for its lifetime. This anchor is injection-proof because the OS enforces it.

**[DECISION] VI.2.3 — Operator allow-list bound to the instance (Tier-2).** An operator authorizes an image by identity — its content hash and its signer subject, path-independent. Membership is decided in the driver against its own copy of the list. A verdict for a running process is bound to that process instance by its OS start key: the driver captured the start key at process creation, rejects any verdict carrying a zero start key, re-checks the allow-list and the interpreter denylist itself, and applies the verdict only if the carried start key equals the one it captured. This defeats PID-reuse spoofing; a user-mode assertion is never trusted on its own.

**[DECISION] VI.2.4 — Interpreters are never allow-listed.** A fixed leaf-name denylist (`powershell.exe`, `pwsh.exe`, `cmd.exe`, `wscript.exe`, `cscript.exe`, `mshta.exe`, `python.exe`, `node.exe`) can never gain allow-list exemption, verified signer or not: their behavior is their argument, not their image.

### VI.3 Two axes of trust

> **[INVARIANT] VI.3.1 — Exempt-as-target and trusted-to-manipulate-others are distinct.** Being exempt from monitoring (Tier-1 or Tier-2) does not grant the right to manipulate *other* protected processes. Only Tier-1 (OS-protected) identities are trusted to open protected process/thread handles for manipulation. A Tier-2 allow-listed process is left alone as a target but is not trusted to inject into others — this bounds the confused-deputy surface (Part IX.2).

---

## PART VII — SELF-PROTECTION (protect the two assets and the observation point)

Self-protection defends the preserved store, the keystore, and the point of observation. It protects mechanism, not by watching, but by making the protected things structurally unreachable to the untrusted.

### VII.1 The store and the channel

> **[INVARIANT] VII.1.1 — The private store is not writable by user mode.** A user-mode attempt to write, truncate, delete, or rename the system's own on-disk store is denied. The system's own kernel-mode writes to it are not user-mode and are unaffected.

**[DECISION] VII.1.2 — The service channel is authenticated three ways.** The kernel communication port is opened only by SYSTEM (its security descriptor grants no one else), only by a process whose image is the service, and only after a challenge/response the service signs with a private key whose public half is embedded in the driver (ECDSA P-256), with a matching protocol version. All three must hold before any authenticated message is accepted; a single connection is served; malformed, unauthenticated, or unknown messages increment a tamper counter and are rejected.

**[DECISION] VII.1.3 — Persistence is atomic and tamper-evident.** Every store write is a flush-to-a-temporary then rename-over, so no reader observes a half-written file. The keystore and the preserved index are MAC-chained and rollback-anchored (II.4.1); a verification failure on load discards the unverified records and raises a tamper flag rather than trusting them.

**[DECISION] VII.1.4 — Never trust a dead driver's data; re-hydrate under verification.** On a boot-start load, filtering begins before persisted state is read back, and the state is re-hydrated only in a deferred boot-reinitialization pass once the storage stack is live; on a manual start it loads synchronously. Either way the load runs the MAC/anchor verification, never a blind trust of the file.

### VII.2 Active injection-proofing

Exemption removes monitoring, so an exempt process must be made unusable as an injection host — otherwise "trusted identity" is a hole an adversary sideloads into.

**[DECISION] VII.2.1 — Strip manipulation rights from untrusted openers of an exempt target.** A pre-operation on process- and thread-handle creation and duplication strips the manipulation-grade access bits (for a process: VM_WRITE, VM_OPERATION, CREATE_THREAD, DUP_HANDLE, CREATE_PROCESS, SET_INFORMATION, SUSPEND_RESUME; for a thread: SET_CONTEXT, SUSPEND_RESUME, SET_INFORMATION, SET_THREAD_TOKEN, IMPERSONATE, DIRECT_IMPERSONATION) when the target is an exempt (Tier-2) process and the opener is neither the target itself nor a Tier-1 trusted opener. Benign rights (read, query, terminate, synchronize) pass. For a handle duplication the trust test is applied to the **recipient** of the handle, not the caller — closing the duplicate-into-an-untrusted-process primitive. In AUDIT the attempt is logged; in ENFORCE the bits are stripped. This requires the driver image to be built with integrity checking.

**[DECISION] VII.2.2 — A Tier-2 exemption is revoked when its process loads a low-signed foreign module.** If an allow-listed (Tier-2, not Tier-1) exempt process loads a user-mode image below an adequate signature level, its exemption is revoked to observation and the event is logged. Tier-1 (OS-protected) exemption is never revoked.

**[DECISION] VII.2.3 — Close the pre-verdict window at the source, with fast auto-verdict.** A launch of an allow-listed image is detected promptly (a poll of new processes) and driven through the same verdict pipeline as a manual request, so the interval between launch and exemption is short rather than operator-latency-bounded.

> **[NEGATIVE] VII.3.1 — Never force-close pre-existing handles held by other processes.** A sweep that enumerates and force-closes handles held by already-running processes reproduced a CRITICAL_PROCESS_DIED bugcheck when it closed handles held by boot-critical processes. It was built, tested, and is permanently forbidden.

> **[NEGATIVE] VII.3.2 — Never protect a not-yet-verdicted process's handle surface at creation.** Provisionally hardening a newborn allow-list candidate's handles at process-create reliably broke process creation itself, because the OS's own creation machinery needs manipulation-grade handles to the newborn. Only the OS can protect a process from birth; a third-party driver cannot. Exemption's handle-hardening legitimately begins post-verdict; the window is closed by fast auto-verdict (VII.2.3), not by protecting a newborn.

> **[NEGATIVE] VII.3.3 — Do not build process-injection *detection* as a defense.** The answer to injection into a trusted process is to make the trusted process an unusable host (VII.2.1), not to add a heuristic that watches for injection. Watching the exempt contradicts VI.1.

---

## PART VIII — THE PHANTOM WITNESS LAYER

Phantoms are decoys: files that do not belong to any workload, placed where an indiscriminate file-walker will reach them. A process that touches enough of them is walking the file system indiscriminately — behavior no ordinary application exhibits. Phantoms are evidence; they are never a recovery source.

**[DECISION] VIII.1 — Deterministic, invisible decoys.** Decoy names, sizes, timestamps, and file-reference numbers are HMAC-derived from a per-volume secret and the canonicalized directory, so they are stable across queries and reboots without a stored index. Decoy density scales with a directory's real file count. Backing content is synthesized on first touch.

> **[INVARIANT] VIII.2 — An exempt identity does not see phantoms and cannot be convicted by them.** To an exempt (Tier-1 or Tier-2) process, decoys are not merely permitted — they do not exist: they are absent from directory enumerations, from journal queries, from queries by name and by file-reference. An exempt process accrues no phantom evidence and can never reach the phantom threshold. Showing a decoy to an exempt process, or accruing evidence against it, would be watching the exempt, which VI.1 forbids; the exemption is an absolute contract.

**[DECISION] VIII.3 — Conviction at K independent touches.** A non-exempt process that writes to (or is reparsed onto the backing of) `K = 3` [DESIGN] distinct phantoms is convicted. A brand-new file that merely collides with a deterministic decoy name is let through — only touching what the process believes is an existing file counts. For a false conviction, three independent phantom touches must occur by chance; the rate is bounded near `p^3 ≈ 10^-9` [DERIVED].

---

## PART IX — BOUNDARIES (declared non-defenses)

The default state of any newly found gap is closed unless it escapes the tests in 0.3, and no condition that fails the 0.3.1 self-check appears here. What remains are the genuine lines this system does not cross.

### IX.1 [BOUNDARY] Kernel-code execution is game over

A user-mode service plus a minifilter structurally cannot defend against an equal-or-higher-privilege kernel attacker: such an attacker bypasses the hooks, reads the pool, and can destroy the keystore and the preserved store directly. This is the architectural ceiling of all minifilter defenses. On-disk store confidentiality likewise stands only against a non-kernel attacker; a kernel attacker who can read the pool can read the store key.

### IX.2 [BOUNDARY] The exempt-identity trust ceiling

An exemption is a contract; the system invests in making it unforgeable, not in policing its holder. A genuinely OS-protected (Tier-1) identity is trusted to the same degree the OS trusts it. A Tier-2 allow-listed binary can, post-exemption, be a confused deputy abused through a legitimate feature of the trusted image — an accepted residual that VI.3.1 bounds (Tier-2 is not trusted to manipulate others) and VII.2.2 shrinks (a foreign-module load revokes it). An in-process memory-safety exploit within a trusted process, or a shared writable executable section the trusted process itself maps, sits at the same ceiling the OS's own PP/PPL accepts.

### IX.3 [BOUNDARY] Wiper / no-key destruction

Plaintext-over-plaintext destruction, pure deletion, and zeroing produce no recoverable key and are not targets of the Oracle: they have no unbounded recovery. Such destruction is still *preserved* within the resource envelope like any other seam and is recoverable from the floor (Part III) — except where it is confidently blind (IV.2.2), content the writer never read, which is outside FN=0 by definition and is not captured. Beyond the envelope it is refused under ENFORCE like any other seam (III.4.1). A genuine no-key wipe's unbounded recovery is out of scope; a wiper is not viable ransomware: no key, no ransom.

---

## PART X — COMPATIBILITY AND DEPLOYMENT

**[DEPLOY] X.1 — Signing and load.** The minifilter ships with an EV-signed, WHQL-attested, Microsoft-co-signed image at an allocated altitude, built with integrity checking (required for the handle-callback registration of VII.2.1). The service image is Authenticode-signed. The handle-callback component registers at the activity-monitor altitude band.

**[DEPLOY] X.2 — The early-launch component is a boot-order shell, not a classifier.** The ELAM image exists to obtain an early-launch boot-start classification for the protection stack through the build and registry machinery; its runtime entry does no malware classification and gates nothing. It must not be described or relied upon as an active detector.

**[DECISION] X.3 — Attach discipline.** The filter declines to attach to DAX volumes, resolves per-volume geometry for the mapped-section path on attach, and forces platform fast-I/O-bypass off where negotiable so its own path cannot be circumvented.

---

## PART XI — CONFORMANCE CHECKLIST

An implementation is constitutional iff all hold.

**The heart (Parts I, II)**
- [ ] Response is graduated to evidence; the gate never blocks; nothing terminates a process (I.1, IV.1, V.2.1).
- [ ] Conviction is a forward proof `Transform_K(original)==written`; key-presence and the reverse direction never convict (II.1).
- [ ] The battery covers the fixed cipher/mode set, structural-scan first, including the stream-constant scan (II.2.2–II.2.3).
- [ ] Recovery verifies the key against a captured original sample in constant time before any write, and replaces atomically, never in place (II.3.1–II.3.2).
- [ ] A full keystore never causes a live preserved original to be dropped (II.4.2).

**Recovery and preservation (Part III)**
- [ ] The pre-image is the true, pre-commit original; region-only, never whole-file (III.1.1–III.1.2).
- [ ] First-write-wins; partial-overlap staging preserves the uncovered remainder from the persisted store, across close/reopen (III.1.3–III.1.4).
- [ ] Truncate, delete, and rename-over preserve their content before commit (III.2.1).
- [ ] Probation promotes to protected on conviction; a captured key reconciles held ranges by containment (III.3.1–III.3.2).
- [ ] ENFORCE refuses a destructive operation rather than evict a live original; a preservation-path resource failure is fail-closed; protected is never evicted to make room; retention expiry applies to all holds (III.4.1–III.4.3).
- [ ] The mapped seam reserves-or-refuses at section-arm, captures region-only at paging write-back against the reservation, and tells new writes from overwrites by extent type; whole-file eager capture is absent (III.5.1–III.5.3).

**The gate (Part IV)**
- [ ] D is read-preceded destruction; the confidently-blind seam is out of scope by design (IV.2.1–IV.2.2).
- [ ] T is diff-restricted 2-gram novelty at θ=0.10 over μ=12; no whole-file entropy threshold exists anywhere (IV.3.1–IV.3.4).

**Response and identity (Parts V, VI)**
- [ ] Capacity refuses one operation; conviction blocks the actor's destructive operations only; ordinary and kernel-mode I/O is untouched (V.2).
- [ ] Exemption is decided and re-checked in the kernel across all three anchors, bound to the instance by start key; interpreters are never allow-listed (VI.1–VI.2).
- [ ] Exempt-as-target and trusted-to-manipulate-others are distinct; only Tier-1 is trusted to manipulate others (VI.3.1).

**Self-protection and phantom (Parts VII, VIII)**
- [ ] The private store is not user-writable; the channel is SYSTEM-only + image + signed-handshake + version; persistence is atomic and tamper-evident; dead-driver data is re-hydrated under verification (VII.1).
- [ ] Untrusted openers of an exempt target are stripped of manipulation rights, recipient-evaluated on duplication; Tier-2 exemption revokes on a low-signed module load; the two forbidden self-protection mechanisms are absent (VII.2–VII.3).
- [ ] Phantoms are invisible to and inert against exempt identities; conviction at K=3 distinct touches; new-file collisions pass (VIII.2–VIII.3).

**Scope (Parts 0, IX)**
- [ ] No clause defends a condition that fails the 0.3.1 self-check; every recorded boundary is a genuine capability ceiling, not an environmental failure mode (0.3.1, IX).
