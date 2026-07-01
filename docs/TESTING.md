# TESTING.md — Verification Strategy for LLM-Driven Development

> **What this is.** A standing guide for **how** to verify software you (an LLM) build —
> at every stage, from a single edit to a deployment. It is the companion to
> DEVELOPMENT_WORKFLOW.md: that document governs *when* verification happens (its Phase 5,
> "demonstrate correctness with evidence"); this one governs *how* you produce that evidence
> and how far up the ladder to climb.
>
> **Project-agnostic.** Parts 0–6 carry no assumptions from any one codebase and are meant
> to be copied unchanged into any LLM-driven project. The Appendix is the only
> project-specific section; replace it per project.
>
> **Vocabulary and proportionality** follow DEVELOPMENT_WORKFLOW.md §0: **[INVARIANT]** must
> always hold, **[NEGATIVE]** must never be done, **[DEFAULT]** unless you have a stated
> reason, **[GUIDELINE]** is calibrated judgment. Scale verification to the task — the two
> opposite failures, **under-verifying** (asserting success) and **over-verifying**
> (ceremony), are equally wrong.

---

## PART 0 — THE ONE RULE

**[INVARIANT] 0.1 — Verify by observation, never by inspection.** A change is unverified
until you have **run the artifact and read the actual result**. "The code looks correct" is
not verification; it is the single most common failure mode — the *trust-then-verify gap*: a
plausible-looking implementation that does not handle the edge case. Code can be
syntactically correct and semantically plausible and still not execute as expected; only
execution reveals it. Produce the evidence — the command you ran and what it returned, the
test output, the screenshot — and let **it**, not your reasoning, settle the claim.

