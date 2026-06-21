# semantics-ar — IMPLEMENTATION CONSTITUTION

> STATUS: RATIFIED. This document is the authoritative specification for the
> semantics-ar defensive anti-ransomware system (Windows filesystem minifilter +
> user-mode service). It governs implementation. Where code and this document
> disagree, this document wins and the code is wrong.

================================================================================
## §0  HOW AN IMPLEMENTER READS THIS DOCUMENT  (READ FIRST — BINDING)
================================================================================

### §0.1  What this system is
A **preserver/observer/adjudicator**. It (a) preserves the original bytes of a
file before they can be destroyed (data-safety), and (b) proves a write is
encryption via the Oracle Test — recover candidate key K and verify
`Encrypt_K(P) == C` (conviction). It contains NO ransomware payload, NO attack
tooling, NO evasion implementation. Every threat-model statement exists ONLY to
decide **where the defender places a preservation/adjudication hook**.

### §0.2  This document is a CLOSURE BOUNDARY, not a tutorial
You (the implementer) will re-research each scope you implement, and you will
reconstruct your own understanding. That is expected and correct. This document
does NOT exist to re-teach you. It exists to **fix the boundary of what your
re-research is allowed to reopen.**

> Your re-research answers HOW to implement a conclusion.
> It MUST NOT be used to reopen WHETHER a conclusion holds.
> Every CLOSED CONCLUSION below is already settled. Re-deriving it differently
> is a constitutional violation, not a contribution.

### §0.3  Item types (every normative item carries exactly one)
- **[INVARIANT]** — Must always hold. If your implementation can violate it under
  any input, timing, failure, or supported OS, the implementation is wrong.
  Non-negotiable.
- **[DECISION]** — A chosen method. Implement it as written. You may not
  substitute a different method merely because re-research surfaced one, unless
  it falls within that item's IMPLEMENTATION LATITUDE.
- **[BOUNDARY]** — A thing we deliberately do NOT defend against. You MUST NOT
  add defenses past this line. Attempting to is a violation (see §8, slope).
- **[NEGATIVE]** — A thing you MUST NOT do.
- **[DEPLOY]** — A deployment/environment precondition that research cannot
  close; the named verification/configuration IS the closure method.

### §0.4  Anatomy of a normative item
Each item may contain:
- **CLOSED CONCLUSION** — the settled statement. Binding.
- **CLOSURE SCOPE** — what this closure covers; what re-research findings are
  already subsumed; what is explicitly out of this item (and where it goes
  instead). This operationalizes "closed."
- **PRE-EMPTIVE CLOSURE** — (present only where re-research will strongly tempt
  you toward a contrary conclusion) the most plausible counter-finding, written
  out and closed in advance. If your research surfaces that finding, it is
  ALREADY ANSWERED HERE. Do not reopen on its basis.
- **IMPLEMENTATION LATITUDE** — what you are free to decide. Your re-research
  belongs here.
- **DEPENDS-ON** — items that must be satisfied for this item to be implementable.
- **RATIONALE** — present ONLY for [BOUNDARY] and [NEGATIVE], one line, so you
  understand why not to cross the line.

### §0.5  The two-axis discipline (BINDING on all reasoning, not just code)
This system has two independent axes:
- **DATA-SAFETY** (never lose an original) — must close ON ITS OWN.
- **CONVICTION** (catch the attacker in real time) — is best-effort by OS limit.

> [NEGATIVE] You MUST NOT discharge a limit on one axis by appealing to the other.
> If conviction is weak somewhere, you may NOT call it "fine because data is safe."
> Data-safety must be argued from preservation alone; conviction must be argued
> from the Oracle alone. Cross-subsidy between axes to make something LOOK closed
> is a violation. The axes may degrade gracefully and independently; they may not
> rescue each other rhetorically. (DERIVES: parent T8 axis independence.)

The same independence governs how the two axes meet the **operating environment**
(§0.9). Data-safety closes on the minifilter/preservation surface that exists on
every minifilter-capable Windows; the §6 self-protection threat model closes only
to the extent the platform supplies its primitives. A weakness in the second is
NOT a weakness in the first, and MUST NOT be argued as one in either direction.

### §0.6  Slope control — the default state of any newly-found gap is CLOSED
While implementing, you WILL discover new-looking attack surfaces or failure
modes. Before treating any as open work, classify it:
1. Is it a corollary of an existing [INVARIANT]?  → CLOSED. Implement to the invariant.
2. Is it inside a declared [BOUNDARY]?            → CLOSED. Do not defend it.
3. Is it already covered by an existing [DECISION]? → CLOSED. Apply that decision.
If and ONLY if none of the three apply, escalate it as a genuine open item.

> [NEGATIVE] Default-open is forbidden. "I found a new path, therefore there is a
> new hole" is the slope this project is defined against. Default is CLOSED;
> the burden is on showing it escapes all three categories above.

### §0.7  The single reopening test (apply to every objection you generate)
For any objection re-research raises, ask exactly one question:

> Does this change HOW I implement a conclusion, or does it reopen WHETHER the
> conclusion holds?

- Changes HOW → legitimate; resolve it inside IMPLEMENTATION LATITUDE.
- Reopens WHETHER → STOP. The conclusion is closed. The objection is the slope.

### §0.8  WHAT vs HOW
This document specifies **WHAT must be true** (constraints, precise enough to be
mechanically checkable). It deliberately does **NOT** specify **HOW** (data
structures, algorithms, libraries) except where a HOW is itself load-bearing and
promoted to [DECISION]. Implementation freedom is real; constraint satisfaction
is mandatory.

### §0.9  The compatibility contract (BINDING; a property of the two axes, not a feature)
This system targets **every Windows release that supports file-system
minifilters (Filter Manager)**. Compatibility is not a bolt-on; it is the direct
consequence of §0.5 applied to the platform. The two axes meet the OS at
different floors, and conflating them mis-scopes the system:

- **DATA-SAFETY axis** rides only on surface present across the whole
  minifilter-capable range: FltMgr I/O DDIs, stream/instance contexts, CNG,
  and the destruction-enumeration of §3. Therefore §1.1 is delivered on **every
  supported OS**, down-level included, by runtime feature detection (§0.10) — NOT
  by raising the floor.
- **SELF-PROTECTION axis (§6)** rides on platform security primitives (HVCI-backed
  pool secrecy, TPM-rooted key residence, Dev-Drive attach policy) that are a
  **function of the OS generation**. Where a primitive is absent, the boundary it
  guards (§7.1: "kernel-code execution = game over") descends to a lower
  privilege on that OS; nothing else about the architecture changes.

> [INVARIANT] §1.1 holds on every minifilter-capable OS. The §6 boundary
> descends, per OS generation, exactly as far as the missing primitive would have
> raised it — no further, no less. The system MUST detect each primitive at
> runtime and, where absent, record the descended boundary EXPLICITLY rather than
> present a silently-weakened guarantee (this is §0.5's "no rhetorical rescue,"
> applied to the environment). The data-safety ceiling is unaffected and is
> argued from preservation alone.

> [NEGATIVE] The absence of a platform primitive is NOT a defect — neither of this
> system nor of that OS. It is the security capability of that OS generation.
> Do NOT frame it as a defect (which misassigns responsibility), and do NOT
> attempt to synthesize the missing hardware guarantee in software (which is the
> §8 slope — e.g. self-obfuscation, injection-detection as a substitute for pool
> secrecy). Detect, scope honestly, preserve regardless.

- DEPENDS-ON: §0.5, §0.10, §6, §7.1, §9.

