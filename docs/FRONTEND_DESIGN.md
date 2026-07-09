# semantics-ar — FRONTEND DESIGN (the spine)

> **STATUS: RATIFIED (v1.1).** This document fixes the confirmed design direction for the
> T4 operator-surface rebuild under `frontend/`. v1.1 folds in the adjudicated results of an
> external design review (Part 0.5), correcting internal contradictions and filling gaps
> without reopening any ratified architecture decision. It is **subordinate to
> `EXPERIENCE_CHARTER.md`, which is itself subordinate to `CONSTITUTION.md`.** Where this
> document and the Charter disagree, the Charter is right; where either disagrees with the
> Constitution, the Constitution is right.
>
> It is the **closure boundary for the frontend spine** (Charter 0.1): it fixes the small
> set of cross-cutting, load-bearing decisions that a large, long-running, mock-first build
> must not re-open per screen. It deliberately does **not** enumerate every screen. Individual
> screens close by owner feedback on contract-aware mocks and by VM verification against the
> running backend — not by up-front decree.

---

## PART 0 — HOW TO READ THIS DOCUMENT (binding)

### 0.1 What this closes, and what it does not
This document is the ratified answer to "what must every frontend decision obey, and what
are the settled cross-cutting choices." It closes: the trust/transport architecture, the
elevation model, the concrete platform/library/performance decisions, the notification and
posture-color model, the certainty-ladder terminology and claims discipline, the
information architecture, the two hero flows that carry the most judgment (Recovery, and the
normal-day Budget-Attribution/Exemption-Discovery flow), the exemption confirmation and
staleness UX, the canonical failure-state wording, the backend preconditions to file, the
build sequencing, and the definition of done.

It does **not** close: pixel-level visual specification (owned by the design system /
component library it authorizes), per-screen layout beyond the hero flows, or the items in
Part XV that close only by legal review, VM verification, or governed Constitution
amendment.

### 0.2 Item types
Same discipline as the Charter (0.3): **[PRINCIPLE]**, **[DECISION]**, **[PATTERN]**,
**[BOUNDARY]**, **[NEGATIVE]**, **[OPEN]**. A load-bearing claim about the outside world
(Windows/.NET/regulatory behavior) carries **[VERIFIED]** with source and date; a
load-bearing outside-world claim that is not yet independently confirmed is written as
motivation, never as fact.

### 0.3 Provenance of the [VERIFIED] claims
The outside-world claims below were closed by a global research pass (July 2026) across
Microsoft Learn / .NET / Windows App SDK docs, Google Project Zero, peer-reviewed usable-
security work (MIS Quarterly, USENIX), the FTC, and vendor documentation. Each [VERIFIED]
item names its source inline. Re-verify the one moving target (Windows Administrator
Protection) against a current build before shipping any code path that depends on it.

### 0.4 The critical-acceptance record
One expert research recommendation was **rejected** because it contradicted the
Constitution: changing the exemption match key to signer + minimum-version (so app updates
pass silently). It is preserved as a [NEGATIVE] in X.3 with its reasoning, so the rejection
is not silently re-litigated. External expertise informs *how*; it never re-opens a
constitutional *whether*.

### 0.5 v1.1 amendment record (external review, adjudicated)
An external design/architecture review of v1.0 was obtained and critically adjudicated
against the governing documents. Accepted findings were integrated: pipe mutual
authentication (III.7); the stale-exemption / amber-rarity contradiction (V.4, X.4);
honest managed-memory and residual-threat wording (III.4); Windows-honest critical-alert
delivery (V.3); the posture-plane no-free-text rule (II.2); onboarding as a hero flow
(VIII.5); the operational definition-of-done tier (XIV); and several clarifications
(II.2, IX.7, X.5, XV.4) and new [OPEN]s (telemetry XI.2, offboarding XV.6, multi-user
XV.7). **One confident review recommendation was rejected as unconstitutional** — a
"probation-window hybrid" that would auto-renew an exemption on a same-signer update while
continuing to preserve the app's writes. It is rejected on two independent grounds and the
reasoning is recorded in X.6: (a) auto-renewing on same-signer identity *is* the
signer-based matching already rejected in X.3; (b) preserving the writes of an exempted app
violates the absolute "exemption means zero monitoring" contract (Const. VI.1, VII.3.3;
Charter I.6). Only the *spirit* of that recommendation — that the lapsed-and-re-monitored
state is already the safe state and so the re-confirm need not be urgent — was accepted, in
the form of X.4's non-blocking treatment.

---

## PART I — THE ONE IDEA AND THE SPINE

- **[PRINCIPLE] I.1 — The one idea (Charter 0.4).** *The interface makes the certainty of
  protection legible — and never claims more certainty, nor reveals more, than the evidence
  and the Constitution allow.* Every decision below serves this.
- **[PATTERN] I.2 — The certainty ladder is the single most-reused component.** Three rungs —
  **DEFINITIVE** (Oracle key captured; verified reconstruction, unbounded), **BOUNDED**
  (preserved original in a time+capacity window, honest by pool), **UNRECOVERABLE** (neither
  asset holds; stated plainly with reason) — realized as **one** color + icon + label triad
  reused in every list, detail view, incident summary, and the tray glyph. A rung is never
  rendered by color alone.
- **[DECISION] I.3 — One design-token source of truth, consumed by both rendering stacks.**
  The ladder's labels, glosses, colors, icons, and glyph shapes live in a single
  machine-readable token file consumed by **both** the HTML mocks and the WPF resource
  dictionary, so the most-reused (and most drift-prone) asset cannot diverge across the two
  stacks. Drift is made structurally impossible, not review-caught.

---

## PART II — ARCHITECTURE: THE TWO-PLANE TRUST MODEL

- **[PRINCIPLE] II.1 — Two planes, enforced by type, not convention (Charter VII.3.1,
  I.10–I.12).** The app has exactly two data planes:
  - **Posture plane** — session-readable with **no elevation prompt**: overall verdict,
    mode, component health, counts, storage-window health (bucketed), recorded descents, and
    a redacted event stream. Drives Home, the tray, and toasts.
  - **Itemized plane** — served **only** over the elevated control pipe, per fresh elevation:
    the recoverable catalog, preserve list, file paths, provenance, per-item detail, budget
    attribution, and every consequential verb.
- **[NEGATIVE] II.2 — The posture plane's types physically cannot carry a path, and carry no
  free text.** The posture DTOs contain no field capable of holding a file path, provenance,
  hash, key identifier, or decoy identity, **and no free-text/string field at all**: posture-
  plane content is enum codes / template IDs + numeric parameters only, rendered to prose
  client-side — because any general string field is path-capable and would defeat the XIII.4(a)
  schema assertion, which cannot mechanically tell a "message" from a "path." (The current
  `sar_events_frame_t` already complies: it carries only an event-class code, generation/
  sequence/timestamp, and an opaque actor key.) This makes I.11 / I.12 / VII.3.1 violations
  impossible **by construction in `SemanticsAr.Core`, proven at the native `sarapi` boundary by
  the XIII.4(a) artifacts** — the phrasing is scoped to what the type system and the boundary
  test actually guarantee, since `sarapi` is native C the managed type system does not reach.
