# semantics-ar — Constitution, PART XII — PROJECTION AMENDMENTS (the itemized-plane evidence contract)

**STATUS: DRAFTED — pending operator ratification and VM verification.** This document is a
proposed addition to `CONSTITUTION.md`. It is written in that document's normative discipline
(Part 0.2 item and epistemic tags) and is subordinate to it: where this draft and the ratified
Constitution disagree, the Constitution is right until this draft is ratified into it. It exists
because the frontend's normal-day flow (Charter Part IX; FRONTEND_DESIGN Parts IX, X, XII) needs
evidence the running backend already holds but does not *project*, and the closure discipline
(Const. III.1.3 / Charter III.4) forbids inventing that projection in the UI. Each amendment below
is the minimal projection — or the minimal forward-accruing capture — that serves a stated
frontend obligation, and nothing more.

Ratification closes on: (a) the operator enabling the `SarTarget` VM and approving the backend
redeploy; (b) VM verification of each amendment against the running backend (FRONTEND_DESIGN
XV.1); (c) merge of these clauses into `CONSTITUTION.md`.

---

## XII.0 — WHY THIS PART EXISTS, AND ITS SCOPE BOUNDARY

The itemized (elevated) plane already carries the recoverable catalog and the preserve list
(Const. III, Charter VII.3.1). It does not carry three classes of evidence the operator's
resource-hygiene and exemption decisions require:

1. **Which pool a held original sits in** — probation vs protected (Const. III.3, III.4).
2. **Which actor a held original belongs to** — for incident grouping consistent with the catalog.
3. **Which application caused a held original** — durable image identity, for budget attribution
   and for quantifying an exemption's cost.

> **[NEGATIVE] XII.0.1 — These amendments touch the itemized plane only; the posture plane is not
> widened.** The posture frame (`sarapi_posture_t`, the 40-byte no-path DTO proven at the `sarapi`
> boundary, FRONTEND_DESIGN II.2) gains no path-capable and no free-text field from this Part. The
> one posture-plane addition (XII.3) is a single enum/flag, not a string or path. All attribution
> and identity evidence lives behind fresh elevation, because per-app preservation status is
> defensive-posture intelligence (Charter IX.4): the intelligence an attacker wants before
> encrypting is exactly what elevation gates.

> **[BOUNDARY] XII.0.2 — Attribution is aggregate evidence, never a surveillance ledger.**
> Recording the causing application on a held original is evidence metadata about a *copy the
> system already holds*; it is not new observation of the machine, and it inherits the retention
> bounds of the copy it describes (Charter IX.4). Preservation remains identity-independent at
> *capture* (Const. VI); attribution is read *off* the copy after the fact, on the elevated plane.
> The attribution view shows app × coarse file-class × aggregate impact — never a per-file path
> list (Charter IX.4). This Part adds no per-write requestor identity to the capture decision
> (Charter I.8).

### XII.0.3 — Verified state of the running backend (Beat, 2026-07-10) [MEASURED]

The following were read from source in this repository and are the factual basis for the
amendments. They are recorded so the amendments are not written against an assumed backend.

- `sar_preserve_record_t` (`common/include/semantics_ar/preserve_format.h`) **already carries**
  `state` (`SAR_PRESERVE_PROBATION=0` / `SAR_PRESERVE_PROTECTED=1`) and `actor_id` (uint64).
- The driver populates the preserve `actor_id` with **`PsGetProcessId`** (`driver/capture.c:376`,
  `:443`, `:1036`, `:1190`) — a **PID**, reused across process lifetimes — whereas the keystore
  (catalog) actor is **`PsGetProcessStartKey`** (`driver/capture.c:559`), the durable OS start key.
  **The preserve actor binding is therefore not the catalog's actor key**, and grouping BOUNDED
  holds by the current `actor_id` would both fail to join the catalog's incidents and admit
  PID-reuse collisions. This is a correctness defect the amendment corrects, not merely a missing
  projection.
- Both projection layers drop `state` and the actor binding: the driver→service reply entry
  `semantics_ar_preserve_entry_t` (`common/include/semantics_ar/protocol.h`) carries only
  provenance path/offset/length, capture_time, and payload_length; and the service→app entry
  `sarapi_preserve_entry_t` (`frontend/sarapi/include/sarapi.h`) carries provenance path, offset,
  length, capture_time, size. Neither carries pool, actor, or causing app.
