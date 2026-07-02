# semantics-ar — EXPERIENCE CHARTER (operator surface)

> **STATUS: RATIFIED (v1.0).** This charter governs the design of the **operator surface**
> of semantics-ar — the frontend through which a human installs, understands, trusts, and
> drives the product. It is the design counterpart to `CONSTITUTION.md` and is
> **subordinate to it**: where this charter and the Constitution disagree, the
> Constitution is right and this charter is wrong.
>
> This is a governing document, not a screen inventory. It fixes the philosophy, the
> invariants of the experience, and the load-bearing platform decisions, so that a large
> and long-running frontend build stays coherent. It does not enumerate every control; it
> fixes what every control must obey.

---

## PART 0 — HOW TO READ THIS CHARTER (binding)

### 0.1 This charter is subordinate to the Constitution
`CONSTITUTION.md` is the authoritative specification of **what the system proves and
does**. This charter specifies **how the system is presented to and operated by a
human**. The frontend is a *derivation of* the Constitution, never a re-opening of it.

> A design decision here may refine *how* a constitutional truth is surfaced. It must
> never contradict, soften, or silently widen a constitutional truth. If a desirable UX
> would require the system to claim more than the Constitution proves, or to reveal what
> the Constitution keeps closed, the UX is wrong, not the Constitution.

The Constitution is a *closure boundary* for mechanism. This charter is a *closure
boundary for the experience*: it fixes the philosophy your re-research and iteration may
refine, and the small set of experience-invariants they may not reopen.

### 0.2 What this charter governs — and does not
**Governs:** the product's guiding proposition-as-experience; the information
architecture; the hero flows; the visual and interaction language; the platform and
transport decisions that are load-bearing for trust, footprint, and correctness; the
claims discipline; accessibility and notification behavior; the frontend's own
boundaries.

**Does not govern:** detection, conviction, capture, recovery correctness, the gate, the
keystore, preservation semantics, self-protection *mechanism* — all owned by the
Constitution. Where the frontend has hard **backend preconditions** (new endpoints,
projected fields, transport hardening), this charter *names them as preconditions*; their
normative home is a Constitution Part VII amendment and/or the backend design docs, not
this charter (VII.0). Nor does it govern pixel-level visual specification, which lives in
the design system and component library it authorizes (closing note).

### 0.3 Item types
Every normative item carries exactly one tag.

- **[PRINCIPLE]** — A durable value the experience must always express. If a screen
  violates it under any state, the screen is wrong.
- **[DECISION]** — A chosen method (framework, transport, pattern). Implement as written;
  vary only within stated latitude.
- **[PATTERN]** — A recurring interaction/visual solution to reuse for consistency.
- **[BOUNDARY]** — A thing the frontend deliberately does **not** do.
- **[NEGATIVE]** — A thing the frontend must never do.
- **[OPEN]** — A decision deferred to empirical/legal/VM closure, not to taste. Each names
  how it closes.

Where a claim about the outside world (Windows behavior, regulation, framework state) is
load-bearing, it carries **[VERIFIED]** (independently confirmed, source on file) so that
false certainty does not enter this document. A load-bearing outside-world claim that is
**not** yet [VERIFIED] is written as motivation, not as fact, and is marked **[OPEN]** if a
decision rests on it.

### 0.4 The one design idea
Everything below is one sentence, unfolded:

> **The interface makes the certainty of protection legible — and never claims more
> certainty, nor reveals more, than the evidence and the Constitution allow.**

The Constitution's governing proposition (Part I) is *"the strength of the response is
proportional to the certainty of the evidence."* Its interface translation is the
**certainty ladder** (Part IV): every place recoverability or protection is shown, it is
ranked by the strength of the evidence behind it, honestly, including where there is none.
This is the spine of the product. Every other decision serves it.

---

## PART I — BINDING INTERFACE (the constitutional clauses the frontend obeys)

These are the constitutional truths the operator surface is **bound by**. Each states the
obligation it imposes on the experience. This section is restated, not owned, here; the
Constitution is the source.

- **[PRINCIPLE] I.1 — Graduated response (Const. Part I, I.1).** The UI presents protection
  as **graduated to evidence**, never as a single binary "protected/not." The two recovery
  assets and the behavioral witness are shown as *distinct strengths of certainty*, not
  merged into one green light.
- **[PRINCIPLE] I.2 — The honest edge (Const. IX.2).** *"The edge is stated, not hidden."*
  Where neither asset can recover data, the UI says so plainly and says why. A false
  all-clear is a constitutional violation, not merely a UX flaw. Coverage boundaries
  (Const. IX.1–IX.4) are surfaced, not buried.
- **[PRINCIPLE] I.3 — Verify before replace (Const. III.4, III.5.6).** Recovery is verified
  against a capture-time tag (and, for held originals, an integrity + (file,region)
  binding) before it replaces a file; a mismatch declines and leaves the target
  byte-for-byte intact. The UI surfaces this as the core **safety promise of recovery**
  ("your current file is never destroyed to attempt a recovery").