### §0.10  Single binary, runtime feature detection (the compatibility mechanism)
- **[DECISION]** One source tree → one binary per CPU architecture. Newer
  DDIs/structures/info-classes are made visible at compile time (target
  `NTDDI_VERSION` high) and the newer *calls* are gated at runtime; setting the
  build target low hides surface you may optionally use and buys nothing for load
  compatibility.
- The driver MUST still **load** on the lowest supported OS. Any routine whose
  import would otherwise fail to resolve on a down-level kernel is resolved at
  runtime (`MmGetSystemRoutineAddress` / `FltGetRoutineAddress`) with the
  down-level alternative invoked when absent; OS-version-gated *behavior* branches
  on `RtlGetVersion`. Microsoft's own minifilter samples establish this pattern;
  it is the canonical way one binary spans the range.
- A *superset* of operation callbacks, FSCTL codes, and info-classes is correct:
  an operation a given OS never issues never reaches the handler, so the handler
  arm is inert there (§3.1, §13 of §6.3 referenced via §9). Defining a constant
  an OS does not recognize is harmless.
- **[NEGATIVE] No dead code, no compatibility fallback for its own sake.** A
  down-level branch exists ONLY where the up-level path would fail to load or
  function on a supported OS. Defensive branches for operations that cannot reach
  a given callback, or wrappers that resolve identically on every supported OS,
  are forbidden — they are the dead code this project is defined against.
- CLOSURE SCOPE: This is the ONLY sanctioned compatibility mechanism. Multiple
  build *variants* are forced by SIGNING, not by code (§9); do not split the
  source tree to chase OS coverage.
- IMPLEMENTATION LATITUDE: which routines warrant runtime resolution vs. an
  unconditional call (decided per supported-floor, §9), the version-branch
  granularity, the probe order.
- DEPENDS-ON: §0.9, §9.

================================================================================
## §1  TOP-LEVEL INVARIANTS  (the two axes; everything below derives)
================================================================================

### §1.1  [INVARIANT] DATA-SAFETY
For every operation that would destroy the original bytes of an existing file,
the original bytes are preserved (recoverable) BEFORE the destruction is allowed
to take effect — regardless of conviction outcome, timeout, deadlock, key
state, suspend behavior, attacker identity, or **supported OS generation**.
- CLOSURE SCOPE: This is the ceiling. "Lose nothing" is the maximum; no
  improvement exists above it. Re-research cannot raise this; it can only fail to
  meet it. Meeting it is success. It is delivered on every minifilter-capable OS
  (§0.9) because it depends only on §2/§3 surface, never on §6 primitives.
- DEPENDS-ON: §2, §3.

### §1.2  [INVARIANT] CONVICTION-IS-BEST-EFFORT
Real-time proof of encryption (Oracle) is provided whenever OS/crypto conditions
permit, and is structurally best-effort where they do not. Best-effort, here, is
the OS-imposed ceiling — a CONFIRMED LIMIT, therefore itself a closure.
- CLOSURE SCOPE: You may not "fix" conviction past the OS ceiling; attempts to do
  so are slope (§0.6). Cumulative capture across N files and saturation signaling
  are the accepted amplifiers (§4.4). Single-shot probability is not a target.
- PRE-EMPTIVE CLOSURE: Re-research will surface families with structural p≈0
  (pure-software AES never loading round keys to XMM; per-file-key zeroized
  immediately; mmap cold-flush). This does NOT reopen conviction. Those cases are
  CLOSED as confirmed-limit, AND data remains safe via §1.1 independently (stated
  per-axis, NOT as cross-subsidy: §1.1 is met by preservation alone; this clause
  only notes the two independent facts coincide). Do not build new conviction
  machinery to chase p≈0 families.
- DEPENDS-ON: §4.

================================================================================
## §2  DATA-SAFETY AXIS  (preserve-gate, ordering, fail-safe)
================================================================================

### §2.1  [INVARIANT] PRESERVE-BEFORE-ORACLE ORDERING
On a detected destructive operation, the control flow is fixed:
```
detect destructive touch (D∧T, §3)
  → PRESERVE the original (complete & verified)
  → release/allow the operation to proceed
  → THEN attempt Oracle (conviction) outside any held-IRP state
```
Preservation completes before any conviction attempt and before any memory scan.
Conviction success/failure/timeout/suspend MUST NOT be able to affect whether
preservation happened.
- CLOSURE SCOPE: This single ordering is what makes timeout, deadlock, key-miss,
  and suspend all data-independent. It is a DERIVATION from §1.1 + parent T1 + T8,
  not a tunable. (DERIVES: §3.8, §3.9 of closure record.)
- PRE-EMPTIVE CLOSURE: Re-research into `FltSendMessage` will show a ~3s
  synchronous-hold timeout, and into minifilter rules will show "must not hold a
  resource across an I/O." Both are real and ALREADY ACCOMMODATED by this
  ordering: because preserve precedes Oracle and the IRP is released before any
  memory scan, a timeout or a faulting scan cannot lose data. The notification to
  the service that an adjudicable candidate exists MUST NOT block the IRP on a
  user-mode round-trip; it is fire-and-forward (or a hold so brief it cannot
  approach the FltSendMessage ceiling), and the actual Oracle runs service-side
  after release. This does not reopen anything.
- DEPENDS-ON: §3 (what counts as destructive), §2.3 (fail-safe).

### §2.2  [INVARIANT] PRESERVE-GATE NEVER BLOCKS
The trigger emits PRESERVE-or-SKIP. It NEVER blocks an operation. Blocking/
conviction is strictly downstream and has its own abstain guarantees.
Error asymmetry is fixed: a missed preserve (→ data loss) is FORBIDDEN
(recall ≈ 1 on true destruction); over-preservation is mere window pressure,
absorbed downstream. Tune the trigger toward over-preservation.
- CLOSURE SCOPE: A loose trigger CANNOT create a false block, because the trigger
  cannot block at all. Do not add a blocking branch to the trigger.
- DEPENDS-ON: §1.1.

### §2.3  [INVARIANT] PRESERVE-FAILURE IS FAIL-SAFE (never fail-open)
If preservation cannot complete (disk-full, I/O error, shadow-store exhaustion,
partial copy, unreadable source), the operation MUST NOT be allowed to destroy
the original. The write is held/denied; it is NEVER passed through unpreserved.
Every capture failure resolves to preserve-or-hold. This is uniform across ALL
destruction paths (§3.1) and ALL supported OS — a path that fails to preserve and
silently passes the destruction through is the single way to breach §1.1.
- CLOSURE SCOPE: This is a COROLLARY of §1.1 + §2.2, not new research. Re-research
  into preserve-copy failure modes does NOT open a new item; all of them resolve
  here: "cannot preserve ⇒ do not let the original die." Partial/striped writes:
  resolve destruction by POST-OP ground truth (§3.5), then preserve-or-hold.
- PRE-EMPTIVE CLOSURE: You may be tempted to engineer atomicity guarantees for
  the preserve operation as a separate subsystem. The CONSTRAINT is only
  "no unpreserved destruction." HOW you achieve fail-safe (retry, redirect, hold,
  reject) is IMPLEMENTATION LATITUDE. Do not promote it to a new open problem.
  Separately: the read used to capture original bytes MUST NOT itself become a
  hidden fail-open — a read mechanism that fails on un-sector-aligned offsets
  (e.g. a non-cached read at an arbitrary offset) would turn an ordinary
  destructive write into an unpreserved pass-through. The capture read MUST
  succeed at the arbitrary offsets real destruction arrives on, or fail-safe.
- IMPLEMENTATION LATITUDE: copy-on-write vs redirect-on-write vs hold; retry
  policy; shadow-store backpressure handling; capture-read mechanism subject to
  the no-fail-open constraint above.
- DEPENDS-ON: §1.1, §2.2.