- The current `sarapi_preserve_entry_t` is 552 bytes (`provenance_path[260]×2 + 4×uint64`), pinned
  by `RecoveryLadder.PreserveEntrySize=552` and `InteropLayoutTests`. Any field addition updates
  that constant and its assertion in lockstep (FRONTEND_DESIGN I.3 / HANDOFF binding rule 3).
- No durable causing-application identity is recorded on a preserve hold anywhere in the backend.

---

## XII.1 — [DECISION] Per-item pool status on the projected preserve entry

**Obligation served:** Charter IV.1.2 / I.4a, FRONTEND_DESIGN VI.2 — a BOUNDED item must be shown
honestly by pool: a **protected** hold as a firm date (*"recoverable until <date>"*), a
**probation** hold as *"held — up to <date>, may yield sooner under storage pressure."*

**Amendment.** Project the existing `sar_preserve_record_t.state` through both layers, unchanged in
meaning:
- add `uint32_t state` to `semantics_ar_preserve_entry_t` (driver→service reply) and to
  `sarapi_preserve_entry_t` (service→app), populated by copy at each projection point;
- carry the two ratified values only (`SAR_PRESERVE_PROBATION` / `SAR_PRESERVE_PROTECTED`); the app
  renders probation as the softer promise and never gives a probation hold a protected hold's firm
  date.

> **[NEGATIVE] XII.1.1** The pool is a retention fact, never a detection or conviction signal shown
> to the user (Charter I.4a). The UI never labels a probation hold as "suspected" or a protected
> hold as "convicted"; promotion is surfaced, if at all, only as the retention promise it changes.

**Nature:** cheap — the value exists; this is projection, not new capture. **Until it lands, BOUNDED
items degrade to the conservative probation-honest framing** (FRONTEND_DESIGN XII.1); the app never
shows a firm protected date it cannot substantiate.

---

## XII.2 — [DECISION] Per-copy causing-application identity on the preserve hold (forward-accruing)

**Obligation served:** Charter Part IX (budget attribution — "where is my recovery window going")
and Charter X.1 (the exemption's *quantified cost*: "currently protecting N files (S bytes) from
this app"). Both need to attribute a held copy to the application that caused it, **durably**, so
the attribution is legible weeks later when the causing process is long gone.

> **[INVARIANT] XII.2.1 — Attribution is a durable image identity, not an ephemeral instance key.**
> A preserve hold records a reference to the causing process's **image identity** — the same
> identity axis the exemption anchor uses (content hash ∧ certificate subject, Const. VI.2.3, X.3)
> — because the process start key and the PID both cease to resolve to an application once the
> process exits. The reference is written when the hold is staged and is never rewritten (it is
> evidence about that copy, first-write-wins in spirit with Const. III.1.3).

> **[DECISION] XII.2.2 — Resolution is off the write path, keyed to what the driver cheaply holds.**
> Computing a content hash and signer subject synchronously in the preserve pre-op at PASSIVE_LEVEL
> on the I/O path is forbidden by the same reasoning that snapshots the Oracle off the write path
> (Const. II.2.1). The driver records, on the hold, the cheap durable facts it already has at
> capture — the causing process's start key and its image file path — and the **service** resolves
> the durable identity (content hash ∧ signer subject) asynchronously through the existing
> `ResolveIdentity` machinery, binding it as attribution metadata. The attribution view groups by
> the resolved identity and shows the unresolved remainder as an explicit "older activity (before
> attribution)" bucket (FRONTEND_DESIGN IX.6).

> **[DESIGN] XII.2.3 — Latitude on the on-disk shape.** The implementation MAY store the resolved
> identity inline on each hold, or store a compact `app_identity_id` (uint64) on each hold that
> indexes a separately enumerable, deduplicated identity table (the recommended shape — it keeps
> the per-entry wire small, matches the aggregate read pattern, and avoids repeating a 256-wide
> subject on every entry). Either is constitutional; the wire projection carries whichever the
> chosen shape dictates, and the size assertion is updated in lockstep. What is **not** latitude:
> the identity axis is hash ∧ subject (never signer-only), and resolution is off the write path.

