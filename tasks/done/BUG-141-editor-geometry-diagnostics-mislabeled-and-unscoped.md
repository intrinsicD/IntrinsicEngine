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

## Status

- Completed and retired on 2026-08-09.
- Completion commit: this retirement commit. Slice A landed in `72559336`.

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

## Progress — slice B landed 2026-08-09; the task closes

**Lifetime.** Half of the requirement was already true and worth naming rather
than rebuilding: the session stores exactly one `Last<Operation>Result` per
operation, so every outcome is superseded by the next run of the operation that
produced it. What was missing is the case where the user is not going to run it
again. `EditorMethodResultSinks::DismissResult` takes an
`EditorGeometryProcessingResultSlot` and the session resets that one slot;
panels drop their own copy at the same call site so the line does not reappear
on the next prepared frame. It is one enum and one sink rather than a
per-operation dismissal sink because the session's only reaction is to reset
the matching optional. Dismissal is per-slot: clearing simplify must not clear
denoise, which the contract test asserts. The sink obeys the same
attachment-epoch rule as every other result sink.

**Rejection cause.** `ParameterizeMesh` now fills
`ParameterizeResult::Rejection` with the connected-component and boundary-loop
counts whenever a run did not succeed. It is computed once in the unified
dispatch, so all three strategies get it, and only on the failure path, so the
success path pays nothing. The count needed a const-mesh flood fill, which is
`MeshRepair::LabelConnectedComponents` — the same labelling
`ComputeConnectedComponents` already ran, minus the `v:component`/`f:component`
publication a diagnostic has no business doing. The editor copies the counts
onto `EditorParameterizationResult::Rejection` and repeats them in the message,
and distinguishes the two cases honestly: when the counts violate disk topology
it says so and names the fix, and when they do not (a punctured torus has one
component and one loop) it says the failure was geometry or config rather than
claiming the topology explains it. `Evaluated` stays false when no mesh reached
the solver, so a stale-entity or bad-config rejection does not report counts it
never took.

**One duplication slice A missed.** The live capture for the rejection cause
showed the new sentence printed twice in the `Parameterize (UV)` window: once
in the panel header and once in `Last run diagnostics`. That is the same mirror
slice A removed, in the one panel that does not use `DrawDomainWindowHeader` —
`BuildEditorParameterizationViewModel` copied the last result's message onto
`EditorParameterizationViewModel::Message`, and the panel renders both. The
mirror is gone; the header keeps only what is true of the view itself (no
selection, stale entity, unusable `v:texcoord`, unavailable topology), and the
outcome stays on the result. Pinned by
`ViewModelDoesNotMirrorTheLastResultMessage`.

## Progress — slice A landed 2026-08-09

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

Not attempted in slice A, and the reason the task stayed open at the time: the
dismissal affordance and the parameterization rejection cause, both of which
slice B above now lands. One test named in
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
      duplication was a double render in `app`, not a double emission. (Slice B)
      One more mirror surfaced in the `Parameterize (UV)` panel, the only one
      that does not use `DrawDomainWindowHeader`; it is removed at the runtime
      view model.
- [x] Scope diagnostics to the originating operation/panel so unrelated windows
      do not display them. (Slice A)
- [x] Give diagnostics a lifetime (superseded by the next run of the same
      operation, or explicitly dismissible). (Slice B) Each outcome was already
      superseded by the next run of its own operation; slice B adds the
      explicit per-slot dismissal via
      `EditorMethodResultSinks::DismissResult`.
- [x] Include the rejection cause in parameterization failures (at minimum
      connected-component count and boundary-loop count when the solver rejects
      the mesh). (Slice B) `ParameterizeResult::Rejection` carries both counts;
      the editor copies them onto the result and repeats them in the
      message.

## Tests
- [x] Add a contract test asserting a queued job produces a pending-class
      diagnostic, not a failure-class one
      (`QueuedMeshJobIsNotReportedAsAProcessingFailure`).
- [x] Add a test asserting one command yields exactly one diagnostic entry.
      Recorded as covered by the two tests either side of it plus the live
      before/after pair, and not by a third assertion: see `## Progress` for
      why the obvious one could not fail. Slice B adds the one case that could
      fail, `ViewModelDoesNotMirrorTheLastResultMessage`.
- [x] Add a test asserting a diagnostic raised by one operation is absent from
      another operation's model
      (`OneOperationsFailureIsAbsentFromTheSharedProcessingModel`, plus the
      updated assertion in
      `MeshVertexNormalsCommandRejectsConflictingPropertyType`).
- [x] Add a test asserting a parameterization rejection carries a structured
      cause (`ParameterizationOperations.SolverRejectionCarriesStructuredTopologyCause`,
      plus `ParameterizationDispatch.RejectionCarriesConnectedComponentAndBoundaryLoopCounts`
      at the geometry seam and
      `SandboxEditorSession.DismissClearsOneGeometryProcessingResultSlot`
      for the lifetime half).
- [x] Default CPU gate stays green (slice A 4150/4150, slice B 4154/4154;
      expected GLFW/LSan skip in both).

## Docs
- [x] Document diagnostic scoping and the pending-is-not-a-failure rule in
      `src/runtime/README.md` (slice A). Lifetime is documented with slice B,
      which is what introduces it.

## Acceptance criteria
- [x] No queued job is reported under a failure code.
- [x] One command produces one diagnostic.
- [x] Diagnostics appear only in the panel that produced them.
- [x] Diagnostics expire or can be cleared.
- [x] A parameterization rejection names its cause.

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