### §2.4  [DECISION] PRESERVATION MECHANISM = REDIRECT/COPY AT EARLIEST SAFE CALLBACK
Default to copy-on-first-modification at the earliest **safe non-paging**
callback. For the residual paging-only path, use redirect-on-write. Same-file
synchronous reads on the paging-write path can deadlock under memory pressure;
therefore preservation MUST NOT depend on a synchronous same-file read on that
path. On a non-paging callback the pre-image is still present in cache and on
disk (destruction has not yet taken effect), so a cache-coherent same-file read
there is sound; the deadlock constraint is specific to the paging-write path.
- CLOSURE SCOPE: Converts the gate from fail-open to fail-safe (with §2.3). The
  shadow artifact for any single preserved range MUST be uniquely named (a
  monotonic sequence or equivalent), never overwrite-if-exists, so two captures
  in the same time tick cannot clobber one another — clobbering a shadow is a
  §1.1 breach.
- IMPLEMENTATION LATITUDE: read-consistency layer for redirected files (serve
  in-flight reads from shadow; cache-coherency on shared cached pages); cost
  budget. Mechanism is yours; the no-deadlock and read-consistency CONSTRAINTS
  are not.
- DEPENDS-ON: §2.1, §2.3, §3.

================================================================================
## §3  D-AXIS COMPLETENESS + FSCTL MEDIATION  (what counts as destruction)
================================================================================

### §3.0  [INVARIANT] DESTRUCTION ENUMERATION IS THE FILESYSTEM INTERFACE
The set of ways an original can be destroyed is a DECIDABLE enumeration over the
NTFS/ReFS interface, because it IS the filesystem interface. Preservation rides
the pre-op of whatever IRP destroys the original. Completeness means: for every
member of this enumeration, a preserve point exists before destruction.
- CLOSURE SCOPE: "Decidable enumeration" is why D-completeness is closable at all.
  New members can appear only via new OS interface surface; finding one is a
  DECISION-extension (add the hook), not a reopening of §1.1. Conversely, members
  that exist only on newer OS (block-clone, POSIX/Ex info-classes) are simply
  never emitted by older OS, so handling their superset is portable (§0.10) and
  the legacy info-classes carry the same destruction on down-level OS (§3.1).
- DEPENDS-ON: §2.1.

### §3.1  [DECISION] D-AXIS MEMBER SET (preserve on the destroying IRP's pre-op)
Members (each: preserve before it takes effect):
- In-place overwrite — `IRP_MJ_WRITE` (cached & non-cached, incl. paging writeback)
- Truncate — `SetEndOfFile` shrink / `FileEndOfFileInformation` (non-AdvanceOnly)
  AND `FileAllocationInformation` shrink (distinct cluster-deallocation vector)
- Delete — `FileDispositionInformation` AND `FileDispositionInformation[Ex]`
  (original moved to shadow). Both classes MUST be handled: down-level OS emit
  only the legacy class, newer OS may use Ex (incl. POSIX semantics) — coverage
  must not depend on the OS choosing one form.
- mmap/section write — captured via §3.3 (CoSC), surfaced as paging-write
- rename-over-existing — `FileRenameInformation` AND `FileRenameInformationEx`
  + ReplaceIfExists → §3.4 (legacy + Ex, same portability reason as delete)
- hardlink-replace — `FileLinkInformation` AND `FileLinkInformationEx` replace
- block-clone / ODX — `FSCTL_DUPLICATE_EXTENTS_TO_FILE` / `FSCTL_OFFLOAD_WRITE`
  (ReFS/ODX; absent on down-level OS, inert there) → mediate per §3.6
- destructive/detaching FSCTL class → §3.6 (SINGLE DEFINITION; do not re-list)
- BypassIO — NOT a destruction path (§3.7); read-only today; veto preserves view
- volume raw-write precursor — `FSCTL_LOCK_VOLUME`/`FSCTL_DISMOUNT_VOLUME` → §3.6
- IMPLEMENTATION LATITUDE: per-member detection mechanics, chunk/stripe handling.
- CLOSURE SCOPE: The legacy/Ex pairing above is the portability requirement of
  §0.9 made concrete: a member's coverage must hold whether the running OS uses
  the old or new info-class. This is not two code paths for compatibility's sake
  (§0.10 NEGATIVE) — it is one classifier that the OS feeds via whichever class
  it actually emits.
- DEPENDS-ON: §3.0, §3.6.

### §3.2  [DECISION] JUDGMENT UNIT = WRITTEN RANGE R, NOT WHOLE FILE
Classify the byte range actually overwritten, not the file average. Defeats
partial/intermittent encryption dilution. Header-only / 64KB-slice encryption is
the easy case under this rule, not the hard case.
- DEPENDS-ON: §3.1.

### §3.2.1  [DECISION] T-AXIS = INFORMATION-CHARACTER TRANSITION (AIT-grounded)
Signal T = collapse of meaningful (algorithmic) information across an in-place
replacement: old(R) had a compact model, new(R) has none (≈ its own length).
NOT absolute entropy (JPEG/H.264/zip are high-entropy but modeled). Implement via
conditional compression magnitude `C(new|old) ≈ C(old·new) − C(old)`, on fixed-
size aligned blocks within the compressor window; relative/transition magnitude,
never a single absolute threshold.
- PRE-EMPTIVE CLOSURE: Re-research will surface entropy-only and ML-classifier
  detectors. Both are REJECTED as the gate (see §8). Entropy-only misclassifies
  modeled-high-entropy data; ML-as-gate is rejected. They may exist only as cheap
  pre-filter HINTS, never as the gate. The T-gate MUST be a real measurement, not
  an always-preserve stub: an always-preserve trigger satisfies §1.1 vacuously
  but floods the shadow store and the conviction path, making the system
  unusable — usability is part of "production quality," not optional.
- IMPLEMENTATION LATITUDE: compressor choice, block size within NCD caveats,
  cheap first-screen ordering, integer-only PASSIVE-safe primitive. Note the exact
  cross-prediction measure is novel/unvalidated against published detectors; it
  is sound to deploy because misclassification costs window/latency, never a false
  block (§2.2), and the threshold is tunable post-deploy.
- DEPENDS-ON: §3.2.

### §3.3  [DECISION] mmap CAPTURE = CoSC (capture-on-section-creation)
Capture at section creation, via the `ACQUIRE_FOR_SECTION_SYNCHRONIZATION`
filter callback (PASSIVE_LEVEL), for UserMode writable R/W sections.
- CLOSED CONCLUSION: Capture occurs at section-create, which is temporally BEFORE
  any modification (the app has only mapped, not written). The disk original is
  intact at that instant by definition. Therefore window duration is IRRELEVANT
  to data-safety. Capture here means actually preserving the bytes, not merely
  flagging that a writable section exists; the flag without a preserve is not a
  capture. Once a stream is preserved on this path, re-preservation on the same
  open is suppressed (cost control, not a safety relaxation).
- PRE-EMPTIVE CLOSURE: Re-research will suggest "a safe window exists between
  section-create and first flush." That justification is WRONG and must not be
  used — under memory pressure writes behave write-through. The CORRECT and
  binding justification is "capture precedes modification," which does not depend
  on any window. Write-through collapse touches conviction timing only, never
  data-safety (§0.5: stated per-axis). The callback may legitimately fail section
  creation (STATUS_INSUFFICIENT_RESOURCES) if preservation cannot complete — this
  is the §2.3 fail-safe on this path, not a defect.
- CLOSURE SCOPE: "Is there a writable-section path that skips this callback?" is a
  negative not provable by search → §9 [DEPLOY] instrumentation, NOT an open
  research item. CoSC's data-safety on the mmap path is conditional on that single
  deploy assumption, which is stated, not hidden.
- DEPENDS-ON: §3.0, §2.1.

