---
id: BUG-141
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Editor diagnostic classification, scoping, and lifetime only. No geometry
  element-domain source, property, support-radius, parameterization, or method
  integration surface changes.
---
# BUG-141 — Editor geometry-processing diagnostics are mislabeled, duplicated, unscoped, and unactionable

## Goal
- Make editor geometry-processing diagnostics report the right severity, appear
  only in the panel that produced them, expire, and say enough to act on.

## Non-goals
- No fix for the underlying job-dispatch stall (`BUG-138`) or the import
  topology defect (`BUG-137`).
- No new diagnostics UI window or notification system.
- No change to the `EditorDiagnosticCode` enum beyond what these four defects
  require.

## Context
Four distinct defects observed in one `ExtrinsicSandbox` session, all on the
shared editor geometry-processing diagnostic surface:

- **Mislabeled severity.** A normally queued async job is reported under a
  failure code: `GeometryProcessingFailed: Mesh simplify CPU job queued
  (job 4:1).` Queuing is not a failure.
- **Duplicated.** Each queue event is emitted **twice** — after three
  operations the header showed six lines for three jobs.
- **Unscoped.** Those six simplify/subdivide/remesh lines then appeared in the
  header of **every** mesh domain window, including the K-Means and
  `Parameterize (UV)` panels, which had nothing to do with them.
  `DrawDomainWindowHeader` / `DrawDiagnostics` render the shared
  `model.Processing.Diagnostics` regardless of which panel produced the entry.
- **Never expire, cannot be cleared.** The lines persisted for the rest of the
  session with no dismissal affordance. After three operations they consumed
  roughly 40% of a default-sized domain panel (see also `UI-049`).
- **Unactionable content.** `Mesh / Processing / Parameterize (UV)` reports
  `Status: GeometryProcessingFailed (invalid input)` and "Mesh parameterization
  solver rejected the selected mesh or config." without saying *why*. For the
  observed case the actionable fact — the mesh had roughly 7000 connected
  components — is computable and was not reported.
- Impact: the diagnostic surface actively misleads. A user reading a
  parameterization panel sees stale simplify "failures" and a solver rejection
  with no cause.
- Owner: `runtime` editor workspace snapshot/diagnostic model plus the app-side
  `DrawDiagnostics` presentation in `Sandbox.DomainPanels` /
  `Sandbox.MeshProcessingPanels`.

## Required changes
- [ ] Classify a queued async job as a pending/informational diagnostic, not
      `GeometryProcessingFailed`.
- [ ] De-duplicate diagnostic emission for a single command.
- [ ] Scope diagnostics to the originating operation/panel so unrelated windows
      do not display them.
- [ ] Give diagnostics a lifetime (superseded by the next run of the same
      operation, or explicitly dismissible).
- [ ] Include the rejection cause in parameterization failures (at minimum
      connected-component count and boundary-loop count when the solver rejects
      the mesh).

## Tests
- [ ] Add a contract test asserting a queued job produces a pending-class
      diagnostic, not a failure-class one.
- [ ] Add a test asserting one command yields exactly one diagnostic entry.
- [ ] Add a test asserting a diagnostic raised by one operation is absent from
      another operation's model.
- [ ] Add a test asserting a parameterization rejection carries a structured
      cause.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Document diagnostic severity classes, scoping, and lifetime in the owning
      runtime editor doc.

## Acceptance criteria
- [ ] No queued job is reported under a failure code.
- [ ] One command produces one diagnostic.
- [ ] Diagnostics appear only in the panel that produced them.
- [ ] Diagnostics expire or can be cleared.
- [ ] A parameterization rejection names its cause.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'EditorWorkspaceSnapshots|GeometryProcessing|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Suppressing diagnostics to fix the noise instead of scoping and classifying
  them.
- Moving diagnostic classification into `app`; runtime owns the model.