- **[PRINCIPLE] I.4 — Two assets, ranked (Const. II, III.5).** The **Oracle** (key capture)
  is definitive and unbounded; **preservation** is circumstantial and bounded by a
  time+capacity window across two pools (Const. III.5.5). The UI must never present a
  bounded, expiring hold as if it were the definitive asset, must reflect the pool a hold
  sits in (I.4a), and must show the window's expiry per held item.
- **[PRINCIPLE] I.4a — Probation vs protected (Const. III.5.5).** A held original sits in
  **probation** (silently yields, newest-first-kept, under capacity pressure at any time)
  or **protected** (promoted on conviction; reclaimed by retention age alone). The UI never
  gives a probation hold the firm promise it gives a protected hold (see IV.1.2). The pool
  is a retention fact, never a detection or conviction signal shown to the user.
- **[PRINCIPLE] I.5 — Modes are the user's policy, not the system's inference
  (Const. V.1).** AUDIT and ENFORCE are the operator's decision. Observation, capture,
  preservation, recovery, and visibility are always on in both. The UI never implies the
  system decided "now is the time to block"; it presents ENFORCE as a policy the user
  adopts, with its consequences stated (VI.2.1).
- **[PRINCIPLE] I.6 — Whitelisting means "stop protecting" (Const. V.2).** A whitelist entry
  is a **full exemption the user owns**, resolved at process creation against verified
  identity (image path ∧ signature ∧ content hash). The UI presents it with that exact
  honest meaning — never as a soft "trust but keep watching."
- **[PRINCIPLE] I.7 — One knob, and it is a resource envelope, not a detection tuner
  (Const. 0.5, V.1.3).** The only value the user may set is the preservation **budget**
  (retention time + capacity). The UI frames it as a backup-retention policy and never
  exposes any detection/conviction threshold as a setting.
- **[PRINCIPLE] I.8 — Behavioral, identity-independent capture (Const. VI).** Capture does
  not depend on who issued a write. The UI never presents a per-write "requestor" identity
  as the basis of capture or recovery; identity appears only in the whitelist and, as
  *evidence about a convicted actor*, in the activity surface.
- **[PRINCIPLE] I.9 — The phantom witness is evidence, never a recovery source
  (Const. VIII).** Phantom convictions may appear in the activity surface as behavioral
  evidence about the convicted process; the UI never offers to "recover" from the phantom
  layer.
- **[NEGATIVE] I.11 — The phantom set is never disclosed (Const. VIII.0, VIII.1.2,
  VIII.2.1).** No operator surface, event record, notification, log export, or read
  endpoint ever carries a phantom's name, path, file id, extension, or location, at any
  privilege level. A phantom conviction is surfaced strictly as behavioral evidence *about
  the convicted process* — process identity, independent-write count K, timestamps — never
  as the decoy files it touched. Any persisted or exported decoy identity would break the
  kernel-privilege closure the Constitution guarantees (VIII.0); the interface must not be
  the leak the kernel refuses to be.
- **[BOUNDARY] I.12 — Key material and preserved plaintext never reach the operator surface
  (Const. III.1.2, III.1.3, III.5.6).** The UI never displays, exports, requests, or offers
  a control implying access to key bytes, IVs, verification tags, or held-original content.
  Recovery is driven by identifier and path only; the proof the UI shows is *status*
  (verified / declined), not secret material.
- **[BOUNDARY] I.10 — The UI is below the kernel line (Const. IX.1; authority Const. VII.3).**
  The frontend is a standard-user (or on-demand elevated) user process; consequential
  authority lives in the SYSTEM service, which authenticates its client (Const. VII.3). The
  UI is **not** a security boundary and is not presented as one. Its compromise or absence
  loses *future observation and operator control* — and hands a hostile session process
  whatever a read endpoint serves it, which is why that endpoint is posture-only (VII.3.1) —
  but never retroactively undoes past captures.

---

## PART II — WHAT THE PRODUCT IS, AND FOR WHOM

### II.1 Essence
semantics-ar is a defensive anti-ransomware system whose distinctive promise is **recovery
certainty from captured evidence**, not mere prevention. Unlike prevention-first security,
its emotional core is **calm confidence**: *"if something runs, here is exactly what we can
bring back, and here is the proof."*

### II.2 Operator and scope
- **[DECISION] II.2.1 — The operator is an administrator of a single endpoint.** The product
  requires a kernel driver + SYSTEM service; the operator is whoever administers the
  protected machine. The experience is designed for that person.
- **[DECISION] II.2.2 — Scope is single-endpoint, local, this release.** The backend has no
  remote/fleet transport. The frontend is a local desktop application. A fleet/remote
  console is a future evolution (Part IX), and the IA is drawn so that evolution does not
  require re-teaching the core.