### §3.4  [DECISION] rename-over victim = CoFW the target before completion
`rename(ReplaceIfExists, target exists)` is a destructive touch on the replaced
target. Strip `SL_OPEN_TARGET_DIRECTORY` before name queries and restore before
passing down; resolve victim from `FILE_RENAME_INFORMATION` synchronously in
requestor context; handle `FILE_RENAME_POSIX_SEMANTICS` (open handles stay valid
→ "no other handle ⇒ safe" logic is INVALID); verify tunneled name post-op.
- CLOSURE SCOPE: The victim — not the rename source — is what is preserved, and
  the recovery journal MUST record the VICTIM's path so restoration targets the
  victim. A preserve helper that derives the recorded path from the operation's
  source file object instead of the victim corrupts recovery; the preserved bytes
  and the recorded path must both be the victim's. (Same requirement for
  hardlink-replace.)
- DEPENDS-ON: §3.1, §3.5.

### §3.5  [INVARIANT] DESTRUCTION CONFIRMED BY POST-OP GROUND TRUTH
The NT stack is async; pre-op observation order is NOT authoritative. Eviction/
release/"was it actually destroyed?" decisions resolve by post-op status or
re-open check, never by pre-op ordering.
- DEPENDS-ON: §3.0.

### §3.6  [DECISION] ⟦FSCTL-MEDIATION-COMPLETENESS⟧ — SINGLE AUTHORITATIVE DEFINITION
> This is the one place the destructive/detaching FSCTL class is defined.
> §3.1 and §6.3 REFERENCE this anchor by name ⟦FSCTL-MEDIATION-COMPLETENESS⟧.
> Do not re-list these codes elsewhere; edit only here.

The minifilter MUST register `IRP_MJ_FILE_SYSTEM_CONTROL` and mediate at least:
- **Detaching (enable raw-sector bypass):** `FSCTL_LOCK_VOLUME`,
  `FSCTL_DISMOUNT_VOLUME` (incl. legacy `FSCTL_FORCE_DISMOUNT`).
  On a non-removable DATA volume, treat as imminent-bulk-destruction →
  arm preserve/redirect or deny. (System/pagefile volume cannot be locked/
  dismounted while running — OS-enforced.)
- **Direct data destruction/trim:** `FSCTL_FILE_LEVEL_TRIM`,
  `FSCTL_SET_ZERO_DATA`, `FSCTL_SET_SPARSE`.
- **Backup-history destruction:** VSS shadow-deletion path
  (`vssadmin delete shadows` and equivalent volsnap IOCTLs).
- **Block-clone/ODX (ReFS):** `FSCTL_DUPLICATE_EXTENTS_TO_FILE`,
  `FSCTL_OFFLOAD_WRITE`.
- CLOSED CONCLUSION: With this class mediated in addition to `IRP_MJ_WRITE`, a
  NON-KERNEL (admin) actor cannot destroy file DATA on a mounted filesystem; the
  only remaining sub-filter data-destruction path requires kernel code execution,
  which reduces to §6.3 / §7.1. The codes an OS does not implement are never
  emitted to the handler and are inert there (§0.10) — a superset registration is
  correct across the whole range and introduces no down-level risk.
- PRE-EMPTIVE CLOSURE: Re-research WILL show that `IRP_MJ_WRITE` alone is
  insufficient — that admin can `FSCTL_DISMOUNT_VOLUME` a data volume then write
  raw sectors, or `FSCTL_SET_ZERO_DATA` a file's backing directly, WITHOUT kernel
  code. This is EXPECTED and is exactly why this class is mediated. It does NOT
  reopen the §6.3 single-gate reduction; it COMPLETES it. The reduction's gate is
  "kernel code execution ∪ this FSCTL class," and this anchor is the second term.
- IMPLEMENTATION LATITUDE: per-code action (arm-preserve vs deny vs redirect);
  data-vs-system volume discrimination; future ReFS-revision additions; whether a
  range-scoped destroyer (e.g. file-level-trim) is preserved range-scoped or via
  conservative full-stream capture (the latter is over-preservation, §2.2-safe).
- DEPENDS-ON: §3.0, §2.1.

### §3.7  [BOUNDARY] BypassIO is not a destruction path
- CLOSED CONCLUSION: BypassIO is a read-optimization (read-only today). A
  minifilter that filters `IRP_MJ_WRITE` and does NOT declare
  `SUPPORTED_FS_FEATURES_BYPASS_IO` (or calls `FltVetoBypassIo`) forces fallback
  to traditional I/O on its volume, preserving write visibility. Mediation of
  offload writes is performed in the FSCTL handler (§3.6), NOT by the registry
  `SupportedFeatures` value — that registry value is not an enforcement mechanism
  and end users can change it; declaring offload support there while intending to
  block offload is incorrect.
- RATIONALE: Treating a read-only, vetoable path as a destruction hole is slope.
- [DEPLOY] Track the OS "BypassIO for Filter Drivers" surface each release; if
  write-side BypassIO is ever added, the veto/non-declare path remains the
  supported way to keep visibility (§9).

================================================================================
## §4  CONVICTION AXIS  (Oracle)
================================================================================

### §4.1  [INVARIANT] ORACLE IS THE PROVENANCE PROOF
The relation `C = Encrypt_K(P)` is itself the causal provenance proof: a recovered
key for candidate P simultaneously proves C came from P and that it is encryption.
Content-similarity provenance breaks on encryption (C≠P); the Oracle is
strengthened by encryption. Wrong P-candidates simply fail to yield a key
(fail-safe: cost, not accuracy).
- DEPENDS-ON: §1.2.

### §4.2  [DECISION] ORACLE FIXED TO THE WRITE IRP
Oracle runs at the WRITE IRP (the only moment K is hot). Preservation tracks the
DESTRUCTION IRP (§2.1). One invariant (Oracle@write) + one variable
(preserve@destruction). Guaranteed by the read→encrypt→write causal order.
- DEPENDS-ON: §4.1, §2.1.

### §4.3  [DECISION] REGISTER-FIRST, DEFER MEMORY-SCAN; OFFLINE CIPHER BATTERY
- Register (XMM/FXSAVE) capture is fault-free and MAY run during a short
  synchronous hold (kernel-saved, no user paging, no FS re-entrancy).
- Memory-scan fallback runs ONLY after the IRP is released (§2.1), outside any
  held state. Colder key there is accepted best-effort.
- The cipher-verification battery (AES/XTS/3DES/SM4/Camellia/ARIA/SEED + stream)
  needs NO live process: it runs OFFLINE on captured candidates + the P/C samples
  already in the driver message.
- The conviction round-trip MUST NOT be performed synchronously inside the write
  pre-op holding the IRP. The driver notifies; the service suspends/captures/scans
  after the IRP is released; a confirmed verdict is delivered back asynchronously
  and acted on as a downstream block (§5.2), never as an in-pre-op user-mode wait.
- PRE-EMPTIVE CLOSURE: Re-research into `NtSuspendProcess` will raise deadlock and
  "MS forbids it" concerns. The classic suspend-deadlock is SAME-process; our
  suspender (service) and target differ, so it does not auto-apply. The held-IRP
  re-entrancy deadlock is structurally eliminated by §2.1 (release before scan),
  NOT by probabilistic tuning. Do not reopen as a data-safety risk; it is at most
  a conviction-timing/availability cost ([DEPLOY] §9).
- DEPENDS-ON: §4.2, §2.1.