**Nature:** genuinely new **and forward-accruing** — this value cannot be back-derived for holds
captured before it ships, so **every week without it is a week of unattributed data at launch**.
Per FRONTEND_DESIGN XII.2 / XV.1 this is the **highest-priority, file-first** amendment: the backend
field ships before its consumer, so accrual begins immediately.

> **[NEGATIVE] XII.2.4** Attribution never re-opens the capture decision. It does not make
> preservation identity-dependent (Const. VI), does not exempt or convict, and is not shown on the
> posture plane. An exempt identity is monitored zero (Const. VI.1): it produces no holds, hence no
> attribution.

---

## XII.3 — [DECISION] Integrity-halt signal on the posture frame

**Obligation served:** Charter III.2a / FRONTEND_DESIGN V.3 — a keystore/preserve-store rollback,
tamper, or erasure (Const. VII.1.3–VII.1.4) is the one condition that is **red and delivered by a
foreground window**, never toast-only. The posture frame today carries mode, health, counts,
capacity, descents — but no dedicated VII.1.4 halt signal.

**Amendment.** Add a single flag to the posture frame (`sarapi_posture_t`) reporting that the
driver raised its tamper flag on load-time MAC/anchor verification (Const. II.4.1, VII.1.3). It is
an enum/boolean, path-free and text-free (honoring XII.0.1). The app maps it to the red +
foreground-window tier and to a persistent tray state (FRONTEND_DESIGN V.3); it is the only posture
condition that foregrounds a window.

> **[NEGATIVE] XII.3.1** The halt signal reports *that* integrity verification failed, not *what*
> the tampered content was. No store bytes, key material, or preserved plaintext accompany it
> (Charter I.12).

**Nature:** small posture addition. Confirm whether the service already exposes an equivalent flag
before adding; the frontend integrity-halt tier is otherwise unreachable and stays gated on this.

---

## XII.4 — [DECISION] Correct and project the preserve actor binding as the process start key

**Obligation served:** FRONTEND_DESIGN VIII.1 — BOUNDED holds group into incidents alongside
DEFINITIVE captures, which are grouped by `actor_start_key`. Grouping requires the *same* durable
key on both sides.

> **[INVARIANT] XII.4.1 — The preserve actor binding is the OS start key, not the PID.** The driver
> records the causing process's start key (`PsGetProcessStartKey`) on a preserve hold, replacing the
> current PID (`PsGetProcessId`). The PID is reused across process lifetimes and cannot durably or
> uniquely identify an actor; the start key is the same durable, PID-reuse-proof identity the
> catalog and the conviction/promotion path already use (Const. VI.2.3). Promotion-by-actor
> (`SarPreservePromote`) is evaluated against the start key so that a conviction promotes exactly
> the convicted actor's holds.

**Amendment.** (a) Populate the preserve actor binding from `PsGetProcessStartKey` at **every** stage
origin — verified as seven in `driver/capture.c` (lines 376, 443, 809→`SarPreserveStageLink`, 1030,
1262, 1888, and the mapped-section replay at 1856) plus the promotion site (336, `SarPreservePromote`)
— switched **together**, so promotion (`sar_preserve_promote` matches `actor_id == key`) stays
consistent; (b) project it as `uint64_t actor_start_key` through `semantics_ar_preserve_entry_t` and
`sarapi_preserve_entry_t`; (c) the app groups BOUNDED holds by it and joins them to catalog incidents
(FRONTEND_DESIGN VIII.1).

> **[DECISION] XII.4.2 — The mapped-section seam captures the start key at arm time.** The paging
> write-back site (`SarMmapOnPagingWrite`→`SarMmapCaptureInline`) has no live `PEPROCESS`; it replays a
> value stored at section-arm (`SarMmapArm`). Today it stores the arming **PID** (`mmap_arm_pid`) and
> uses that same value *both* as the owner key *and* as a PID for a `PsLookupProcessByProcessId` on the
> Oracle-submit path (`capture.c:1807`). The two uses are decoupled: the arming process's **start key**
> is resolved once at arm (the process is live at section-create) and stored alongside the PID; the
> start key becomes the owner binding, the PID remains for the internal lookup. A single call-site swap
> here is insufficient and would break the mapped-section Oracle submit — this is why XII.4 is a
> correctness change, not a projection.