### II.3 The emotional register
- **[PRINCIPLE] II.3.1 — Calm by default; urgency only when earned.** The dominant state of
  the product is quiet reassurance. Visual urgency is spent only on an actionable,
  evidence-backed event. The product is the opposite of scareware.

---

## PART III — DESIGN PHILOSOPHY

- **[PRINCIPLE] III.1 — Glanceable verdict.** One hero element answers *"am I protected?"* in
  under a second. Everything else is progressive detail beneath it.
- **[PRINCIPLE] III.2 — Green is the default; red is earned through an amber tier.** Three
  posture states: **green** (protected, no action), **amber** (a recommendation the operator
  can act on — e.g. "you are in AUDIT; attacks are recorded but not blocked"), **red**
  (actually broken). Red never means "could be better"; that is amber's job. Illustrative,
  not a closed enumeration: green = protecting normally; amber = AUDIT-mode recommendation,
  a recorded platform descent the operator can improve (III.2a); red = driver not
  loaded / service not responding / an integrity halt (III.2a) / protected-pool holds being
  lost. Probation-pool churn is silent **by design** (Const. III.5.5) and is never a posture
  alarm.
- **[PRINCIPLE] III.2a — Constitutionally mandated alerts and descents have a fixed home.** An
  **integrity halt** (Const. VII.1.4 — keystore/preserve-store rollback, tamper, or erasure
  detected) is **red and the modal tier** (III.3); it is the one alert that is never only a
  toast. A **recorded platform descent** (Const. VII.5 — no TPM/VBS seal, no PPL, no HVCI) is
  surfaced as a stated fact on Home/Settings, **amber only where the operator can act on it,
  never red** — a descent is honest posture, not breakage.
- **[PRINCIPLE] III.3 — Tiered notification; silent by default.** Housekeeping is logged
  silently; a genuine detection raises a toast; only a decision the system genuinely needs,
  or an integrity halt (III.2a), raises a modal. The product never emits per-event noise.
  (Habituation to repeated security alerts is a real, measured effect, not laziness.)
- **[PRINCIPLE] III.4 — Every claim is provable one level down, within the constitutional
  projection.** Behind any verdict sits the evidence **the Constitution projects**
  (Const. III.1.3, III.5.6): for a captured file — provenance path, algorithm/mode, key
  identifier, region offset/length; for a held original — provenance path, region, capture
  time, age, size, pool status; plus facts the client can compute itself (e.g. a hash of a
  file **after** a completed recovery, to show "restored bytes match"). The UI never
  requires, requests, or displays key material, IVs, the verification tag, or preserved
  bytes (I.12). Any evidence field beyond the constitutional projection is **[OPEN]**, closed
  only by a constitutional amendment to the projection — never widened "just for the UI."
- **[PRINCIPLE] III.5 — Progressive disclosure.** The same screen serves the anxious
  non-expert (plain-language story + one verdict) and the analyst (full forensic
  timeline/tree on request, within III.4/I.11/I.12). Default to the story; put depth one
  interaction away.
- **[PRINCIPLE] III.6 — Non-destructive by default.** No operator action the UI offers may
  silently destroy present data. Recovery surfaces verify-before-replace (I.3) as an
  explicit promise; any overwrite is previewed and consented; the current state is never
  clobbered to attempt a restore.
- **[PRINCIPLE] III.7 — Honesty about limits and coverage.** The UI states what it cannot
  recover and what class of attack it does not cover (IV.4), in plain language, at the point
  of relevance. This is the interface expression of Const. IX.2.
- **[NEGATIVE] III.8 — No scareware, no fabricated urgency, no upsell on the safety
  surface.** No fake scan bars, no invented threat counts, no countdown pressure, no
  monetization nagging on the protection screens.
- **[PRINCIPLE] III.9 — Accessible by construction.** Meaning is never carried by color
  alone (VIII.4). The experience is legible under color-vision deficiency, fully
  keyboard-navigable with a visible focus model, and screen-reader-labeled, as a baseline,
  not a retrofit.

---

## PART IV — THE CERTAINTY LADDER (the spine)

### IV.1 The three rungs
- **[PATTERN] IV.1.1 — DEFINITIVE (Oracle).** A file whose encryption key was captured.
  Recoverable **with certainty** by decrypting the on-disk ciphertext and verifying each
  file against its capture-time tag before replacement (I.3). **Unbounded in time** while the
  key is held. Visual weight: strongest, calm, positive.
- **[PATTERN] IV.1.2 — BOUNDED (Preservation), honest by pool.** A file whose original was
  held copy-on-first-write in the bounded window. Recoverability is shown **honestly by pool**
  (Const. III.5.5, projected `status`, I.4a): a **protected** hold shows *"recoverable until
  &lt;date&gt; (&lt;N&gt; days left)"*; a **probation** hold shows *"held — up to &lt;date&gt;,
  may yield sooner under storage pressure."* Never a firm promise the pool cannot keep. Visual
  weight: positive but time-urgent.