### §4.4  [DECISION] CONVICTION-RATE LEVERS = ROUND-KEY INVERSION + CUMULATIVE-N
- **Round-key inversion (DOMINANT lever for AES-NI):** AES key expansion is
  invertible. During AES-NI encryption the XMM round keys ARE present; invert the
  schedule from any captured round key to recover the original key. Treating XMM
  contents only as an original key is a code limitation, not a fundamental limit.
  Each captured 16-byte register value is therefore a candidate original key AND a
  candidate round key at each schedule position (AES-192's 6-word groups require
  tracking the word offset when inverting).
- **Cumulative-N:** ransomware encrypts N=hundreds–thousands of files = N
  independent capture chances; capture `= 1−(1−p)^N → 1` rapidly for any p>0. A
  single warm hit collapses the campaign (global/session key) or at minimum fixes
  the originator.
- PRE-EMPTIVE CLOSURE: No public number exists for "fraction with key live in XMM
  at the intercepted write." Do NOT invent one; the design correctly does not
  depend on a single p. Governing variables are (a) round-key inversion
  implemented? (b) does capture timing overlap the active loop? — not the
  original-vs-round-key distinction. Independence breaks only for structural-p≈0
  families (closed under §1.2 PRE-EMPTIVE CLOSURE; do not cross-subsidize axes).
- IMPLEMENTATION LATITUDE: sampling schedule, originator accounting integration,
  adaptive-sampling to keep effective N high.
- DEPENDS-ON: §4.3.

### §4.5  [DECISION] SATURATION IS INDEPENDENT EVIDENCE
What accumulates as preserved-but-unadjudicated occupancy is, by construction,
"destroyed-original ∧ random ∧ non-whitelisted ∧ unadjudicated" = legitimately
attack-suspect. Occupancy exists ONLY if an original was actually destroyed
(D∧T); benign apps fail Gate D or Gate T and never reach occupancy.
- PRE-EMPTIVE CLOSURE: mmap does NOT create an unadjudicable third state polluting
  saturation; it changes the destruction PATH, not the D∧T gate. Do not reopen.
  Saturation is EVIDENCE/weighting only; it MUST NOT be wired into a key-less
  auto-block on a raw shadow-bytes percentage — that mechanism is rejected (§8).
  A confirmed transition to "convicted" originates ONLY from the Oracle's behavior
  proof, never from occupancy alone.
- DEPENDS-ON: §4.4, §3.

================================================================================
## §5  IDENTITY DISCIPLINE  (the load-bearing anti-confused-deputy rule)
================================================================================

### §5.1  [INVARIANT] DATA-SAFETY AND ORACLE ARE IDENTITY-INDEPENDENT
The preserve-gate (§2.2) decides on D∧T alone and never on who issued the write.
The Oracle (§4.1) proves behavior (`Encrypt_K(P)==C`) and never trusts identity.
Neither axis may be made to depend on process/thread identity.
- PRE-EMPTIVE CLOSURE: Re-research WILL surface `FltGetRequestorProcess(Id)` and
  tempt you to anchor an originator on it. You MUST NOT. Under IRP pending by a
  higher filter, the pre-op callback can run in a SYSTEM-WORKER-THREAD context, so
  requestor identity is fundamentally unreliable for IRP-based operations. This is
  not a bug to work around; it is WHY data-safety and Oracle are identity-
  independent. Anchoring safety on identity would be unsound. Closed. In
  particular, a destruction path that preserves ONLY when the requestor is a
  "tracked" process is a §1.1 hole and an identity-anchoring violation at once;
  preservation is unconditional on D∧T.
- DEPENDS-ON: §1.1, §4.1.

### §5.2  [DECISION] WHITELIST = SATURATION WEIGHT, NOT ORACLE EXEMPTION
A whitelisted identity does NOT skip the D∧T gate, does NOT skip preservation,
and does NOT skip the Oracle. Its ONLY effect is a frequency-axis weighting after
the Oracle acquits (raise WARN threshold / reduce saturation pressure for a
repeatedly-acquitted originator).
- CLOSED CONCLUSION: This removes the payload of confused-deputy/injection. The
  thing an attacker could steal by hijacking a trusted identity shrinks from
  "Oracle exemption" to "saturation weight." Stealing the weight loses no data
  (§2 preserves unconditionally) and misses no encryption (§4 convicts on
  behavior). At worst the saturation signal is slightly delayed while all
  originals remain preserved.
- PRE-EMPTIVE CLOSURE: Re-research into process-injection will pull you toward
  building injection DETECTION (unbacked-memory threads, trampolines, etc.).
  Injection detection is an incomplete arms race and is NOT how this is closed.
  We do not detect injection; we remove its payload. Do not add injection
  detectors as a gate.
- IMPLEMENTATION LATITUDE: weighting function; acquittal-history accounting.
  Identity MAY be used for this NON-CRITICAL role only.
- DEPENDS-ON: §5.1.

### §5.3  [BOUNDARY] legitimate-encryption-tool vs ransomware = intent, unobservable
A genuine encryption tool (e.g. BitLocker, BestCrypt) performing D∧T is provable
encryption by the Oracle; the system CANNOT distinguish "authorized encryption"
from "malicious encryption" at the behavior level. That distinction is intent.
- RATIONALE: Intent is not visible to a minifilter; trying to infer it is slope.
- CLOSED CONCLUSION: This does NOT harm data-safety — the original is preserved
  regardless of intent. User-intended runs are whitelisted (→ §5.2 weight);
  attacker-spawned runs fill saturation and signal independently (§4.5). Either
  way originals survive. Accepted boundary.
- DEPENDS-ON: §5.2, §4.5.

================================================================================
## §6  OBSERVATION-POINT INTEGRITY  (O7: keep trusted code trusted, keep the hook attached)
================================================================================

> §6 is the SELF-PROTECTION axis. Per §0.9 it rides on platform security
> primitives that are a function of the OS generation. Each item below names the
> primitive it requires; where the primitive is absent on a supported OS, the
> boundary that item guards descends per §0.9 and the system records the
> descended posture explicitly. The data-safety axis (§1–§5) does NOT depend on
> any of these and is unaffected. The comm-port authentication and journal
> integrity items (§6.1 L2, §6.5) are exceptions: they ride on surface present
> across the whole range and hold on every supported OS.

### §6.1  [DECISION] AUTHORIZATION INTEGRITY = KEY RESIDENCE, NOT STRONGER MAC (N3)
The weak link in "this code is trusted" state is WHERE the key lives, not the MAC
algorithm. Close the trust regression by moving residence downward:
- **Layer 1:** authoritative authorization/whitelist state lives in the
  minifilter's KERNEL non-paged pool — inaccessible to user-mode (even SYSTEM /
  SeDebugPrivilege) without kernel code execution. Disk journal is demoted to a
  recovery cache. (Requires §6.2 pool secrecy; descends per §0.9 where HVCI absent.)
- **Layer 2:** journal (recovery cache) is sealed with a keyed-MAC (replace
  CRC32). The MAC key NEVER exists in user-mode; MAC generate/verify happen in
  kernel. The user-mode service only REQUESTS bit changes via the filter
  communication port (SD-protected); the kernel decides and MACs. (Rides on CNG
  kernel surface present across the range; holds on every supported OS.)
- **Layer 3:** the kernel MAC key is TPM-sealed under a PCR policy, unsealed ONCE
  at early boot by our boot-start driver via the kernel TBS export-driver path
  (`Tbsip_Submit_Command`), after which the gating PCR is extended so no later
  code (incl. a compromised service) can re-unseal in that boot session.
  `TPM2_PolicyAuthorize` accommodates legitimate PCR changes on update.
  (Requires a TPM-2.0 kernel stack; where absent — no TPM, TPM 1.2, or no kernel
  TPM access at the boot-start phase — Layers 1–2 hold and Layer 3's hardware-root
  closure is NOT claimed on that OS, per §0.9.)
