---
id: BUG-141
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug141"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-09T02:25:00Z"
contract_schema: 1
contracts: []
contract_review: >-
  Editor diagnostic classification, scoping, and lifetime only. No geometry
  element-domain source, property, support-radius, parameterization, or method
  integration surface changes.
---
# BUG-141 — Editor geometry-processing diagnostics are mislabeled, duplicated, unscoped, and unactionable

## Slice plan

Written before implementing, because the five required changes do not share a
mechanism.

**Slice A — classification, scoping, de-duplication.** The three defects that a
live session on 2026-08-09 reproduced directly while diagnosing `BUG-138`
(screenshot: `tasks/evidence/BUG-138/artifacts/refusal-before-fix.png`, which
shows `GeometryProcessingFailed: Mesh simplify CPU job queued (job 3:1).`
rendered twice, and `denoise-applies-on-the-same-entity.png`, which shows
simplify's diagnostics inside the Denoise panel).

**Slice B — lifetime and rejection cause.** A dismissal affordance and the
parameterization rejection cause (connected-component and boundary-loop
counts). Slice B needs a solver-side reason that does not exist yet, which is
why it is separate.

### Slice A design note — why scoping is relocation, not a tag

`## Forbidden changes` rules out suppressing diagnostics instead of scoping
them, so this is worth stating explicitly. Each per-operation entry in the
shared `EditorGeometryProcessingModel::Diagnostics` list is a *mirror* of
`Last<Operation>Result.Message`, which the owning panel already renders on its
own ("`Last simplify run: StaleEntity`" followed by the same message). The
mirror is what leaks into unrelated windows, because
`AppendDiagnostics(model.Diagnostics, model.Processing.Diagnostics)` folds the
shared list into every domain window's header.

Slice A therefore drops the mirror rather than tagging it with a scope. Nothing
is hidden: the message stays in the model on the operation's own result and
stays on screen in the panel that produced it. Tagging each entry with
`EditorGeometryProcessingAlgorithm` and filtering per panel would reach the
same end state with a second copy of information the model already carries per
operation, so it was rejected as the larger of two equivalent designs. If
review prefers the tag, it is a contained follow-up.

## Progress — slice A landed 2026-08-09; the task stays open for slice B

`BuildGeometryProcessingModel` mirrored every `Last<Operation>Result` whose
status was not `Succeeded()` into the shared
`EditorGeometryProcessingModel::Diagnostics` list, under
`EditorDiagnosticCode::GeometryProcessingFailed`. That single mirror caused two
of the reported defects at once: `EditorCommandStatus::Pending` is not
`Succeeded()`, so a queued job was announced as a failure, and
`BuildEditorDomainWindowModel` folds the shared list into every domain window's
header, so one operation's outcome was printed by all of them. The eleven
mirrors are gone; each outcome stays on its own result, which the owning panel
already renders.

The duplication was separate and lived in `app`: four panels called
`DrawDomainWindowHeader(model)` — which renders `model.Diagnostics`, the
superset — and then `DrawDiagnostics(model.Processing.Diagnostics)` again. The
second call is removed at all four sites (`Sandbox.MeshProcessingPanels.cpp`,
`Sandbox.DomainPanels.cpp`, and two in `Sandbox.MethodPanels.cpp`).

Live evidence, same build and mesh before and after
(`tasks/evidence/BUG-141/artifacts/`): a queued simplify that previously read
`GeometryProcessingFailed: Mesh simplify CPU job queued (job 3:1).` twice in the
header now shows no header diagnostic at all and reports
`Last simplify run: Pending`, then `Applied`; and the Denoise panel, which
previously carried two `Sandbox.MeshSimplify.CPU did not apply: …` lines it had
nothing to do with, is clean.

Not attempted here, and the reason the task stays open: the dismissal
affordance and the parameterization rejection cause. One test named in
`## Tests` was written and then deliberately dropped rather than shipped —
an assertion that the header list contains each processing diagnostic exactly
once. With the mirrors gone, a matching-domain window's processing diagnostics
list is empty in every reachable state, so the assertion could not fail and
would have been decoration. The de-duplication is proven by the before/after
screenshots instead, and by the removal of the second render call itself.

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
- [x] Classify a queued async job as a pending/informational diagnostic, not
      `GeometryProcessingFailed`. (Slice A) A queued job now raises no
      diagnostic at all and is reported as `Pending` on its own result, which
      is what the K-Means path already did via its `Status != Queued` guard.
- [x] De-duplicate diagnostic emission for a single command. (Slice A) The
      duplication was a double render in `app`, not a double emission.
- [x] Scope diagnostics to the originating operation/panel so unrelated windows
      do not display them. (Slice A)
- [ ] Give diagnostics a lifetime (superseded by the next run of the same
      operation, or explicitly dismissible). (Slice B)
- [ ] Include the rejection cause in parameterization failures (at minimum
      connected-component count and boundary-loop count when the solver rejects
      the mesh). (Slice B)

## Tests
- [x] Add a contract test asserting a queued job produces a pending-class
      diagnostic, not a failure-class one
      (`QueuedMeshJobIsNotReportedAsAProcessingFailure`).
- [x] Add a test asserting one command yields exactly one diagnostic entry.
      Recorded as covered by the two tests either side of it plus the live
      before/after pair, and not by a third assertion: see `## Progress` for
      why the obvious one could not fail.
- [x] Add a test asserting a diagnostic raised by one operation is absent from
      another operation's model
      (`OneOperationsFailureIsAbsentFromTheSharedProcessingModel`, plus the
      updated assertion in
      `MeshVertexNormalsCommandRejectsConflictingPropertyType`).
- [ ] Add a test asserting a parameterization rejection carries a structured
      cause.
- [x] Default CPU gate stays green (4150/4150, expected GLFW/LSan skip).

## Docs
- [x] Document diagnostic scoping and the pending-is-not-a-failure rule in
      `src/runtime/README.md` (slice A). Lifetime is documented with slice B,
      which is what introduces it.

## Acceptance criteria
- [x] No queued job is reported under a failure code.
- [x] One command produces one diagnostic.
- [x] Diagnostics appear only in the panel that produced them.
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