- **[PATTERN] IV.1.3 — UNRECOVERABLE.** Data where neither asset holds (Const. IX.2). Stated
  plainly, with the reason. Never masked as green. Visual weight: honest and neutral —
  designed **not** to read as a "disabled/inactive" gray — never alarmist and never falsely
  reassuring.

### IV.2 The ladder is used everywhere recoverability appears
- **[PATTERN] IV.2.1.** The three rungs are one consistent system of **color + icon +
  label** reused in every list, detail view, incident summary, and the tray glyph. A rung is
  never rendered by color alone (VIII.4).

### IV.3 Claims discipline
- **[PRINCIPLE] IV.3.1 — Absolute words attach to the verified per-file event, not to the
  product.** "Certain," "verified," "byte-for-byte," "unbounded" describe *a captured file's
  recovery mechanism*, never product-level protection. Canonical phrasing: *"When we captured
  the encryption key at the write, we restore the original bytes and verify each file with
  SHA-256 before replacing it."* The absolute word never stands unscoped as a headline.
- **[PRINCIPLE] IV.3.2 — The differentiator is scoped, not universal.** The honest,
  defensible differentiator is *per-file cryptographic verify-before-replace on captured
  files* — the product closes the "a copy exists ≠ it restores" gap **for the DEFINITIVE
  rung**. It is not a claim that competing backup products never verify, and not a claim of
  universal recovery.
- **[PRINCIPLE] IV.3.3 — Canonical strings are the claims-reviewed, translatable unit.** The
  English canonical strings (IV.3.1, IV.4.1) are the unit legally pre-cleared (X.1);
  translations are re-cleared per X.1, and the certainty-ladder labels are part of the design
  system, not ad-hoc per locale (VII.6.3).

### IV.4 Coverage boundary is a first-class element
- **[PRINCIPLE] IV.4.1 — Coverage is shown, not implied.** The Oracle captures keys present in
  local memory at a destructive write. It structurally does not capture keys that never touch
  this host — notably **remote/network-driven encryption** — and there is nothing to recover
  in **data-theft-only extortion**. The UI states this coverage line where a user could
  otherwise infer the DEFINITIVE rung covers all ransomware. **[VERIFIED]** remote encryption
  is a large share (≈60%) of human-operated ransomware (Microsoft Digital Defense Report), so
  this honesty is load-bearing, not decorative.
- **[PRINCIPLE] IV.4.2 — An Oracle miss degrades to the lower rungs, and the UI says so.** A
  write the Oracle cannot key-capture still has its original held by preservation (BOUNDED)
  and its behavior may convict via the phantom witness — exactly the graduated model
  (Const. I, IX.2). The UI presents an Oracle miss as *"held/behavioral, not definitive,"*
  never as "unprotected," unless both assets have truly failed (then IV.1.3).

### IV.5 Empty states are a ladder statement
- **[PATTERN] IV.5.1 — "Nothing to recover" is disambiguated.** A fresh or never-attacked
  install has an empty Recovery surface and an empty timeline. The UI distinguishes *"empty
  because you are protected and nothing has been attacked"* (calm, green-consistent) from
  *"empty because the system is not yet watching"* (a posture problem, III.2). Empty is a
  certainty statement, not a blank.

---

## PART V — INFORMATION ARCHITECTURE

Five surfaces plus a resident presence. Each answers one operator question.

- **[DECISION] V.1 — Home / Shield ("am I protected?").** The posture hero (III.1): one
  verdict, current mode, driver/service health, captured-file count, preservation window
  health (capacity used, oldest **protected**-hold expiry), any recorded platform descent
  (III.2a), and the coverage line (IV.4). **Posture only — no file paths** (VII.3.1). Amber
  recommendations surface here.
- **[DECISION] V.2 — Recovery ("bring my files back").** The hero flow (Part VI). Organized by
  the certainty ladder and by **incident** (X.6), not by a fabricated periodic timeline.
  Itemized recoverable data is shown here, behind elevation (VII.3.1, VII.4.1).
- **[DECISION] V.3 — Activity / Detections ("what happened?").** A detection timeline: proven
  encryption, capacity events, phantom convictions — each expandable to forensic depth within
  the constitutional projection (III.4). **Phantom convictions expand to the convicted
  process's identity and evidence count only — never to decoy identities (I.11).** Calm; a
  detection raises a toast, routine activity does not.
- **[DECISION] V.4 — Response / Policy ("what should the system do?").** Mode
  (AUDIT ↔ ENFORCE) with honest consequences and friction on adopting ENFORCE (VI.2);
  whitelist as honest "stop protecting" (I.6), added by picking an app whose identity is
  resolved at add-time, revocable, gated by fresh elevation (VI.3).
- **[DECISION] V.5 — Settings ("what am I willing to spend, and is everything healthy?").** The
  budget as a retention policy (I.7), with the honest note that narrowing/eviction is
  irreversible and widening is prospective only; service/driver status and version; recorded
  platform descents (III.2a); and **logs export that inherits every non-disclosure rule** —
  no phantom identities (I.11), no key material or preserved bytes (I.12); any path-bearing
  export is itemized data and therefore requires elevation (VII.3.1).