**Nature:** small but a **correctness change**, not a pure projection (XII.0.3). **Until it lands,
BOUNDED holds degrade to a flat, ladder-ranked list** (FRONTEND_DESIGN VIII.1, Charter VI.1.1); no
fabricated incident grouping is shown.

> **[DECISION] XII.7 — An ABI/wire change bumps the honest skew guard, not a migration path.** Because
> `sarapi_preserve_entry_t` grows (552→576) and the pipe entry changes, `SARAPI_ABI_VERSION` and the
> `SemanticsAr.Core` `ExpectedAbiVersion` (checked at `NativePostureReader`), and the service↔driver
> `protocol_version`, are bumped in lockstep. This is the FRONTEND_DESIGN VII.6.1 *degrade-to-honesty*
> guard (a mismatched pairing is refused), **not** backward-compatibility or migration code, which the
> project's never-shipped discipline forbids (HANDOFF binding rule 2). Correspondingly, a pre-existing
> on-disk preserve store of the old record size fails the size check on load and is reset to empty
> (`driver/preserve.c:565` — no tamper flag, no crash); this is the correct never-shipped behavior, and
> a versioned-magic on-disk migration is **rejected** as forbidden compatibility code. Redeploy runs
> against a clean store (the sealed-VM baseline).

---

## XII.5 — [DECISION] Enumerate exemption records over the elevated channel

**Obligation served:** Charter VI.3 / FRONTEND_DESIGN Part X — the Exemptions & trust surface (the
canonical home of the disarm verb) must list current exemptions, show the exemption log, and drive
the staleness re-affirm and changed-signer flows. The three exemption verbs already exist on
`ISarElevatedControl` (`ResolveIdentity`, `WhitelistAdd`, `WhitelistRemove`) but are not on
`IElevatedControlChannel`, and there is no *enumerate* verb.

**Amendment.** (a) Add an elevated **enumerate-exemptions** verb returning, per record, the bound
identity (image path, signer subject, content hash, first-seen time) and its live match state
(matching / lapsed-same-signer / lapsed-changed-signer); (b) surface the three existing verbs
through `IElevatedControlChannel` **together with** this consumer, so they are not added as dead
code (HANDOFF binding rule 2; FRONTEND_DESIGN XIII.5).

> **[NEGATIVE] XII.5.1 — Enumeration does not relax the enforced anchor.** The list is decision
> evidence for a human; it never auto-approves. The enforced match key stays content-hash ∧ signer
> subject (Const. VI.2.3, FRONTEND_DESIGN X.3); a lapsed exemption is reported as lapsed (the app is
> re-monitored and therefore *more* protected — never a Home posture alarm, FRONTEND_DESIGN V.4,
> X.4), and interpreters are never enumerable as exemptable (Const. VI.2.4, FRONTEND_DESIGN X.5).

**Nature:** one new elevated verb + a channel-surface of three existing verbs. The quantified
exemption cost (Charter X.1) is computed client-side by joining this list with the XII.2 attribution
totals; until XII.2 accrues, the cost line shows the honest "not yet attributed" state, never a
fabricated number.

---

## XII.6 — CONFORMANCE (this Part is satisfied iff)

- [ ] The posture plane gains no path-capable and no free-text field; only XII.3's flag is added,
      and it is enum/boolean (XII.0.1).
- [ ] Pool `state` is projected with its two ratified values and rendered honestly by pool; no firm
      protected date is shown for a probation hold (XII.1).
- [ ] Causing-application identity is a durable hash ∧ subject reference, resolved off the write
      path, forward-accruing, with an explicit unattributed bucket; it never re-opens capture or
      touches the posture plane (XII.2).
- [ ] The preserve actor binding is the start key, not the PID, everywhere it is recorded, promoted,
      and projected (XII.4).
- [ ] Exemption enumeration reports match state without relaxing the enforced hash ∧ subject anchor;
      interpreters are never exemptable; the three existing verbs reach the channel only alongside a
      live consumer (XII.5).
- [ ] Every wire size change updates its `RecoveryLadder` constant and `InteropLayoutTests`
      assertion in lockstep, and is VM-verified against the running backend before ratification
      (FRONTEND_DESIGN I.3, XIV, XV.1).