**[NEGATIVE] 0.2** Never present an unverified change as done. Never present a proxy ("it
compiles," "it looks right") as if it were the real check ("it runs correctly").

---

## PART 1 — THE VERIFICATION GRADIENT

**[INVARIANT] 1.1 — Run the cheapest check that would catch the failure first; escalate only
as far as confidence requires.** Verification is a ladder ordered by cost and latency. Each
rung is slower and more expensive than the last; climb only as high as the change's risk
demands.

1. **Build / compile** — the exit code is the first signal. Code that does not build is not a
   change.
2. **Static checks** — type-check, lint, format. Cheap, deterministic, fully under your
   control. The strongest cheap feedback is *rules-based*: a linter states exactly which rule
   failed and why.
3. **Unit tests** — fast, isolated, deterministic. The bulk of coverage. Run the *single
   relevant test* in the inner loop; the whole suite at the boundary.
4. **Integration tests** — components together, across real seams (DB, filesystem, IPC).
5. **End-to-end / system** — the whole thing, driven as a user would.
6. **Environment-specific** — what cannot run in the fast tier: real hardware, a VM/emulator,
   a kernel, staging. Slow, expensive, sometimes destructive (a crash). Reserve it for what is
   *irreducibly* environment-dependent.
7. **Progressive delivery + production observability** — canary / blue-green and monitoring on
   real traffic are the final verification, where applicable.

**[GUIDELINE] 1.2 — Most confidence comes from rungs 1–3.** The classic shape is the **test
pyramid** (many unit, fewer integration, fewest e2e); for I/O-heavy code the **testing
trophy** (a heavier integration middle) fits better. The anti-pattern is the **ice-cream
cone** — most weight on slow manual/e2e at the top, little underneath — which is slow, flaky,
and finds bugs late and expensively. Push verification *down* the ladder ("shift left"): the
lowest rung that gives confidence is the right one.

**[INVARIANT] 1.3 — A test you cannot trust is worse than no test.** Verification is only as
good as its determinism. A test must be **hermetic** (it sets up and tears down everything it
needs; it does not depend on ambient state, wall-clock time, network, or another test's order)
and **deterministic** (same input → same verdict, every run). A *flaky* test — one that passes
and fails on the same code — corrodes the whole suite: once people learn to re-run or ignore
it, every real failure it would have caught is lost. Mature suites hold flakiness near zero (on
the order of 0.1%); as it approaches 1%, tests stop being believed. Wrap the clock, stub
external services, never `sleep` to synchronize, and quarantine a flake rather than letting it
erode trust.

---

## PART 2 — THE INNER LOOP (after every change)

**[INVARIANT] 2.1 — Close the loop on yourself.** After a coherent set of edits, run — without
waiting for the user — the checks you control: **build, type-check, lint, and the relevant
tests.** Read the result; fix; repeat until green. Without a check you can run, "looks done"
is the only signal available, and the user becomes your verification loop — every mistake
waits for them to notice it. Give the loop something that produces a pass or fail and it
closes on its own.

**[INVARIANT] 2.2 — Regression safety.** A fix is not done if it breaks something that worked.
After a change, re-run the **existing** suite, not only your new test, and treat any
newly-failing test as a failure of *your change*. (Agentic-coding benchmarks encode this as
two invariants: the new test must pass **and** the previously-passing tests must stay green.)

**[GUIDELINE] 2.3 — Keep the inner-loop check cheap, or it will not be run.** Latency decides
how often verification actually happens: a slow or costly default check gets skipped under
pressure. Keep the per-edit loop to fast, free, deterministic checks (build, type-check, lint,
the single relevant test); push slow or expensive checks (the full suite, e2e, anything that
spends real money or wall-clock minutes) to the boundary, not the inner loop. The default check
you run after every edit should never be the one that hurts to run.

---

## PART 3 — TESTS AS YOUR CONFIDENCE MECHANISM

**[DEFAULT] 3.1 — Write the test that would catch your own mistake.** A test is not a
formality; it is how you earn confidence in code you cannot otherwise prove. The strongest
self-correcting patterns:
- **Failing-test-first.** To fix a bug, first write a test that reproduces it (confirm it is
  *red*), then fix until *green*. To build a feature, TDD — red → green each cycle — gives
  unambiguous feedback; it is the single strongest pattern for agentic coding.
- **Differential / reference testing.** When you write a new fast or optimized implementation,
  test it against a trusted slow reference and assert they agree across inputs. This is how a
  windowed or streaming rewrite is proven *equal to* the whole-buffer original — the test
  catches the off-by-one the author cannot see by reading.

**[NEGATIVE] 3.2** Do not edit the test to make it pass instead of fixing the implementation.
Commit the test first as a diffable safety net. A test that cannot fail is theater.

**[NEGATIVE] 3.3 — Do not mock away the thing under test.** A test whose mocks do not match the
real implementation passes while the system breaks, and the mocks drift silently as the code
evolves — cheap green that proves nothing. Mock the *boundary* (slow or external dependencies),
never the behavior you are trying to verify. (Over-mocking is a measured failure mode of
autonomous coding agents specifically.)

**[GUIDELINE] 3.4 — Do not grade your own exam.** You can satisfy weak tests you wrote
yourself, and overfit to them. For anything consequential, add an **independent check** — a
fresh-context subagent prompted to *refute* the result, a second reference, or a deterministic
gate — so the agent doing the work is not the only one grading it.

---

## PART 4 — THE VERIFICATION BOUNDARY

**[INVARIANT] 4.1 — When you cannot verify locally, say so precisely.** Some changes need an
environment you do not have: hardware, a VM, a kernel, staging, an external service. Local
green establishes *local* acceptance; it does not, by itself, establish downstream or
integration correctness. Therefore:
- **Maximize what you verify locally.** Architect so the risky logic can run in the fast tier
  and prove it there — host-test the pure algorithm even when the integration is kernel-only.
- **State the boundary explicitly.** Name what was verified and how, what was **not**, and the
  exact commands or steps the next stage (a human, a VM run, CI) must execute.

**[NEGATIVE] 4.2** Never silently present an environment-deferred change as fully done.
"Implemented and host-tested; the driver path is unverified — run `<x>` in the VM" is honest;
a bare "done" is not.

---

## PART 5 — PROPORTIONALITY & TIMING

**[GUIDELINE] 5.1 — Match verification depth to risk and reversibility.** A typo, a log line,
a rename: a build/lint pass is enough. A new algorithm, a security-relevant path, a multi-file
change: differential tests, the full suite, and an end-to-end check. Each rung trades setup
for attention — spend it where a bug would be costly or hard to reverse.

**[INVARIANT] 5.2 — Earn the green before you claim done or commit.** Verification precedes the
success claim and the commit, never the reverse. The canonical loop is **Explore → Plan →
Implement (write tests, run the suite, fix failures) → Commit** — tests are green *before* the
commit. Treat green local checks as the *minimum* bar, not proof of correctness; gate "done"
on observed evidence even when final validation is downstream.

---

## PART 6 — THE TWO-TIER PATTERN (most real systems)

Most systems split into a **fast tier** — builds and runs in-process on the dev host (pure
logic, unit and integration tests; milliseconds to seconds; fully under your control) — and a
**slow / real tier** — needs the actual environment (hardware, a VM, a kernel, a cluster;
seconds to minutes; sometimes destructive). The strategy:

- **Maximize the fast tier.** Architect so the risky logic is host-testable — a pure core
  behind a thin environment shell — and prove correctness there.
- **Minimize the slow tier.** Reserve it for what is genuinely environment-dependent: wiring,
  real I/O, timing, the behaviors only the real platform exhibits.
- **When the slow tier fails, you need a runbook.** A crash in the real tier (a BSOD, a hung
  VM, a failed deploy) is expensive to diagnose; keep a **debugging runbook** for it so the cost
  is paid once, not every time. That runbook is necessarily project-specific — it lives beside
  this methodology, not inside it (this project's is named in the Appendix).

**[GUIDELINE] 6.1** The two-tier split is also a *design* pressure, not just a testing one: the
more logic you can pull into the fast tier, the more of the system you can prove cheaply and
deterministically, and the smaller the irreducible, expensive, environment-only residual
becomes.

---

## APPENDIX — PROJECT INSTANTIATION (example: semantics-ar)

*This is the one project-specific section; replace it per project.*

- **Fast tier — the engine (host-built C, proven here).** `cmake --build build --config Debug
  --target <test>` then run the test exe, or `ctest -C Debug` for the suite. Crypto, gate,
  preserve, and recovery logic is proven on the host. Example of Part 3.1: the windowed-decrypt
  rewrite is validated by a differential test asserting **chunked == whole-buffer** across
  every cipher mode, geometry, and policy — the test caught a real reset-origin bug the author
  could not see. Run the relevant test after each engine change; the full suite before calling
  an engine change done.
- **Slow tier — the minifilter driver (kernel; VM only).** The driver builds only under the
  WDK/EWDK and runs only in the Hyper-V `SarTarget` VM; end-to-end behavior is exercised by
  `scripts/vm_verify_coverage.ps1` (the phased coverage regression). Driver changes are
  **host-unverifiable** — state that boundary (Part 4) and verify them in the VM.
- **When the VM tier crashes — DEBUGGING.md** (the kernel-dump runbook).
- **The boundary, by construction:** engine = host-proven; driver = VM-deferred. Always name
  which side a change touched, and what remains to verify.

---

*Grounding (independent research, verified 2026-07).*

*LLM/agent verification — Anthropic, Best practices for Claude Code
(<https://code.claude.com/docs/en/best-practices>): "show evidence rather than asserting
success," the *trust-then-verify gap*, rules-based feedback, TDD as the strongest pattern,
independent-subagent grading; Building agents with the Claude Agent SDK
(<https://www.anthropic.com/engineering/building-agents-with-the-claude-agent-sdk>) and
Building Effective Agents (<https://www.anthropic.com/research/building-effective-agents>):
gather-context → act → verify → repeat, "ground truth from the environment." CodeHalu
(<https://arxiv.org/pdf/2405.00253>): execution-based detection of code hallucination.
SWE-bench Verified: the FAIL_TO_PASS / PASS_TO_PASS regression invariants. Over-mocking as an
agent failure mode (arXiv 2602.00409). Differential testing as a cross-referencing oracle
(<https://en.wikipedia.org/wiki/Differential_testing>). Agent test-pyramid / "the default test
command should never spend money" (Block Engineering,
<https://engineering.block.xyz/blog/testing-pyramid-for-ai-agents>).*

*Classical testing-strategy consensus — Martin Fowler: "TestPyramid"
(<https://martinfowler.com/bliki/TestPyramid.html>), "Practical Test Pyramid," "SelfTestingCode,"
"Non-Determinism in Tests," "CanaryRelease." Kent C. Dodds, "The Testing Trophy"
(<https://kentcdodds.com/blog/the-testing-trophy-and-testing-classifications>). "Software
Engineering at Google," ch. 11 (Testing — hermeticity, ~80/15/5) and ch. 20 (Static Analysis /
Tricorder); Google Testing Blog ("Just say no to more end-to-end tests," "Flaky tests at
Google"). Google SRE (canary, monitoring the four golden signals) and DORA's four key metrics.
Microsoft, "Shift left to make testing fast and reliable."*