- **[DECISION] V.6 — Resident presence.** A system-tray verdict (color + distinct glyph, never
  color alone — VIII.4) reflecting **posture only** (VII.3.1), and the tiered notification
  layer (III.3). The tray is the always-there answer to III.1 without opening the app and
  without a prompt.

---

## PART VI — HERO FLOWS

### VI.1 Recovery (the emotional peak)
- **[PATTERN] VI.1.1 — Incident-centric, with a temporal anchor (see X.6).** Recovery is
  organized by detected destructive incident; each incident is anchored to its detection time
  ("restore state as of just before T") when the backend provides it (X.6). This gives the
  familiar "roll back to before the attack" mental model without pretending we hold periodic
  snapshots (we hold per-write captures and per-region originals). Until the incident data
  contract lands (X.6), the surface degrades to a flat, ladder-ranked, selectable list.
- **[PATTERN] VI.1.2 — Browse-and-pick and recover-all coexist.** "Recover every file in this
  incident" is one action; individual selection is always available.
- **[PATTERN] VI.1.3 — Rung-honest selection.** Each recoverable item shows its rung
  (DEFINITIVE / BOUNDED with pool-honest countdown, IV.1.2). The user always knows the
  strength of what they are about to restore.
- **[PATTERN] VI.1.4 — Preview before commit.** Before executing: what files, from what point,
  to where, and what happens to any conflict — with the explicit reassurance *"your current
  files are safe; recovery replaces only after byte-for-byte verification."*
- **[PATTERN] VI.1.5 — Loud, itemized, verified success report.** After executing: *"X of Y
  restored, byte-for-byte verified; Z declined (reason)."* Verification is the emotional
  payoff (I.3); it is shown, per item, as the kernel's returned status — never assumed, and
  never by exposing secret material (I.12).