- CLOSED CONCLUSION: Where the platform supplies it, the regression terminates in
  TPM hardware (SRK never leaves the chip). No stronger crypto improves this; the
  fix was residence. Where the platform does not supply it, the regression
  terminates at the highest residence the platform allows, and the system says so
  rather than overclaiming.
- PRE-EMPTIVE CLOSURE: Re-research may propose VBS/VSM key-isolation as a
  "cleaner" primitive than TPM-seal+PCR-extend. It is RECORDED but NOT adopted;
  the TPM pattern is the canonical, BitLocker-proven mechanism and is already
  closed. Substituting it is reopening a closed item (§0.7). Separately,
  re-research will claim TBS is user-mode-only and unreachable from a boot-start
  driver; this is FALSE — `tbs.sys` is a kernel-mode export driver and the kernel
  `Tbsip_Submit_Command` issues IRPs to the TPM driver. The residual is timing,
  not feasibility (the TBS path being ready at the boot-start load phase), which
  is [DEPLOY] §9, not a reopening.
- IMPLEMENTATION LATITUDE: MAC primitive (HMAC-SHA256/KMAC); pool layout; request-
  port protocol. NOT latitude: key never in user-mode; unseal-once; extend-after;
  runtime detection of the TPM stack and explicit non-claim where absent.
- [DEPLOY] §9: PCR index (MUST NOT be 11 — BitLocker-reserved; choose 12/13/15
  SHA-256 and enumerate co-writers via measured-boot log); unseal→extend ATOMIC in
  one kernel routine (no wait/IRP/preemption gap between them); MAC key NOT resident
  in plaintext in pool past use; PolicyAuthorize authority key held in TPM/HSM, new
  policy signing gated out-of-band; TBS-ready-at-boot-start verified empirically.
- DEPENDS-ON: §6.2 (pool secrecy precondition), §9 (TPM, HVCI).

### §6.2  [INVARIANT] KERNEL-POOL SECRECY PRECONDITION
Reading/writing the minifilter's non-paged pool from user-mode requires kernel
code execution (driver load gated by DSE/HVCI/blocklist, or kernel exploit).
SeDebugPrivilege reaches user process space only, not kernel pool;
`\Device\PhysicalMemory` is kernel-restricted.
- CLOSURE SCOPE: This is the bedrock for §6.1 Layer 1. It HOLDS at full strength
  only with HVCI active (→ §9 runtime check). On a supported OS generation
  without HVCI, the precondition is not "weakened" — the boundary it guards
  descends (§0.9): a non-kernel admin may reach the pool that HVCI would have made
  require kernel code. The system MUST detect HVCI at runtime and record this
  descended boundary; it MUST NOT claim full pool secrecy where the platform does
  not provide it, and MUST NOT attempt to synthesize it in software (§0.9
  NEGATIVE). Co-resident SIGNED third-party kernel drivers can read the pool even
  with HVCI → §6.1 secrets must not persist in plaintext past use; this is a
  constraint here, not a reopening.
- DEPENDS-ON: §9 (HVCI runtime-verified).

### §6.3  [DECISION] BYPASS-ALTITUDE = SINGLE-GATE REDUCTION
Every sub-observation-point file-DATA destruction path on a mounted filesystem
reduces to exactly one of two gates:
- **Gate A — kernel code execution:** raw-IRP-to-NTFS (`SL_FORCE_DIRECT_WRITE` is
  kernel-only), lower-altitude/kernel driver, post-lock raw sector write via a
  kernel agent. Defended ABOVE by §9 (HVCI + vulnerable-driver blocklist + ASR +
  signed boot-start ordering). Beyond it → §7.1 BOUNDARY. On an OS generation
  lacking HVCI/blocklist, Gate A widens (§0.9): the privilege required to reach it
  descends; the architecture is unchanged and §1.1 is unaffected.
- **Gate B — mediable FSCTL class:** the non-kernel admin paths
  (dismount-then-raw-write, direct zero/trim, shadow-deletion) ALL surface as
  `IRP_MJ_FILE_SYSTEM_CONTROL` and are mediated per ⟦FSCTL-MEDIATION-COMPLETENESS⟧
  (§3.6 — single definition; referenced, not duplicated). Gate B holds across the
  whole range (it rides on IRP visibility, not on §6 primitives).
- CLOSED CONCLUSION: With Gate B mediated and Gate A defended-above/bounded-below,
  the reduction is sound. The bypass surface is closed up to the §7.1 boundary,
  wherever that boundary sits for the OS generation in play.
- PRE-EMPTIVE CLOSURE: Re-research will (correctly) find that "kernel code
  execution" alone is NOT the single gate — Gate B exists without kernel. That is
  already incorporated: the gate is A ∪ B, and B is §3.6. Finding B does not
  reopen; it confirms this structure.
- DEPENDS-ON: §3.6, §9, §7.1.

### §6.4  [DECISION] ATTACHMENT INTEGRITY (boot-start + policy-forced attach)
The minifilter loads as a boot-start driver (`SERVICE_BOOT_START`) with an
appropriate AM-range ALTITUDE, so it is present before any user-mode code runs.
Attachment is forced by policy on all volumes; where the platform provides Dev
Drive (ReFS), the filter is on the enterprise Dev Drive filter allow-list and
registered to attach to ReFS as well as NTFS; default-instance not manual-attach.
Service runs PPL where the platform supports it; kernel self-protection via
object-callbacks.
- PRE-EMPTIVE CLOSURE: "ELAM-class early attach" is OVER-SPECIFIED. ELAM gives the
  AM driver first-load + the right to classify subsequent boot drivers; it does
  NOT give an arbitrary minifilter a deterministic ordinal. The needed property
  (load before user-mode malware) comes from BOOT-START alone. Do not chase a
  stronger ordering guarantee; precise inter-boot-start order is [DEPLOY]
  empirical per build (§9). A co-shipped ELAM driver may attest us Good; optional
  except where required to obtain PPL (which itself exists only on supported OS
  generations — absence descends the service self-protection, §0.9).
- CLOSURE SCOPE: Dev Drive exists only on its OS generation (§9); where it is
  absent there is nothing to attach to and the allow-list logic is inert (§0.10),
  losing nothing. Where present, the allow-list is ADDITIVE — if a managed Dev
  Drive defines a list and we are absent, we do not attach; hence the deploy
  requirement.
- IMPLEMENTATION LATITUDE: altitude value within AM range; LoadOrderGroup; self-
  protection mechanics; runtime detection of Dev Drive / PPL availability.
- DEPENDS-ON: §9.

### §6.5  [DECISION] COMM-PORT CLIENT AUTHENTICATION
The filter communication port is the only channel by which user-mode mutates
trusted state (config, trusted-PID set, confirmed/restore signals). The kernel
MUST authenticate the connecting client in the connect callback — verify the
caller's protection/signing level and expected service identity, not merely accept
the first connector — and the port security descriptor MUST restrict access to
that identity. Accepting the first connector and granting it gate-exempt status is
a trust-bootstrap hole that voids §6.1 from outside.
- CLOSED CONCLUSION: This rides on comm-port + process-protection query surface
  present across the supported range; it holds on every supported OS. Where PPL is
  available the service runs protected and the check is strongest; where PPL is
  absent the check uses the strongest signing/identity evidence the platform
  offers, and the descended assurance is recorded per §0.9.
- IMPLEMENTATION LATITUDE: SD construction; which protection/signing predicate;
  re-auth on reconnect.
- DEPENDS-ON: §6.4, §9.

================================================================================
## §7  BOUNDARIES  (declared non-defenses — DO NOT cross)
================================================================================