- **[DECISION] II.3 — Process and assembly topology.**
  - `sarapi` (native C) owns the binary wire for **both** planes, reusing `protocol.h` /
    `posture.h`; the .NET app never hand-marshals struct layouts (Charter VII.2.1).
  - `elevation-host` (elevated COM local server) is the **only** component that speaks the
    elevated control pipe; it is spawned per consented action and exits — never resident.
  - `SemanticsAr.Core` (.NET library) owns the domain (certainty rung, incident, recovery
    outcome), the two channels, the redaction-enforcing DTOs, and version-skew handling.
  - `SemanticsAr.App` (WPF, standard-user) owns the five surfaces, the tray, and the design
    system.
- **[PRINCIPLE] II.4 — Reuse only what genuinely fits.** The technology decisions the Charter
  fixes (WPF/.NET, `sarapi`, COM elevation, three-pipe transport) carry forward because they
  are ratified and already exist in the backend. The existing seams (`PostureService`,
  `ElevatedControlChannel`, `CertaintyChip`, `CertaintyRung`, `TrayIconController`,
  `ThemePalette`, `ProcessHardening`) are the right *shape* but are re-derived against the
  ratified documents, not assumed correct. Placeholder views/viewmodels are discarded.

---

## PART III — ELEVATION AND TRANSPORT

- **[DECISION] III.1 — Elevate per task, not per step (the consent-fatigue resolution).**
  **[VERIFIED]** Microsoft's UAC design guidance: *"Tasks that require administrative
  privileges should be designed to require a single elevation… Once elevated, stay elevated
  until elevated privileges are no longer necessary… Revert to least privileges [when the
  task completes]"* (Microsoft Learn, *User Account Control (Design basics)*, page updated
  2025-07-24; principle current, presentation dated). The confirmed model:
  1. One elevation performs an **itemized read** and returns a snapshot to the standard-user
     app.
  2. The standard-user app browses, filters, sorts, and expands that snapshot **in-process
     with no re-elevation** — browsing already-fetched data is not a privileged operation.
  3. Each **mutating** action (recover, mode change, add/remove exemption, budget change) is
     its own single-elevation task with its own fresh consent.
- **[VERIFIED] III.2 — This model is forward-compatible with Windows Administrator
  Protection (AP).** AP replaces the always-present admin token with just-in-time,
  per-operation elevation and destroys the token at process exit; a long-lived elevated
  token/session is design-incompatible with AP (Microsoft Learn, *Administrator protection*,
  updated 2025-11-20; Windows Developer Blog, 2025-05-19). AP is **not GA / not default** as
  of mid-2026 (shipped 2025-10, reverted, Insider opt-in since 2026-03; MSRC CVE-2026-42829).
  Our per-action model never assumes a persistent admin token, so it needs no change for AP;
  **re-verify against a current Insider build before shipping.**
- **[NEGATIVE] III.3 — No resident elevated broker; no long-lived elevated session
  (Charter VII.4.1).** A persistent elevated helper is hijackable under classic same-desktop
  UAC and breaks under AP's profile separation. The privileged work is done by the SYSTEM
  service behind pipe authorization, not by a surviving elevated user-context helper.