### VI.2 Adopting ENFORCE (a consequential policy)
- **[PATTERN] VI.2.1 — Deliberate, reversible friction, with the exact consequence stated.**
  Switching to ENFORCE is a policy decision whose consequence is autonomous blocking. The UI
  states the consequence precisely: *a block fires on any of the Constitution's three triggers
  — proven encryption at first instance, preservation-capacity exhaustion, and phantom
  conviction (Const. V.1.2); a conviction itself is never a false accusation (it is
  cryptographic proof, Const. II.1.1), but legitimate bulk encryption is behaviorally
  indistinguishable by design and will be blocked unless whitelisted (Const. V.1.1)* — then
  confirms. AUDIT is the reversible default at onboarding, per Const. V.1 ("the posture for
  discovering the whitelist and minimizing false positives").

### VI.3 Whitelisting (a disarm switch, owned honestly)
- **[PATTERN] VI.3.1 — Named as exemption, gated by fresh elevation.** Adding a whitelist entry
  is presented as *"stop protecting this app"* (I.6). Because it disarms protection for a
  target, it is a consequential verb (VII.4.1) requiring fresh elevation; the entry is bound
  to verified creation-time identity (image path ∧ signature ∧ content hash) and is revocable.

### VI.4 First run / onboarding (the trust-forming moment)
- **[PATTERN] VI.4.1 — Onboarding is a hero flow, not a dialog.** For a product whose register
  is "calm confidence" (II.3), the install is where trust is won or lost. The flow:
  (1) **pre-empt the platform trust prompts** — explain the kernel-driver / SmartScreen
  warning *before* Windows shows it (VII.5.2), so a scary wall is not the first impression;
  (2) explain the two-asset protection in one plain screen (I.4) and show the coverage line
  once, in full (IV.4); (3) default to **AUDIT** with its honest meaning stated ("attacks are
  recorded and recoverable, not yet blocked"); (4) help the user pin the tray verdict icon
  (VIII.5). It nudges toward ENFORCE only after the system has been quiet enough to trust the
  whitelist (Const. V.1).

---

## PART VII — ARCHITECTURE & PLATFORM DECISIONS

### VII.0 On backend preconditions
Several decisions below are load-bearing for the frontend but are realized in the backend
(new endpoints, projected fields, transport hardening). They are stated here as **backend
preconditions** so the frontend build knows what it depends on; their **normative home is a
Constitution Part VII amendment and/or the backend design docs**, not this charter. Where a
precondition is not yet met, the dependent frontend decision degrades as stated (VII.3.3).

### VII.1 Client framework
- **[DECISION] VII.1.1 — WPF on .NET (LTS), shipped as a signed desktop app.** Chosen for
  frictionless named-pipe/Win32 interop, minimal attack surface (no embedded web engine in a
  security product), no forced MSIX/packaged-identity tax, mature data/timeline controls, and
  native Windows feel. WinUI 3 is the runner-up (reserved for if Win11 Fluent polish ever
  outranks interop simplicity). Web runtimes (Electron/Tauri) are rejected: an embedded
  browser is attack surface a security agent should not carry, and — motivation, **[VERIFIED]**
  — elevated WebView2 fails to initialize under Windows 11 Administrator Protection because
  its subprocesses de-elevate.
- **[DECISION] VII.1.2 — Target the current .NET LTS with the longest remaining support at
  ship.** A security product must not ship on a runtime near end of support. **[VERIFIED]**
  .NET 8 reaches end of support 2026-11-10; .NET 10 (LTS through 2028) is the target. Revisited
  each major release.
- **[DECISION] VII.1.3 — Fluent look via a maintained theme library.** Motivation (not yet
  [VERIFIED] here): WPF's built-in Fluent/ThemeMode is experimental and weaker on Windows 10,
  so a maintained Fluent library carries the native feel across the Win10/Win11 floor, with
  built-in Fluent treated as progressive enhancement. Confirm the current state of the
  built-in theme before relying on it.

### VII.2 Interop
- **[DECISION] VII.2.1 — A thin native `sarapi` interop layer owns the wire.** A small C library
  (reusing the existing `protocol.h`/`control.h` structs) encapsulates the binary pipe
  protocol and exposes a clean, typed ABI to the .NET app. The wire contract stays owned by
  the C backend; the app never hand-marshals fragile struct layouts.

### VII.3 Transport — a two-endpoint split, by data sensitivity
- **[DECISION] VII.3.1 — Reads split into posture (session-readable) and itemized (elevated).**
  **[VERIFIED]** a standard-user (Medium-IL) process run by an admin holds a *filtered* token
  in which the Administrators SID is deny-only, so the current SYSTEM+Administrators-only
  control pipe refuses it — any read there forces a UAC prompt, defeating the glanceable
  posture (III.1). **Precondition:** the backend adds a **posture-only read endpoint** carrying
  mode, driver/service health, counts, capacity, oldest-protected-hold expiry, recorded
  descents, and a **redacted** event stream (event class + timestamp + convicted-process
  identity; **never** file paths, provenance, key identifiers, or decoy identities — I.11,
  I.12), with a DACL granting **read only** to the interactive logon session (note
  `FILE_APPEND_DATA` == `FILE_CREATE_PIPE_INSTANCE`: grant read, not write). **Itemized reads**
  — the recoverable catalog, the preserve list, per-file event detail — are user-data metadata
  *and* defensive-posture intelligence (which files are DEFINITIVE/BOUNDED/uncovered, and when
  windows expire), so they are served **only over the elevated control pipe**, alongside the
  consequential verbs that already require elevation (VII.4.1). Rationale: "the UI is not a
  security boundary" (I.10) governs the *app*; the endpoint DACL is a *backend* boundary and
  absolutely is one.
- **[DECISION] VII.3.2 — Transport hardening (backend precondition, VII.0).** The service must:
  raise max instances and serve asynchronously (a resident GUI plus a push channel must not
  head-of-line-block behind the CLI on a single synchronous slot); create the server with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` and treat a pre-existing name as tamper (anti-squat); and
  **authorize by token, not by caller identity** — a consequential verb is gated on
  `ImpersonateNamedPipeClient` + a genuinely elevated token, never on "the caller looks like
  our GUI" (PID / image name are spoofable). Normative home: Constitution Part VII / backend
  design docs.
- **[DECISION] VII.3.3 — Push/event channel + persisted journal (backend precondition, VII.0).**
  The service already receives driver detections but does not re-publish them. To answer "what
  happened while I was away" and raise real-time toasts, it gains a subscribe channel on the
  **posture-only** endpoint carrying the redacted event stream (VII.3.1), and an append-only,
  integrity-checked, bounded event journal (subject to I.11/I.12 redaction). The frontend
  degrades to polling if the channel is unavailable.
- **[DECISION] VII.3.4 — Minimal new verbs and derived values.** A `STATUS` read (posture, for
  III.1) is added. Whitelist add/remove (already in the protocol) are surfaced, with an
  add-time identity-resolve helper (given a chosen executable, compute image path + signature
  subject + content hash). Preserve-hold expiry countdowns are **derived client-side** from the
  projected capture time + budget and the pool `status` (both already projected,
  Const. III.1.3) — no new field where a value is derivable. **Named preconditions for the
  incident model (X.6):** the catalog projection currently carries no capture timestamp and no
  convicted-actor identity; incident grouping and the Oracle-side temporal anchor require the
  backend to add those, within the constitutional projection discipline (III.1.3).

### VII.4 Elevation model
- **[DECISION] VII.4.1 — Standard-user app; fresh elevation per consequential action; no
  resident elevated broker.** The main app and tray run as a standard user and read the
  posture endpoint without a prompt (VII.3.1). Consequential verbs (recover, mode, whitelist,
  budget) and itemized reads route to the SYSTEM service over the hardened control pipe and
  require a **fresh interactive elevation consent per action** (the COM Elevation Moniker is
  the Microsoft-blessed primitive). **[VERIFIED]** a long-lived elevated helper conflicts with
  the Windows 11 Administrator Protection just-in-time model and is hijackable under classic
  same-desktop UAC — precisely the local adversary this product defends against — so it is
  rejected. The standard-user-main + isolated-elevation split is Microsoft's own guidance.

### VII.5 Signing and packaging
- **[DECISION] VII.5.1 — Two distinct trust chains, both started early (pointer to the packaging
  workstream).** **[VERIFIED]** the user-mode app is Authenticode-signed (Azure Trusted Signing
  is acceptable for it), but the **kernel driver cannot** be — it requires an **EV code-signing
  certificate + Windows Hardware Developer Program attestation/WHQL + Microsoft co-signing**,
  and a **Microsoft-allocated altitude** (the committed 385000 is a placeholder per HANDOFF).
  EV enrollment and altitude allocation have lead time and gate the whole product. This clause
  is a pointer; the packaging workstream (HANDOFF) is the system of record.
- **[PRINCIPLE] VII.5.2 — First-run trust is designed, not assumed.** A newly signed security
  binary can still trip SmartScreen until reputation accrues, and EV no longer buys automatic
  reputation. Onboarding (VI.4.1) pre-empts the SmartScreen / driver-trust prompts rather than
  letting a scary wall be the user's first impression (contradicting II.1).

### VII.6 App lifecycle, version skew, and localization
- **[DECISION] VII.6.1 — On protocol/version mismatch, degrade to honesty, never guess.** A wire
  handshake version exists (HANDOFF). If the app and service/driver protocol versions do not
  match, the UI shows posture honestly ("management unavailable — component version mismatch")
  and disables verbs it cannot safely issue; it never sends a command it cannot frame
  correctly.
- **[DECISION] VII.6.2 — Updates are signed with the same Authenticode chain (VII.5.1)** and do
  not silently widen protocol or claims; a claims-affecting string change re-enters X.1.
- **[DECISION] VII.6.3 — Localization is first-class but claims-gated (IV.3.3).** UI strings are
  externalized for translation; certainty-ladder labels live in the design system; any
  translated claim string is re-cleared per X.1 before it ships.

---

## PART VIII — VISUAL & INTERACTION LANGUAGE

- **[PRINCIPLE] VIII.1 — "Trust-bank" aesthetic.** Calm, confident, precise. Neutral and green
  dominate screen time; amber and red are rationed (III.2). No scareware, no upsell (III.8).
- **[DECISION] VIII.2 — Windows-native Fluent, light/dark, honoring the system accent.** The
  product should feel like it belongs beside Windows Security, not like a foreign console.
- **[PATTERN] VIII.3 — The certainty ladder is the signature visual system.** One consistent
  color+icon+label triad (IV.2) is the most-reused component in the product.
- **[DECISION] VIII.4 — Accessibility floor.** Meaning is carried by **color + icon + label
  together**, everywhere the ladder appears, including the compact tray glyph (distinct shapes,
  not tint alone) — so color is never the sole channel. The palette is validated under
  deuteranopia/protanopia (amber/green is a known confusion pair) and for non-text contrast;
  the UNRECOVERABLE rung is designed so it is not mistaken for a "disabled/inactive" gray.
- **[DECISION] VIII.5 — Notification behavior respects Windows reality.** Toasts are sent from
  the standard-user tray process. A decision the system genuinely needs, and any integrity
  halt (III.2a), foregrounds a window and does not depend solely on a toast, because Focus
  Assist / Do Not Disturb can suppress toasts. Onboarding notes that a new tray icon may be
  hidden in the Windows 11 overflow and how to pin it. *(Confirm the current elevated-process
  toast constraints against Windows before finalizing the sender architecture — the tray is
  standard-user regardless, so this is design-robust.)*

---

## PART IX — FRONTEND BOUNDARIES & NON-GOALS

- **[BOUNDARY] IX.1 — No fleet/remote console this release.** The backend is local-only; the
  frontend is a single-endpoint app. Multi-endpoint management is a future product, and the IA
  is drawn to evolve into it without contradiction, not to fake it now.
- **[BOUNDARY] IX.2 — The UI is not a security boundary (Const. IX.1).** It is not presented,
  hardened, or trusted as one. Consequential authority lives in the SYSTEM service behind
  token-based authorization (VII.3.2), and the session-readable endpoint is posture-only
  (VII.3.1), not in the app.
- **[NEGATIVE] IX.3 — No detection knob is ever surfaced (Const. 0.5).** The only user control
  is the budget (I.7). No gate threshold, cipher battery, or capture trigger is ever exposed as
  a setting, "advanced" or otherwise.
- **[NEGATIVE] IX.4 — No telemetry-as-dark-pattern.** No engagement mechanics, no upsell, no
  fear metrics. Any diagnostics are opt-in, plainly described, and never a monetization
  surface.

---

## PART X — OPEN ITEMS (closed by evidence, not taste)

- **[OPEN] X.1 — Claims pre-clearance.** Final headline/marketing strings for the DEFINITIVE
  rung and the coverage line (IV.3, IV.4), in every shipped locale (IV.3.3), are reviewed
  against the advertising-substantiation standard before any public dissemination. *Closes by:*
  legal review of the scoped mechanism strings.
- **[OPEN] X.2 — Read-endpoint DACL shape and multi-session semantics.** The exact principal
  (interactive logon SID vs a dedicated group), mandatory-label, and read-not-write construction
  of the posture endpoint (VII.3.1); and, under fast user switching / multiple interactive
  sessions, who sees the tray and as whom. *Closes by:* implementing and testing that a
  standard-user tray reads **posture** with no prompt, **cannot** obtain any file path,
  provenance, hash, key id, or per-item entry, and cannot invoke any mutating verb.
- **[OPEN] X.3 — Remote-origin incident labeling.** Whether the driver can cheaply flag a
  destructive write as remote/network-originated so the UI can label an incident "outside the
  Oracle's reach" (IV.4) rather than silently degrading. *Closes by:* a backend feasibility
  check; until then the coverage line is stated generally.
- **[OPEN] X.4 — Self-protection posture surfacing.** How the UI reflects the PPL/ELAM
  self-protection unit (Const. VII; HANDOFF Unit 5) once present, beyond the descent/halt
  mapping already fixed in III.2a. *Closes by:* that unit landing.
- **[OPEN] X.5 — Deployment footprint target.** Self-contained WPF is large and non-trimmable;
  framework-dependent + a bundled runtime installer is smaller. *Closes by:* measuring the
  actual installer/idle-memory size against the trust/footprint goal (see X.7).
- **[OPEN] X.6 — Incident definition and its data contract.** The grouping key for an "incident"
  (candidate: convicted-actor identity + temporal adjacency over journal events) and its
  degraded form when the journal is absent (v1 polling), plus the wire additions VII.3.4 names
  (catalog capture time; per-event convicted-actor identity). *Closes by:* the journal schema
  landing and a VM-verified grouping over a multi-file campaign. Until then, VI.1.1 degrades to
  a flat ladder-ranked list.
- **[OPEN] X.7 — Performance and interaction budgets.** The one-second verdict (III.1) is fixed;
  resident-tray idle memory/CPU, list virtualization thresholds for large catalogs, and the
  keyboard/focus map (III.9) get concrete numbers. *Closes by:* a performance pass on
  representative incident sizes.

---

## PART XI — CONFORMANCE CHECKLIST

A frontend change conforms only if:

1. It does not claim more certainty than the Constitution proves (Part I; IV.3).
2. Recoverability is shown by rung, with color+icon+label; BOUNDED items show expiry
   **honestly by pool** (protected = firm date, probation = "may yield sooner") (IV.1;
   I.4a; VIII.4).
3. The coverage boundary is visible wherever DEFINITIVE could be mistaken for universal
   (IV.4).
4. No operator action can silently destroy present data; recovery previews and verifies
   (III.6; VI.1.4–5).
5. Consequential verbs **and itemized reads** are gated on fresh elevation and token-based
   authorization; the session-readable surface is **posture-only** and requires no prompt
   (VII.3–VII.4).
6. Modes and whitelisting are presented with their honest constitutional meaning, and the
   ENFORCE consequence names all three triggers and the "conviction is proof, bulk
   encryption is indistinguishable" truth (I.5, I.6; VI.2.1, VI.3).
7. No detection knob is exposed; only the budget is settable (IX.3; I.7).
8. Posture defaults to calm; red is reserved for actually-broken and integrity halts;
   notifications are tiered and Windows-reality-aware (III.2, III.2a, III.3; VIII.5).
9. Meaning survives without color; the UI is keyboard- and screen-reader-navigable
   (III.9; VIII.4).
10. No scareware, no upsell, no fabricated urgency anywhere (III.8; IX.4).
11. **No phantom identity is ever disclosed** on any surface, event, notification, or
    export, at any privilege (I.11).
12. **No key material, IV, verification tag, or preserved plaintext** is ever displayed,
    requested, or exported (I.12).
13. No evidence field beyond the constitutional projection is shown or required; any such
    field is [OPEN] pending amendment (III.4).
14. An integrity halt is red + modal (never only a toast); a platform descent is a stated
    fact, amber only where actionable (III.2a).
15. Any load-bearing outside-world claim added to this document carries [VERIFIED] or is
    written as motivation (0.3).

---

> This charter is subordinate to `CONSTITUTION.md` and is maintained alongside it. The
> pixel-level design system it authorizes lives in the Claude Design project and the
> component library (whose authority is defined by the packaging/design workstream in
> HANDOFF); this document fixes what that system must always obey.