### §7.1  [BOUNDARY] Kernel-privileged adversary
An attacker executing arbitrary kernel code is at our privilege level: it can
bypass our callbacks, read our pool, skip our holds. A user-mode service +
minifilter architecture STRUCTURALLY cannot defend against an equal/higher-
privilege kernel attacker.
- RATIONALE: This is the architectural ceiling of ALL user-mode/minifilter
  defenses, not a defect of ours. Defending past it is impossible-by-construction;
  attempting it is slope.
- CLOSURE SCOPE: §6.1 (N3), §5 (injection), §6.3 (bypass) ALL converge here:
  "kernel code execution = game over" is one boundary, reached from three sides.
  Confirmed boundary = closure (§0.3). On an OS generation lacking the §6
  primitives (HVCI/TPM/PPL), this boundary DESCENDS to a lower privilege (§0.9):
  the privilege an attacker needs to reach "game over" is lower, because the
  platform supplies less to raise it. This descent is neither a defect of this
  system nor of that OS — it is the security capability of that OS generation, and
  it is recorded explicitly, never silently. It does not move §1.1.
- DATA NOTE: preservation before the compromise instant is retained; the boundary
  is loss of NEW observation after compromise, not retroactive destruction of
  existing shadows (a kernel attacker directly destroying the shadow store is
  inside this boundary). This DATA NOTE holds identically wherever the boundary
  sits: even at the descended (non-HVCI) boundary, originals preserved before
  compromise survive — the cost of a missing platform primitive is earlier loss of
  FUTURE observation, never loss of ALREADY-preserved data.

### §7.2  [BOUNDARY] Partition-table / out-of-volume corruption
An admin (non-kernel) can corrupt MBR/GPT or out-of-volume sectors. This makes a
volume unaddressable at next boot; it does NOT destroy file DATA (recoverable via
partition repair).
- RATIONALE: Our property is "preserve original file bytes." Unaddressability is
  not byte-destruction; defending partition geometry is a different product.
- CLOSURE SCOPE: Recovery-after-partition-corruption is out of scope (operational).

### §7.3  [BOUNDARY] Hardware/firmware TPM attacks
faulTPM-class fault injection, LPC/SPI bus sniffing, fTPM weaknesses.
- RATIONALE: Same converged ceiling as §7.1 (physical/firmware ≥ our level).
- [DEPLOY] §9 hardening options (dTPM over fTPM, TPM-bound PIN, encrypted bus
  sessions) MAY raise cost but do not move the boundary. On OS generations without
  a usable TPM stack this boundary is not reached because Layer 3 is not claimed
  there (§6.1) — the residence regression simply terminates higher (§0.9).

### §7.4  [BOUNDARY] Wiper (plaintext-over-plaintext)
Plaintext-over-plaintext (structured→structured) SKIPs by explicit choice; a
wiper is not a detection target.
- RATIONALE: Excluding wiper would need a "plaintext-over = ignore" branch, but a
  wiper is content-indistinguishable from DISGUISED encryption — that branch would
  open a hole. Disguised encryption still fails the conditional test
  `C(new|old)≈|new|` → PRESERVE. A true wiper has no key → not viable ransomware.
- DEPENDS-ON: §3.2.1.

================================================================================
## §8  NEGATIVE CONSTRAINTS  (MUST NOT)
================================================================================

- [NEGATIVE] Do NOT add any blocking layer to the trigger (§2.2). Blocking lives
  downstream with its own abstain guarantees.
  RATIONALE: a blocking trigger converts over-preservation into false blocks.
- [NEGATIVE] Do NOT anchor data-safety or the Oracle on process/thread identity
  (§5.1). RATIONALE: requestor context is unreliable under IRP pending.
- [NEGATIVE] Do NOT cross-subsidize the two axes (§0.5), and do NOT cross-subsidize
  between the data-safety axis and the §6 self-protection posture (§0.9): a weak
  self-protection posture on a low-primitive OS does NOT weaken §1.1, and §1.1
  does NOT excuse overclaiming §6.
- [NEGATIVE] Do NOT default-open a newly-found gap (§0.6). Classify first; default
  is CLOSED.
- [NEGATIVE] Do NOT reopen a CLOSED CONCLUSION on the basis of a re-research
  finding already named in its PRE-EMPTIVE CLOSURE (§0.7).
- [NEGATIVE] Do NOT add defenses past any §7 BOUNDARY.
- [NEGATIVE] Do NOT reintroduce rejected ideas: canaries; key-less aggregate-scale
  auto-block (`STATUS_ACCESS_DENIED` on raw shadow-bytes %); entropy-only trigger;
  ML classifier AS THE GATE; cold-key delete-time re-test sold as a safety net;
  always-preserve T-gate stub sold as "safe over-preservation" (§3.2.1).
  RATIONALE: each was closed-against; re-adding reopens settled ground.
- [NEGATIVE] Do NOT drag minifilter-bypass / raw-kernel into a non-§6.3/§7.1
  framing, and do NOT produce attack PoCs (mmap-encryptor, block-clone destroyer,
  step-by-step bypass). Output is hook placement, never an attack artifact.
- [NEGATIVE] Do NOT treat existing code as authoritative over this document
  (§0 header). Design governs code.
- [NEGATIVE] Do NOT skip `IRP_MJ_FILE_SYSTEM_CONTROL` registration or the
  ⟦FSCTL-MEDIATION-COMPLETENESS⟧ set (§3.6); `IRP_MJ_WRITE` alone is insufficient.
- [NEGATIVE] Do NOT copy "skip cached/paging IO" registration patterns from
  encryption-filter samples; default registration must see paging + cached IO
  (else write visibility shrinks).
- [NEGATIVE] Do NOT raise the OS floor to avoid a down-level branch, and do NOT add
  a down-level branch where none is needed (§0.10). Compatibility is achieved by
  one binary with runtime detection only where a routine would otherwise fail to
  load/function; fallback-for-its-own-sake is the dead code this project forbids.
- [NEGATIVE] Do NOT frame a missing platform primitive as a defect of this system
  or of the OS, and do NOT synthesize a missing hardware guarantee in software
  (§0.9). Detect, scope honestly, preserve regardless.

================================================================================
## §9  DEPLOYMENT PRECONDITIONS  ([DEPLOY] — closure is verification/config, not research)
================================================================================

> §9 preconditions are layered by axis (§0.9). The DATA-SAFETY preconditions hold
> across the whole minifilter-capable range and are mandatory everywhere. The
> SELF-PROTECTION preconditions are a function of OS generation: where present
> they are verified/configured; where absent the dependent §6 layer descends
> (§0.9) and the descended posture is recorded — it is never silently assumed.

DATA-SAFETY (every supported OS):
- [DEPLOY] CoSC writable-section-callback coverage instrumented at deploy (§3.3
  residual): confirm no writable section path skips
  `ACQUIRE_FOR_SECTION_SYNCHRONIZATION` on target builds.
- [DEPLOY] ReFS (where present): confirm logical pre-image read returns
  pre-modification data; integrity-stream checksum-error on read → fail-safe to
  preserve-HOLD (§3.5) — note hold ≠ preserve when the original is itself
  unreadable: the destroying op is denied, but unreadable bytes cannot be copied,
  and that is the correct fail-safe; confirm mmap flush surfaces as paging-write.
- [DEPLOY] BypassIO surface tracked each release (§3.7); offload mediated in the
  FSCTL handler, registry SupportedFeatures reflecting actual behavior (§3.7).

SELF-PROTECTION (per OS generation; descends where absent, §0.9):
- [DEPLOY] HVCI / memory-integrity — runtime-verified (kernel
  `SYSTEM_CODEINTEGRITY_INFORMATION` flag; user-side
  `Win32_DeviceGuard.SecurityServicesRunning`). Precondition for §6.2 full pool
  secrecy and §6.3 Gate A. Where absent → record descended §7.1 boundary and warn;
  data-safety unaffected. (Win10 1607 / Server 2016+.)