- **[PRINCIPLE] III.4 — The fetched snapshot is sensitive-at-rest in our own process; state the
  honest guarantee and the residual threat.** Holding elevation-fetched itemized data in the
  medium-IL app is standard practice (Task Manager, Resource Monitor do the same). The honest,
  achievable guarantee is: the snapshot lives in managed memory only, its references are scoped
  to the view and dropped (eligible for GC) on window close / session lock, and it is never
  persisted to disk or log, nor placed on the clipboard **except by an explicit user copy action**
  (e.g. copying a recovered file's path). We do **not** claim to zero/scrub it from memory —
  managed strings and object graphs cannot be reliably zeroed (GC copies, LOH), so "cleared" would
  be an overclaim. **Residual threat, stated plainly:** once the snapshot is in the medium-IL
  process, a same-user same-IL process can read it (`PROCESS_VM_READ`) or UIA-scrape the rendered
  window. The elevation gate on itemized reads therefore buys a real, worthwhile property —
  *no silent programmatic harvesting without a visible consent prompt* — but it does **not** buy
  confidentiality against a resident same-session attacker once the operator opens an itemized
  view. II.2's "impossible by construction" applies to the posture channel only.
- **[DECISION] III.5 — Posture-pipe DACL (backend precondition, VII.0 discipline).** The
  session-readable pipe must grant **`FILE_GENERIC_READ` only** to the **per-session Logon
  SID** (re-derived per active session), never `FILE_GENERIC_WRITE` / `FILE_APPEND_DATA`.
  **[VERIFIED]** on a named pipe `FILE_APPEND_DATA == FILE_CREATE_PIPE_INSTANCE`, so granting
  append silently grants the right to create a squatting pipe instance (Microsoft Learn,
  *Named Pipe Security and Access Rights*, updated 2025-04-15). **[VERIFIED]** `INTERACTIVE`
  (`S-1-5-4`) spans all interactive sessions, so under fast user switching it would expose one
  user's posture to a concurrently switched-in user — the per-session **Logon SID** is the
  correct principal (same source). Confirm the current backend uses the Logon SID (X-gate in
  Part XV).
- **[DECISION] III.6 — Authorize consequential verbs by token, never by caller identity.**
  **[VERIFIED]** the service impersonates the client (`ImpersonateNamedPipeClient`) and
  checks for a genuinely elevated token (`TokenElevationType == TokenElevationTypeFull`),
  then reverts — PID / image path are spoofable and are never trusted (Microsoft Learn,
  *Impersonating a Named Pipe Client*; *TOKEN_ELEVATION_TYPE*). Normative home is
  Constitution Part VII / backend design docs.
- **[DECISION] III.7 — Mutual authentication: the client verifies the server, both directions.**
  III.5/III.6 secure server→client authorization; the reverse is equally load-bearing for a
  product whose premise is legible certainty. A medium-IL process that pre-creates (squats) a
  pipe name — after a service crash, or by winning a boot race — could feed the tray/Home a
  **fabricated all-green posture** (a spoofed-reassurance attack). Therefore: (a) before trusting
  either pipe, the client verifies the server process is SYSTEM (`GetNamedPipeServerProcessId`
  + token/SID check, or a server-SID query) and refuses otherwise; (b) the client connects with
  impersonation level restricted to `SECURITY_IDENTIFICATION` or below, so a rogue server cannot
  impersonate the client; (c) both the posture **and** the elevated control pipe are created with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` and treat a pre-existing name as tamper (anti-squat) — the
  III.5 squatting analysis applies to the elevated pipe too, where the outcome is worse.
  Server-side anti-squat is a backend precondition (Charter VII.3.2); the client-side server
  verification is frontend.

---

## PART IV — PLATFORM, LIBRARIES, PERFORMANCE

- **[DECISION] IV.1 — Target .NET 10 (LTS).** **[VERIFIED]** .NET 10 GA 2025-11-11, LTS
  through 2028-11-10; .NET 8 and .NET 9 both reach end of support 2026-11-10 (Microsoft .NET
  devblogs / lifecycle policy). Shipping on .NET 8 in 2026 buys no extra security runway.
- **[DECISION] IV.2 — Fluent via a maintained library; built-in `ThemeMode` is progressive
  enhancement only (closes Charter VII.1.3 [OPEN]).** **[VERIFIED]** WPF's built-in Fluent
  theme is still `[Experimental("WPF0001")]`, Windows-11-first, with no Windows-10 story in
  its own docs (dotnet/wpf `using-fluent.md`; discussion #10387, Oct 2025). The primary skin
  is **WPF UI (`Wpf.Ui`, MIT, v4.3.0 2026-05, targets net10, explicit Windows-10 Acrylic
  fallback, ships Segoe Fluent Icons)**; built-in `ThemeMode` is adopted later only if it
  stabilizes. Budget explicit Windows-10 QA (Windows 10 reached end of support 2025-10-14,
  consumer ESU to 2027).
- **[DECISION] IV.3 — Framework-dependent deployment; runtime as an installer prerequisite
  (confirms Charter X.5).** **[VERIFIED]** WPF is not Native-AOT/trim compatible (dotnet/wpf
  #11205), so a self-contained build carries the full untrimmed runtime for no benefit; the
  shared runtime is centrally serviced by the OS update channel (a runtime CVE is patched
  without redeploying the app). The installer detects and chain-installs the **.NET 10
  Desktop Runtime x64** as a prerequisite (WiX/Burn `PrereqPackage`, same pattern as VC++
  redist).
- **[DECISION] IV.4 — Tray via H.NotifyIcon.** **[VERIFIED]** `H.NotifyIcon.Wpf` v2.4.1
  (2025-12, targets net10) is the maintained successor to the inactive Hardcodet control and
  supports procedural icon generation. Ship 3–4 **distinct-shape** glyphs (not tint-only) with
  light/dark/default variants to avoid the OS contrast-plate treatment. **[VERIFIED]** there is
  no API to force an icon out of the Windows 11 overflow chevron (user drags to pin), so every
  state change is **also** surfaced via toast/banner so an overflowed icon never silently hides
  a state change.
- **[DECISION] IV.5 — Large-list performance (closes Charter X.7).** Use default
  `ListView`/`ListBox` UI virtualization with `VirtualizationMode="Recycling"`. **[VERIFIED]**
  WPF has **no built-in data virtualization** (Microsoft Learn, *Optimize control
  performance*), so the underlying collection for large catalogs is paged/windowed, not
  fully materialized. **[VERIFIED]** a known O(n) UI-Automation peer cost (dotnet/wpf #9181)
  makes very large lists slow **once accessibility tooling is active** — so the largest
  realistic list is explicitly tested with Narrator/UIA on (this is where the accessibility
  floor meets performance). Idle memory/CPU targets are set empirically against a measured
  baseline (low tens of MB working set at true idle is the informal target), not against the
  anecdotal figures found in research.

---

## PART V — NOTIFICATIONS AND POSTURE COLOR

- **[PRINCIPLE] V.1 — Silence by default is the substrate of credibility.** **[VERIFIED]**
  habituation to a repeated security warning sets in after as few as **two** exposures
  (Anderson et al., *MIS Quarterly* 42(2), 2018, fMRI + eye-tracking + field); users click
  through ~70% of SSL warnings (Akhawe & Felt, USENIX 2013). Interruption (modal, red, sound)
  is spent **only** on the action-required / broken tier. If our amber/red states fire for
  more than a small minority of sessions, that is a defect in the underlying certainty model,
  not a UI tuning problem.
- **[PATTERN] V.2 — Tiered notification (Charter III.3).** Housekeeping is silent; a genuine
  detection raises a toast; a decision the system genuinely needs, or an integrity halt,
  raises a foreground window. Probation-pool churn is **silent by design** (Const. III.5.5,
  Charter I.4a) and is never a posture alarm.
- **[DECISION] V.3 — Critical alerts use a Windows-honest foreground mechanism, never toast-only.**
  **[VERIFIED]** notifications for elevated apps are unsupported (Microsoft Learn, *Use app
  notifications with a .NET app*, updated 2026-05-08) — so toasts are raised **only** from the
  standard-user tray process. **[VERIFIED]** `scenario="urgent"` breakthrough requires a
  per-app user opt-in and is still suppressed under manual Do-Not-Disturb (toast schema
  reference) — so an **integrity halt** (Const. VII.1.4) and any must-act decision is delivered
  as a foreground window, not merely a toast, matching Charter III.2a. But the delivery must be
  Windows-honest: a background process **cannot** force `SetForegroundWindow` (the OS demotes it
  to a taskbar flash), and a topmost-without-foreground window can sit behind full-screen
  exclusive apps. The real mechanism is therefore **topmost + taskbar flash + persistent tray
  state + re-assert on the next user activation**, never a claim of an un-dismissable window that
  the platform cannot actually guarantee. It respects full-screen/presentation contexts (queue
  and re-assert rather than forcibly interrupt) — a window that hijacks a presentation is the
  scareware pattern XIV forbids; maximal *attention* is earned without maximal *intrusion*.
  `AppNotificationManager`: call `IsSupported()` before `Register()`, and fall back to an
  in-window banner if it returns false or throws (a real Singleton-package deployment risk).
- **[DECISION] V.4 — Posture color: green / amber / red, with amber reserved for the
  actionable-and-resolvable (closes the AUDIT-amber contradiction).** **[VERIFIED]** Windows
  Security's own model reserves yellow for an actionable recommendation that clears when acted
  on, not for a user's deliberate configuration; a permanently-amber chosen policy is the
  textbook crying-wolf failure (ESET/Microsoft Security-Center docs; alarm-fatigue literature).
  Therefore:
  - **green** = protecting normally (the recovery guarantee is fully intact, including in a
    deliberately-chosen AUDIT).
  - **amber** = a recommendation the operator has **not yet decided about** and can resolve —
    onboarding/unsettled AUDIT ("recorded, not yet blocked — adopt ENFORCE once quiet"), a
    recovery window degraded below target (IX.2 discovery trigger), an actionable platform
    descent, or the one exemption-staleness case that is genuinely a decision: a **changed-signer**
    exemption (possible substitution, X.4).
  - **red** = actually broken or an integrity halt only.
  - A deliberately-chosen, acknowledged AUDIT **collapses to a neutral mode badge**, not a
    standing amber. This refines Charter III.2's illustrative example within frontend latitude;
    it does not contradict it.
  - **Routine (same-signer) exemption staleness is NOT a Home amber.** Because X.3 makes
    exemptions lapse on every app update, and V.1 requires amber to stay rare, surfacing routine
    staleness as posture amber would fire it in a large fraction of sessions and train
    rubber-stamping — defeating the changed-signer moment. Routine staleness is a quiet Policy-
    surface badge/count (X.4), never a posture alarm; the lapsed app is already re-monitored and
    therefore *more* protected, which by V.4's own definition is not an amber condition.

---

## PART VI — CERTAINTY LADDER: TERMINOLOGY AND CLAIMS

- **[DECISION] VI.1 — Formal tier label AND plain-language gloss, together.** **[VERIFIED]**
  the dominant safety-label standard (ANSI Z535) pairs a fixed signal-word panel with a
  plain-language message panel, and the usable-security "jargon-comprehension paradox" is
  resolved by hybrid labeling, not by technical terms alone or colloquial language alone
  (ANSI Z535; systematic review arXiv:2504.02109, 2025; NN/g plain-language guidance). The
  ladder shows a stable formal name (`DEFINITIVE` / `BOUNDED` / `UNRECOVERABLE`, the
  claims-reviewed, translatable unit per Charter IV.3.3) **with** a one-line gloss
  ("verified reconstruction" / "stored copy, time-limited" / "not recoverable"). The gloss
  never overclaims beyond the rung.
- **[PATTERN] VI.2 — Three categorical states, never a percentage.** Recoverability is
  discrete; a false-precision number ("87% recoverable") is forbidden. For BOUNDED, the
  **time limit is made visible** as a date/countdown (bare category labels decay in memory
  faster than a concrete deadline), honest by pool: protected = firm date, probation = "up to
  <date>, may yield sooner under storage pressure."
- **[PRINCIPLE] VI.3 — Absolute words are scoped to the per-file mechanism, never the product
  (claims discipline; direction for Charter X.1).** **[VERIFIED]** the FTC requires a
  reasonable basis before a claim; a disclaimer cannot cure a false headline; a money-back
  guarantee is not substantiation; *FTC v. Tapplock* (2020) is a near-exact precedent for an
  untested absolute security adjective (FTC substantiation & deception policy statements;
  Tapplock settlement). Therefore "verified" / "byte-for-byte" attach only to *a captured
  file's SHA-256 verify-before-replace mechanism*, never to product-level "guaranteed
  recovery." Final legal clearance of the canonical strings is an execution gate (Part XV).

---

## PART VII — INFORMATION ARCHITECTURE (5 surfaces + tray)

The Charter's five surfaces (V.1–V.6) are adopted as-is. This document fixes where the
normal-day budget/attribution work lives (Part IX) and the failure-state wording (Part XI).

- **[DECISION] VII.1 — Home / Shield** — posture-only, no prompt: one glanceable verdict
  (V.4), mode, component health, counts, storage-window health (bucketed), recorded descents,
  the coverage line (one line, expandable — a permanent caveat sentence becomes wallpaper).
  The single legitimate amber for a degraded recovery window is the discovery entry point to
  Part IX.
- **[DECISION] VII.2 — Recovery** — itemized, behind elevation. The hero flow (Part VIII).
- **[DECISION] VII.3 — Activity / Detections** — the redacted event stream on the posture
  plane (no prompt); per-file forensic depth behind elevation. A phantom conviction expands to
  the convicted process's identity and evidence count K only — **never** to decoy identities
  (Charter I.11).
- **[DECISION] VII.4 — Response / Policy** — mode switch with honest consequences and friction
  on adopting ENFORCE (naming all three triggers); **exemption list management** (the
  canonical home for the disarm verb, matching AV/EDR practice of keeping exclusions in a
  policy surface, not general settings), with the staleness re-affirm flow (Part X).
- **[DECISION] VII.5 — Settings** — the budget as a retention policy (time-denominated, IX.2),
  **the budget-attribution view** (Part IX) adjacent to the budget it explains, component /
  version health, recorded descents, and a logs export inheriting every non-disclosure rule.
- **[DECISION] VII.6 — Tray** — posture-only glyph (distinct shapes) + the tiered notification
  layer.
- **[NEGATIVE] VII.7 — No sixth "activity dashboard" hero surface.** A permanently-visible
  per-app activity surface advertises ongoing gardening (contradicting effortlessness) and is
  the surveillance drift the Constitution's invisibility principle forbids. The normal-day work
  is folded in (Part IX), discovered on demand, not standing.

---

## PART VIII — HERO FLOW: RECOVERY

- **[PATTERN] VIII.1 — Incident-organized, ladder-ranked (Charter VI.1).** Recovery is grouped
  by detected destructive incident, anchored to detection time ("restore state as of just
  before T"). DEFINITIVE incidents are grouped by the catalog's `actor_start_key` + anchored by
  `capture_time` (both already projected). BOUNDED holds degrade to a flat, ladder-ranked list
  until preserve entries carry an actor key (Part XII precondition). Until the incident data
  contract lands, the whole surface degrades to a flat ladder-ranked list (Charter VI.1.1).
- **[PATTERN] VIII.2 — Non-destructive by default; the "modified since T" trap is solved, not
  warned.** **[VERIFIED]** the strongest reference pattern is Veeam's restore-changed-files
  behavior: restore a changed file **alongside** the current one with a `_RESTORED_<timestamp>`
  suffix (keep both) rather than forcing a destructive choice; restoring to a side location
  converts a destructive action into an additive one (Veeam Agent docs; Backblaze restore-to-
  side-location; File History's per-file cascade is the trap to avoid). Therefore:
  - The preview distinguishes, **per item**, "unchanged since incident" from "modified since —
    restoring discards newer content," with the latter **unchecked by default**.
  - A folder/recover-all restore never silently cascades a single top-level decision across
    modified files.
  - In-place overwrite of a file modified since T requires a separate explicit opt-in; the
    default keeps both.
- **[PATTERN] VIII.3 — Preview → verified success (Charter VI.1.4–5).** Preview states what,
  from when, to where, and conflict handling, with the reassurance *"your current files are
  safe; recovery replaces only after byte-for-byte verification."* Success is a loud, itemized,
  per-item verified report ("X of Y restored, byte-for-byte verified; Z declined (reason)"),
  showing the kernel's returned status — never secret material (Charter I.12).
- **[PRINCIPLE] VIII.4 — Recovery reflects current availability.** Because a probation hold may
  yield at any time (and this is honest, not a broken promise — Charter I.4a), the flow reflects
  availability at fetch time and never firm-promises a probation item it may not hold at
  execution; verify-before-replace (Const. I.3) is the final guard. This is not a probation
  eviction *alarm* (which V.2 forbids).
- **[PATTERN] VIII.5 — Onboarding / first run is the third hero flow (Charter VI.4.1).** For a
  product whose one idea is legible certainty, the install is where certainty expectations are set
  and trust is won or lost — so it is spine material, not a dialog. The flow: (1) **pre-empt the
  platform trust prompts** — explain the driver / SmartScreen warning *before* Windows shows it
  (Charter VII.5.2), so a scary wall is not the first impression; (2) explain the two recovery
  assets in one plain screen and show the coverage line once, in full; (3) default to **AUDIT**
  with its honest meaning stated ("attacks are recorded and recoverable, not yet blocked") — this
  is the surface where the AUDIT-vs-ENFORCE choice, the product's highest-consequence decision, is
  actually made, with ENFORCE's three-trigger consequence stated (Charter VI.2.1); (4) help the
  user pin the tray verdict glyph (IV.4). It nudges toward ENFORCE only after the system has been
  quiet enough to trust the exemption set (Const. V.1). The Part IX budget/exemption-discovery
  flow is what makes that settling period legible.

---

## PART IX — NORMAL-DAY FLOW: BUDGET ATTRIBUTION & EXEMPTION DISCOVERY

The 99.9% case. Because the system targets FN=0, legitimate high-churn apps steadily consume
the preservation budget; the operator needs an intuitive, calm way to see where the budget
goes and to exempt a trusted heavy app. This is the interface realization of Const. V.1
(*"AUDIT is the posture for discovering the whitelist"*).

- **[PRINCIPLE] IX.1 — Framed as resource hygiene, never as detection or accusation.** The view
  answers "where is my recovery budget going," never "here are the mistakes we made, correct
  us." Preservation of a legitimate app's writes is **not** a false positive: the gate never
  accuses and preservation is not a conviction. This framing is load-bearing; without it the
  view becomes a forbidden surveillance console (Const. VI, invisibility).
- **[DECISION] IX.2 — Denominate in time; capacity is secondary.** **[VERIFIED]** every mature
  precedent (Time Machine, File History, Backblaze, NAS snapshot tools) leads with a time window
  ("recoverable back to ~N days"), and the one product that hides depth-of-protection (Windows
  Backup) is criticized for false reassurance. The headline is achieved-window vs. target-window;
  capacity ("cache 62% full") is a secondary diagnostic line. Learning from Time Machine's
  silent thinning, the window degrading below target is a **proactive amber** on Home (V.4), the
  discovery entry point — not a silent depletion.
- **[DECISION] IX.3 — Form: sorted ranked list with proportional bars.** **[VERIFIED]** bar/
  length encodings are read faster and more accurately than treemap area or sunburst angle for
  non-experts (NN/g; CHI 2019 wrapped-bar-lists study; Cleveland–McGill). The view is a trend
  sparkline above a ranked list; each row = app icon + name + a "% of recovery window" bar +
  absolute value (window impact first, bytes on expansion) + a delta vs. prior period; a
  time-range selector (24h / 7 days / since last full backup). Treemap/sunburst is at most an
  optional power-user "explorer." **Drill-down, not drill-up**: tapping an app row expands to
  that app's captured items — never start from folder hierarchy. Progressive disclosure ≤ 2
  levels.
- **[NEGATIVE] IX.4 — Aggregate only; no surveillance.** The view shows app × coarse file-class
  × aggregate impact — **never** a per-file path list in this view. Attribution metadata inherits
  the same retention bounds as the copies it describes. Because app-identity attribution is
  defensive-posture intelligence (which apps are heavy = which to target), the view is on the
  **itemized (elevated) plane** (VII.3.1), not the posture plane.
- **[DECISION] IX.5 — Placement.** The **attribution view lives in Settings**, adjacent to the
  budget it explains (the arrival question "why is my window shrinking" is a budget-health
  question, Charter V.5). The **exemption action** on any row deep-links into the Policy
  exemption flow (Part X) with its full friction — discovery is separated from the consequential
  act, matching backup-product and AV/EDR practice.
- **[PATTERN] IX.6 — Design the "unattributed" bucket first.** Per-copy app attribution accrues
  **forward only** from when its backend field ships (Part XII), so the view's first weeks are
  dominated by an "older activity (before attribution)" slice. The design handles that slice
  gracefully from day one, or the feature launches looking broken.
- **[PRINCIPLE] IX.7 — The elevation toll is once-per-visit, acknowledged.** Because the
  attribution view is on the itemized plane (IX.4), opening it costs one fresh elevation, and the
  Home amber (IX.2) leads directly into that toll. This is accepted (per-app preservation status
  is exactly the intelligence an attacker wants before encrypting), and it is made tolerable by
  the III.1 snapshot model: one elevation fetches the attribution snapshot, and the user then
  sorts / filters / drills down in-process **without re-elevating** — the toll is once per visit,
  never per interaction.

---

## PART X — EXEMPTION: CONFIRMATION AND STALENESS

- **[PATTERN] X.1 — Honest, proportionate confirmation (Charter VI.3.1).** Adding an exemption
  is *"stop protecting this app,"* a consequential disarm gated by fresh elevation. **[VERIFIED]**
  proportionate friction beats theater (NN/g confirmation-dialog & proximity research; AWS
  Cloudscape delete-confirmation threshold): a single **signed** app gets a standard modal, not
  type-to-confirm (which is disproportionate and habituating). The confirm dialog shows, **inline
  (not behind a "more info" expander — the buried-override is SmartScreen's anti-pattern)**: the
  resolved identity (path, signer, content hash, first-seen date), the plain consequence stated
  in ladder terms (*"files changed by X while exempt become **UNRECOVERABLE**, and this cannot be
  undone for changes that happen while exempt"*), and the **quantified cost** we uniquely have
  (count/size of currently-protected files under this app, and what the exemption would have cost
  recently). The exemption is revocable and logged (cheap reversal justifies lower initial
  friction).
- **[DECISION] X.2 — Escalate friction only for genuinely higher blast radius.** Type-to-confirm
  and/or a forced delay apply when the target is **unsigned / unknown-publisher**, the scope is
  broader than one app (folder / wildcard / system path), or the target runs elevated. Every
  exemption change is logged with identity + timestamp (CrowdStrike's audit-comment pattern), and
  a periodic, independent "exemption health check" (Sophos's pattern) surfaces stale/overbroad
  exemptions separately from where they were created.
- **[NEGATIVE] X.3 — The enforced exemption anchor stays content-hash ∧ certificate subject;
  it is NEVER relaxed to signer + minimum-version, and there is NO silent auto-heal on update.**
  Research strongly recommended a FilePublisher-style anchor (signer + product + version floor)
  so app updates pass silently, and a Little-Snitch-style silent auto-heal. **Both are rejected**
  as contradicting Constitution VI: the hash pin is a **deliberate fail-safe** — a different
  binary, even from the same signer, must **not** inherit the exemption; signer-based matching
  broadens the trust surface to everything that publisher ever signs, which is exactly what this
  system's tight identity binding, interpreter prohibition, and injection-proofing exist to avoid.
  When an exempted app updates and its hash changes, the exemption **correctly stops matching**,
  the app is re-monitored, and budget resumes — this is right behavior, not a defect.
- **[PATTERN] X.4 — The re-affirm is calm and non-urgent, because the lapsed state is already the
  safe state (closes the HANDOFF "whitelist update re-approval UX" gap; a genuine market
  differentiator).** When an exempted app updates and its hash lapses, the app is **re-monitored**
  (its writes are preserved again) — so protection is *restored*, not lost, and there is no
  urgency. The routine (same-signer) case is therefore surfaced as a **quiet, non-blocking
  Policy-surface notice with a badge/count** — never a Home posture amber (V.4), never an
  interrupting prompt: *"X updated — it's being protected again; re-confirm its exemption when
  convenient."* The re-confirm shows a diff (old → new hash / version, signer CN, timestamp) as
  **decision evidence only** and, because granting an exemption is a consequential disarm, is
  gated by fresh elevation when the user chooses to act — but it is never silent, never
  auto-applied, and never rushed. A **changed signer identity** (possible substitution) is the one
  case escalated to a prominent, visually-distinct warning (and the one exemption-staleness case
  eligible for amber, V.4). The enforced anchor remains hash ∧ signer throughout; the diff informs
  the human, it does not auto-approve.
- **[NEGATIVE] X.6 — No "auto-renew on same-signer update," and no "preserve an exempted app."**
  An external review recommended a hybrid that would auto-renew the exemption on a same-signer,
  version-monotonic update *while continuing to preserve the app's writes for an N-day window*.
  It is rejected on two independent constitutional grounds: (a) auto-renewing exemption on
  same-signer identity **is** the signer-based matching already rejected in X.3 — a new binary
  inheriting an exemption without hash re-verification; (b) continuing to preserve the writes of
  an app the system treats as exempt **violates the absolute "exemption means zero monitoring"
  contract** (Const. VI.1, VII.3.3; Charter I.6) — an exempt identity is preserved for nothing and
  watched for nothing, with no probationary middle state. Only the review's underlying *insight* —
  that the re-monitored lapse is already safe, so the re-confirm need not be urgent — is adopted,
  in X.4's non-blocking treatment. The correct "keep copies flowing" behavior is exactly what the
  lapse *already* produces: the un-exempted app is preserved as normal until the user re-confirms.
- **[NEGATIVE] X.5 — Interpreters are never exemptable, at any friction (Const. VI).**
  `powershell.exe`, `pwsh.exe`, `cmd.exe`, `wscript.exe`, `cscript.exe`, `mshta.exe`,
  `python.exe`, `node.exe` and peers cannot be added to the exemption list regardless of
  signature — this hard stop sits above all of X.1–X.4. **Scope clarification:** the ban targets
  a *standalone interpreter host image*. An exemption binds the **signed application host image**
  (hash ∧ signer) the user chose; an interpreter runtime *embedded inside* a signed application
  (e.g. the Node runtime inside a signed Electron app) is covered by that app's own image
  identity and is not a separately-exemptable interpreter — the enforcement is on the process
  image, not on what a signed binary embeds.

---

## PART XI — CANONICAL FAILURE-STATE WORDING & DIAGNOSTICS (the spine covers the unhappy paths)

- **[PRINCIPLE] XI.1 — The honesty brand is earned in the failure states.** The spine's
  "canonical honest wording" explicitly covers the unhappy paths, so they are not improvised
  per screen. Canonical, claims-reviewed, translatable strings exist for at least: **integrity
  halt** (red + foreground window), **backend unreachable**, **component version mismatch**
  (degrade to honesty; disable verbs that cannot be safely framed — Charter VII.6.1),
  **elevation declined**, **recovery verification failed / declined** (the target is left
  byte-for-byte intact), **preserve capacity exhausted**, and **probation hold no longer
  available**. Each states the fact plainly and, where the operator can act, the action.
- **[DECISION] XI.2 — Diagnostics are opt-in, plain, and never a dark pattern; crash artifacts are
  sensitive (Charter IX.4).** There is no silent telemetry. Any diagnostics are opt-in, plainly
  described, and never a monetization or engagement surface. Crucially, a crash dump or error
  report of the app process can contain an itemized snapshot (III.4), so dumps are treated as
  sensitive: suppressed, redacted, or gated behind explicit consent — never auto-transmitted with
  process memory. Stating "no silent telemetry" is itself a spine decision so the stance cannot
  drift later.

---

## PART XII — BACKEND PRECONDITIONS (governed Constitution-projection amendments)

These are the only backend deltas the frontend needs. Each is a **Constitution projection
amendment** (Const. III.1.3 / Charter III.4 discipline), filed formally — never invented in
the UI. They are first-class critical-path milestones, filed immediately after the first
mock feedback round.

- **[OPEN] XII.1 — Per-item pool status on the preserve entry.** `semantics_ar_preserve_entry_t`
  carries no probation/protected field; only aggregate counts exist. BOUNDED per-item honesty
  (Charter IV.1.2 / I.4a) requires it. **Until it lands, BOUNDED items degrade to the
  conservative probation-honest framing** ("may yield sooner under storage pressure") — never a
  firm protected date we cannot guarantee.
- **[OPEN] XII.2 — Per-copy app attribution on preserve holds (highest priority).** Preservation
  is identity-independent at capture (Const. VI), but recording the causing app as evidence
  metadata on the elevated plane is what Part IX needs. This value **accrues forward only**, so
  every week of delay is a week of "unattributed" data at launch — file first.
- **[OPEN] XII.3 — Integrity-halt posture signal.** The posture frame has flags / descents /
  preserve-health but no dedicated VII.1.4 rollback/tamper signal to drive the red + foreground-
  window alert (V.3, Charter III.2a). Confirm or add.
- **[OPEN] XII.4 — `actor_start_key` on the preserve entry.** The catalog already has it (so
  DEFINITIVE incident grouping works today); preserve holds lack it, so BOUNDED incident grouping
  (VIII.1) degrades to a flat list until it lands.

---

## PART XIII — BUILD SEQUENCING

- **[DECISION] XIII.1 — Lock the spine only; let screens close by feedback + verification.**
  This document is the locked spine. Individual screens are deliberately not pre-decided; the
  Charter is a philosophy closure boundary, not a screen inventory, and its [OPEN] items close
  by evidence/VM, not up-front taste.
- **[DECISION] XIII.2 — Mock order ≠ wire order; the two interleave.** Mocks are built as fast,
  throwaway-cheap, **contract-aware** interactive HTML prototypes (Claude Design) for a rapid
  owner-feedback loop, ordered by **uncertainty** (Recovery and the Part IX budget flow first).
  **Every field on a mock must trace to a real backend field or a named Part XII precondition** —
  a mock never invents data the backend does not project (the rule that makes throwaway mocks
  safe; enforced as a literal field ledger). WPF implementation, wired to the running backend, is
  ordered by **risk**: the non-elevated posture/Home + tray slice is wired **first**, because its
  guarantees (no-prompt read, no-paths channel, version-skew degradation) are invisible in a
  static mock and must be proven against the real backend, not deferred.
- **[DECISION] XIII.3 — A small WPF walking-skeleton spike accompanies the first mocks, and it
  carries the large-list/UIA spike.** The highest-uncertainty flows have uncertainty HTML cannot
  show (the consent rhythm's *feel*, verify-before-replace, list feel at scale). A thin WPF
  "click → elevate → itemized list → restore → verified report" skeleton (fake data behind the
  real seam) lets the **owner** feel the real consent rhythm before signing off — distinct from
  the XIII.2 security proof. **The skeleton also runs the IV.5 large-list test with Narrator/UIA
  active *before* Recovery's list is designed**, because the known O(n) UI-Automation peer cost is
  an architecture risk that can force a different list control — discovering that after the list UI
  is built is expensive.
- **[DECISION] XIII.4 — "No path leak" is a proof artifact, not a code review.** Slice-1
  acceptance criteria, written now: (a) a schema assertion + negative/fuzz test at the interop
  boundary that the posture wire has no path-capable field; (b) an automated check that Home +
  tray render with zero elevation prompts and cannot obtain any path/hash/key-id/per-item entry;
  (c) version-mismatch degradation exercised against a deliberately-skewed backend. This is the
  closure of Charter X.2.
- **[DECISION] XIII.5 — Swappable data-source seam; mocks are not dead code.** Mock data providers
  implement the **same interface** as the real channels; when a real slice lands, its mock
  provider is **deleted** (HANDOFF binding rule: no dead/compat code). The recurring need to demo
  rare states (integrity halt, red posture) is met by **test fixtures / a dev harness**, not by
  retaining shipped mock providers.
- **[DECISION] XIII.6 — Mock sign-off scope is stated in writing.** Mocks lock information
  architecture, field content, and wording; each WPF slice gets a second, short owner pass for
  *feel* (density, fonts, dialogs, theming) so the HTML→WPF delta is not misread as regression.
  A WIP limit of one mock + one slice in flight avoids thrash for a small team.
- **[DECISION] XIII.7 — Failure states ride the first wired slice.** XI declares failure wording
  brand-critical and XV.2 gates it legally, but it must also be *sequenced*: the posture/Home slice
  hits **backend-unreachable** and **component-version-mismatch** naturally on day one, so their
  canonical wording and fixtures (per XIII.5) are built with slice 1, not deferred. Integrity-halt
  and elevation-declined fixtures follow with their first relevant slice.

---

## PART XIV — CONFORMANCE / DEFINITION OF DONE

A frontend slice is done only when it satisfies the Charter Part XI conformance checklist
(15 items) and Constitution Part XI, **and is VM-verified against the running backend**. In
particular:

1. No claim exceeds what the Constitution proves; absolute words are scoped to the per-file
   mechanism (VI.3).
2. Recoverability is shown by rung with color + icon + label; BOUNDED shows expiry honestly by
   pool (VI.2).
3. The coverage boundary is visible wherever DEFINITIVE could read as universal.
4. No operator action silently destroys present data; Recovery previews and verifies, and
   solves the modified-since-T trap (VIII.2).
5. Consequential verbs and itemized reads are gated on fresh elevation and token authorization;
   the posture surface is prompt-free — **and this is proven by the XIII.4 artifacts, not
   asserted**.
6. Modes and exemption carry their honest constitutional meaning; ENFORCE names all three
   triggers; interpreters are never exemptable (X.5).
7. No detection knob is exposed; only the budget is settable, denominated in time (IX.2).
8. Posture defaults to calm; red is reserved for broken/integrity-halt; a chosen AUDIT is
   neutral, not standing amber (V.4); notifications are tiered and Windows-reality-aware (V.3).
9. Meaning survives without color; keyboard + screen-reader navigable, including through the
   elevation boundary and on the largest list with UIA active (IV.5).
10. No scareware, no upsell, no fabricated urgency.
11. No phantom identity is ever disclosed (Charter I.11).
12. No key material, IV, verification tag, or preserved plaintext is ever shown/requested/
    exported (Charter I.12).
13. No evidence field beyond the constitutional projection is shown; any such field is an
    [OPEN] pending a Part XII amendment.
14. An integrity halt is red + foreground window (never toast-only); a platform descent is a
    stated fact, amber only where actionable.
15. Any load-bearing outside-world claim added here carries [VERIFIED] or is written as
    motivation.

**Operational tier (a slice is not done until these hold too):** the IV.5 empirical idle-memory
target and list-scroll performance are met; the budgeted Windows-10 QA pass (IV.2) is run; the
Windows **High Contrast** rendering mode is verified (distinct from "meaning survives without
color"); the installer's .NET-runtime chain-install (IV.3) and the app's own update path are
exercised; each consequential verb is exercised under **both** classic UAC and, once testable,
Administrator Protection (XV.4); UI strings pass a **pseudo-localization** check (XI declares them
translatable); and crash/error-reporting is verified to treat dumps as sensitive per XI.2 (no
snapshot memory auto-transmitted).

---

## PART XV — OPEN ITEMS (execution & governance gates, not design decisions)

Design is closed. What remains is execution:

- **[OPEN] XV.1 — The four Part XII backend amendments** are filed and VM-verified (XII.2 first,
  forward-accruing).
- **[OPEN] XV.2 — Final legal clearance of the canonical claim strings** (Charter X.1) for the
  DEFINITIVE rung and the coverage line, per the scoping discipline of VI.3. *Closes by:* legal
  review.
- **[OPEN] XV.3 — Posture-pipe DACL confirmation.** *Measured (Beat 0, 2026-07-10, against the
  running backend in SarTarget):* the service creates the posture and events pipes with SDDL
  `D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x120189;;;IU)` (`service/control.c`) — read-only `0x120189`
  (≈`FILE_GENERIC_READ`, **no** `FILE_CREATE_PIPE_INSTANCE`/0x4) to **Interactive Users (IU,
  S-1-5-4)**, full access to SYSTEM/Administrators, and `FILE_FLAG_FIRST_PIPE_INSTANCE`
  anti-squat (`service/pipe_server.c`). A `posture_probe` linking the real `sarapi` read a valid,
  protocol-v1 frame end-to-end (III.7 server-is-SYSTEM check passed; events pipe opened) — so the
  pipe **is session-readable and the no-prompt property holds for interactive users**. **Residual
  decision:** the grant is to `IU` (all interactive sessions), not the per-session **Logon SID**
  III.5 preferred, so it does not isolate concurrent interactive sessions from each other's
  *redacted* posture. Accept `IU` for the low-sensitivity posture frame, or tighten to the Logon
  SID (a small backend change). *Also surfaced:* the III.7 client-side server verification
  (`GetNamedPipeServerProcessId`→`OpenProcess`→token) returns `SERVER_UNTRUSTED` for a
  SAFER-restricted client token that cannot `OpenProcess` the SYSTEM service — a normal medium-IL
  interactive client is expected to pass (`PROCESS_QUERY_LIMITED_INFORMATION` is broadly granted),
  but this is a documented robustness boundary. *Still to prove empirically:* a non-admin
  **interactive** medium-IL read via the `IU` grant (Run 1 passed via the `BA`/admin grant; a
  batch/scheduled-task logon lacks the `INTERACTIVE` SID, so this needs an interactive or
  linked-token launch). *Closes by:* that medium-IL interactive run + the Logon-SID decision.
- **[OPEN] XV.4 — Administrator Protection re-verification, including the III.6 token semantics.**
  Re-verify the elevation path against a current Windows Insider AP build before shipping (III.2)
  — and specifically re-verify that the III.6 authorization predicate (`TokenElevationTypeFull`)
  still means what it means under AP, since the elevated context runs under a separate
  SYSTEM-managed admin identity and elevation-type reporting may differ. *Closes by:* a VM pass on
  an AP-enabled build, exercising each consequential verb under both classic UAC and AP.
- **[OPEN] XV.5 — Remote-origin incident labeling** (Charter X.3) and **self-protection posture
  surfacing** (Charter X.4) remain as the Charter defines them; this document adds no new
  dependency.
- **[OPEN] XV.6 — Uninstall / offboarding disposition.** The product holds copies of user
  originals, captured keys, exemption records, and evidence. What happens to all of it on
  uninstall (destroyed? exported? retained?) is a cross-cutting trust and likely legal decision
  (adjacent to XV.2). The frontend must **surface the offboarding choice and its consequences
  honestly**; the disposition policy itself is governed by the Constitution's data-lifecycle
  position, not invented here. *Closes by:* a governed decision + its honest UI surfacing.
- **[OPEN] XV.7 — Multi-user / multi-session data ownership.** The endpoint may have several
  Windows users. Whether exemptions are machine-wide or per-user, and whose captured originals
  appear in whose Recovery view, is a projection/identity question the frontend surfaces but does
  not decide. *Closes by:* confirming the Constitution's identity model for multi-user endpoints;
  until then the app assumes the single-administrator-operator scope of Charter II.2.1 and states
  that scope honestly.
- **[OPEN] XV.8 — Clock integrity for BOUNDED "firm date" claims.** A protected hold's firm
  expiry date is rendered from system time, which the in-scope adversary can tamper with; the UI's
  firm-date claim inherits this. Whether the countdown is anchored to a trusted time source is a
  backend concern the UI depends on. *Closes by:* a backend position on retention-clock integrity;
  until then the firm-date wording is honest only to the extent the system clock is.

---

---

## APPENDIX A — SLICE-1 BUILD & VM-VERIFICATION RECORD (2026-07-10)

The posture-plane vertical slice (Part XIII.2, risk-ordered first) was built and verified
against the running backend in `SarTarget`.

**Built:** `sarapi.dll` (CMake/MSVC, from the root tree); `SemanticsAr.Core` aligned to §V.4
(AUDIT collapses to green/neutral once acknowledged; `PostureService.AuditAcknowledged` added);
`SemanticsAr.Verify` (a self-contained single-file harness driving Core — `NativePostureReader`,
`PostureEvaluator`, `JournalService`/`NativeEventReader` — against the live pipes). Full frontend
builds with 0 errors; Core unit tests 69/69.

**VM-verified (9/9 PASS):** sarapi ABI compatible; posture DTO is a fixed 40-byte frame carrying
**no path-capable field** (II.2 proven by reflection + size); event DTO carries no path-capable
field; posture read **OK** (session-readable; III.7 server-is-SYSTEM check passed); verdict =
**Amber/AuditMode** (correct unacknowledged-AUDIT per V.4); protocol version matches (no skew);
service running; driver connected; **events pipe connected and 238 events flowed** (including the
`ModeChanged` events triggered during the window). This closes the core of XIII.4(a)/(b) at the
Core layer against the real backend.

**Findings (surfaced by the run):**
1. **[FIXED] Event-class enum gap.** The driver emits `BLOCK_INJECTION` (class 8) and
   `EXEMPT_REVOKED` (class 10) per `protocol.h`, but `sarapi.h`/`JournalEventClass` defined only
   0–7, so those rendered as raw numbers. Both are now added to `JournalEventClass` and `sarapi.h`.
2. **[FIXED] Detection-toast coalescing.** `ToastNotifier` toasted per detection event, which under
   a bulk attack would spam and defeat V.1's anti-habituation discipline. It now coalesces
   detection events in a 3s window into one count-aware toast per class. `BLOCK_INJECTION` and all
   housekeeping classes remain **silent** by design (they carry no `Describe` string) — verified
   correct against the 238-event flood (overwhelmingly `BLOCK_INJECTION`), which produced no toasts.
3. **[BACKEND INTERACTION — record, not a frontend defect] Phantom-witness directory seeding.**
   Deploying the harness into a newly-created directory under a monitored volume failed: the
   phantom witness (Const. VIII) seeds *protected decoy files* into new directories, and a
   non-exempt process's bulk directory cleanup/overwrite (`Expand-Archive -Force`) is denied on
   those decoys. Writing **new files** (distinct names, non-destructive) succeeds — deploying two
   files individually worked. **Implication for the installer/updater:** write new files, never
   bulk-delete/overwrite a monitored directory as a non-exempt process; the app's normal
   new-file writes to its own storage are unaffected.

**Remaining Slice-1 increments (App presentation, over the VM-verified Core):** wire the §V.4
acknowledge-AUDIT action to `PostureService.AuditAcknowledged` in the Home surface; aesthetic
polish of the Home hero + tray glyph to the mock's certainty-ladder language; the integrity-halt
foreground-window tier lands with its backend signal (XII.3). The GUI's visual pass is an owner
review; its backend correctness is inherited from the Core seam proven here.

---

> This document is subordinate to `EXPERIENCE_CHARTER.md` and `CONSTITUTION.md` and is
> maintained alongside them. It fixes the frontend spine; the pixel-level design system and
> component library it authorizes fix the rest. Every frontend decision — in a mock, a slice,
> or a review — obeys what is written here, or amends it here first.