- [DEPLOY] Microsoft Vulnerable Driver Blocklist + ASR "block abuse of vulnerable
  signed drivers" — where available, narrows §6.3 Gate A (BYOVD). Blocklist is
  periodic/non-exhaustive; residual BYOVD stays inside §7.1.
- [DEPLOY] TPM 2.0 + kernel TBS path — runtime-detected. Required for §6.1 Layer 3.
  Where absent (no TPM / TPM 1.2 / no kernel TPM access at boot-start), Layers 1–2
  hold and Layer 3's hardware-root closure is NOT claimed (§6.1). dTPM preferred
  over fTPM (§7.3). PCR index ≠ 11; 12/13/15 SHA-256 bank; enumerate co-writers via
  measured-boot log; unseal→PCR-extend ATOMIC in one early-boot kernel routine
  (no PASSIVE-level preemption gap); TBS-ready-at-boot-start verified empirically.
- [DEPLOY] Dev Drive (where the platform provides it, Win11 22H2 / Server 2025+):
  our filter on the enterprise filter allow-list; "let antivirus filter protect
  Dev Drives" policy on; ReFS attachment registered. Where absent, inert (§6.4).
- [DEPLOY] PPL anti-malware (where the platform provides it, Win8.1+): service
  page-hash signed; loaded non-Windows DLLs signed with the same cert; cert hash
  in a co-shipped ELAM driver registered via `InstallELAMCertificateInfo`. Where
  absent, service self-protection descends (§6.4/§6.5) and is recorded.

LOAD / SIGNING / BUILD (the real driver of distribution variants, §0.10):
- [DEPLOY] Driver linked with `/integritycheck`
  (`IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY`) — MANDATORY at every floor:
  without it `PsSetCreateProcessNotifyRoutineEx` returns STATUS_ACCESS_DENIED and
  the §5 originator/identity subsystem silently never activates; DriverEntry MUST
  treat that registration failure as fatal-to-feature, not swallow it.
- [DEPLOY] Boot-start signature EMBEDDED in the `.sys` (not catalog-only), since
  load is boot-start (§6.4). One signature does not span the entire historical
  range: cross-signing loads down-level but is being de-trusted on newest Windows;
  attestation/Dev-Portal loads Win10 1607+ but attestation is desktop-only and
  omits the ELAM level. The single sanctioned way to span broadly is WHQL/WHCP
  submission with down-level catalogs + dual signatures; a co-shipped ELAM driver
  (for PPL) goes through WHQL, not attestation. CODE remains one binary per
  architecture (§0.10); SIGNING, not code, is what forces multiple distribution
  variants if the floor is set below 1607.
- [DEPLOY] Inter-boot-start load order is empirical per Windows build (§6.4);
  do not assert a stronger guarantee.

COMPATIBILITY FLOOR (decided, recorded, not silently assumed):
- [DEPLOY] The supported floor is a deployment decision recorded here, not a code
  assumption. The DATA-SAFETY axis is deliverable, via §0.10 runtime detection,
  down to the FltMgr/CNG/process-notify-Ex DDI floor (Windows Vista SP1 in
  theory). The §6 SELF-PROTECTION model is intact only from the OS generation that
  supplies its primitives (HVCI/attestation-signing/Ex-info-classes: Win10 1607 /
  Server 2016). The recommended production floor is Win10 1607 / Server 2016 —
  the lowest point where the §6 model holds AND a single attestation/WHCP-signed
  binary suffices (no cross-sign split). Targeting below 1607 is supported for the
  data-safety axis explicitly WITHOUT the §6 guarantees and may require a separate
  signed distribution variant; this is recorded per §0.9, never silently degraded.

================================================================================
## §10  CONFORMANCE CHECKLIST  (an implementation is constitutional iff ALL hold)
================================================================================

DATA-SAFETY (§1.1):
- [ ] For every §3.1 member (legacy AND Ex info-classes) + every
      ⟦FSCTL-MEDIATION-COMPLETENESS⟧ code, a pre-destruction preserve point exists.
- [ ] Preserve completes and is verified BEFORE Oracle and BEFORE any memory scan
      (§2.1); IRP released before scan; notification does not block the IRP on a
      user-mode round-trip (§4.3).
- [ ] Every preserve-failure path holds/denies; no unpreserved pass-through (§2.3);
      capture read does not fail-open on un-aligned offsets (§2.3).
- [ ] Preservation is unconditional on D∧T, never gated on requestor identity
      "tracked" status (§5.1).
- [ ] Shadow artifacts uniquely named; no overwrite-if-exists clobber (§2.4).
- [ ] Trigger never blocks (§2.2).
- [ ] T-gate is a real conditional-transition measurement, not always-preserve
      (§3.2.1).
- [ ] CoSC actually preserves bytes at section-create (not just flags), justified
      by "capture precedes modification" (§3.3).
- [ ] rename/hardlink-replace preserve the VICTIM and journal the VICTIM path
      (§3.4).
- [ ] Destruction resolved by post-op ground truth (§3.5).

CONVICTION (§1.2):
- [ ] Oracle at WRITE IRP (§4.2); register-first, scan deferred post-release,
      cipher battery offline (§4.3); no synchronous user-mode Oracle in the pre-op.
- [ ] Round-key inversion implemented (§4.4); no dependence on a single p; no
      cross-axis subsidy (§0.5).
- [ ] Saturation is evidence/weight only; convicted-transition originates only
      from the Oracle, never key-less occupancy (§4.5, §8).

IDENTITY (§5):
- [ ] Data-safety and Oracle nowhere depend on requestor identity (§5.1).
- [ ] Whitelist affects only saturation weight, never gate/preserve/Oracle (§5.2).
- [ ] No injection-detector used as a gate (§5.2).

OBSERVATION-POINT (§6):
- [ ] Comm-port authenticates the connecting client; first-connector is not
      auto-trusted (§6.5).
- [ ] Authorization state authoritative in kernel pool; MAC key never in
      user-mode; journal keyed-MAC computed/verified in kernel (§6.1 L1–L2).
- [ ] Where the TPM stack is present: TPM-seal + unseal-once + PCR-extend via
      kernel TBS; where absent: Layer 3 not claimed, descended posture recorded
      (§6.1 L3, §0.9).
- [ ] `IRP_MJ_FILE_SYSTEM_CONTROL` registered; full §3.6 set mediated (§6.3 Gate B).
- [ ] Boot-start + forced attachment incl. ReFS/Dev Drive where present (§6.4).

COMPATIBILITY (§0.9 / §0.10):
- [ ] One binary per architecture; down-level branches ONLY where a routine would
      otherwise fail to load/function; no fallback-for-its-own-sake (§0.10).
- [ ] Each §6 primitive runtime-detected; absence descends the boundary per §0.9
      and is recorded EXPLICITLY, never silently weakened or framed as a defect.
- [ ] §1.1 verified to hold on the lowest supported OS independent of §6 state.

BOUNDARIES & NEGATIVES:
- [ ] No defense added past any §7 boundary (at whatever privilege it sits for the
      OS generation in play).
- [ ] No §8 NEGATIVE violated.

DEPLOY (§9):
- [ ] DATA-SAFETY [DEPLOY] preconditions verified on every supported OS.
- [ ] SELF-PROTECTION [DEPLOY] preconditions verified where present; descended
      posture recorded where absent.
- [ ] `/integritycheck` linked; boot-start signature embedded; signing/floor
      decision recorded (§9).

PROCESS:
- [ ] No CLOSED CONCLUSION reopened via a finding already in its PRE-EMPTIVE
      CLOSURE (§0.7).
- [ ] Every newly-found gap classified per §0.6 before any escalation.