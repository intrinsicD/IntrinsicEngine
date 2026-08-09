---
id: BUG-145
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug145"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-09T10:05:00Z"
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# BUG-145 — Editor geometry operations report Applied from written counts

## Slice plan

Written before implementing. The audit lists 22 sites, but they do not share a
change signal: normals and curvature compare published values, topology
operations compare counts against the input, outlier removal already has a
rejected set, and UV regeneration writes a texcoord property. One patch over
all four would be a patch nobody can review, so the work is sliced by change
signal, not by file.

Every slice follows the same three steps `BUG-140` established for denoise, and
each slice keeps the default CPU gate green on its own:

1. Derive the terminal status from a changed quantity.
2. Return `NoChange` with an actionable reason before the history commit, so a
   no-op leaves no undo entry.
3. Surface the change count on the result so the UI can explain the outcome.

- **Slice A — vertex normals** (this slice). Six sites: the mesh, graph, and
  point-cloud commands and their three CPU-job publishers. The change signal is
  a value comparison against the `VertexNormalPropertyState` each path already
  captures for undo, so no new state is needed. Defers everything below.
- **Slice B — curvature and point-cloud outlier removal.** Curvature compares
  published scalar and direction values; outlier removal already computes
  `removal.RejectedIndices` and ignores it. Grouped because both are single
  operations with a signal already in hand.
- **Slice C — topology operations.** Nine sites across remesh, subdivide, and
  simplify (command, worker, publisher each). The signal is an output-versus-
  input topology delta; simplify additionally has a `CollapseCount` it computes
  and does not consult. Largest slice and last of the value-comparison work.
- **Slice D — UV regeneration.** Two sites, `RunUvRegenerationCpuWorker` and
  `ApplyUvRegenerationCommit`. Separate because the writeback path is shared
  with the parameterization lane and worth reviewing on its own.

## Progress — slice A landed 2026-08-09; the task stays open for slices B-D

All three vertex-normal families now compare the normals they are about to
publish against the ones already stored, and report `ChangedNormalCount`. A run
whose every normal already equals the recomputed value reports `NoChange` with a
message naming the vertex count it left alone, and returns before the history
commit, so recomputing normals twice in a row no longer stacks a second undo
entry that would restore identical values.

The comparison is exact, not epsilon-based. These paths publish the kernel's
own output into the same `v:normal` storage it was read from, so an unchanged
normal is bit-identical; an epsilon would have invented a tolerance the undo
snapshot does not share, and a value that differs by less than it would then be
committed while being reported as unchanged.

An absent `v:normal` property is a change by definition — every slot is newly
authored — so `HadNormal == false` reports the full written count rather than
comparing against nothing.

## Goal

- Make every editor geometry-processing operation that currently derives
  `Applied` from a written or processed count derive it from a changed count
  instead, so a run that alters nothing reports `NoChange` with a reason,
  matching the rule `BUG-140` established for mesh denoise.

## Non-goals

- No change to any processing algorithm, kernel, or default parameter.
- No redesign of the shared `EditorCommandStatus` enum; `NoChange` already
  exists and is already produced by `ToEditorCommandStatus`.
- No re-fix of mesh denoise, which `BUG-140` already closed and which is the
  reference pattern for this task.
- No new abstraction layer over result construction; each operation keeps its
  own result type.

## Context

- Owner: `runtime` editor geometry-processing operations, all in
  `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp`
  unless noted.
- `BUG-140` fixed mesh denoise: it reported `Applied` / `Success` with
  `moved: 0` because the status came from a slot-derived `WrittenCount` that is
  non-zero whenever the kernel runs at all. Its audit, required by that task,
  found the same shape in the sibling operations listed below. They were
  deliberately not folded into that fix, which was scoped to the reporting
  defect it reproduced.
- The audit, recorded here as the durable inventory:

  **Vertex normals** — status is decided purely from the kernel's own
  `RecomputeStatus`, and the reported count is a copied `WrittenCount`. No
  operation checks whether any normal actually differs.
  - `ApplyEditorMeshVertexNormalsCommand` (status at `Mesh.cpp:8282-8285`,
    count copied at `:1130`)
  - `PublishMeshVertexNormalsCpuJob` (`Mesh.cpp:4529`)
  - `ApplyEditorGraphVertexNormalsCommand` (`Mesh.cpp:9165-9168`, count at
    `:1206`)
  - `PublishGraphVertexNormalsCpuJob` (`Mesh.cpp:4579`)
  - `ApplyEditorPointCloudVertexNormalsCommand` (`Mesh.cpp:9332-9335`, count at
    `:1342`)
  - `PublishPointCloudVertexNormalsCpuJob` (`Mesh.cpp:4630`)

  **Curvature** — `ScalarWrittenCount` is `mean.size() + gaussian.size()`, a
  pure written count, and `Applied` follows unconditionally.
  - `ApplyEditorMeshCurvatureCommand` (`Mesh.cpp:8345-8348`, count at `:8282`)
  - `RunMeshCurvatureCpuWorker` (`Mesh.cpp:5278-5281`, count at `:5225`)

  **Topology operations** — output counts are reported but never compared
  against the input, so a remesh/subdivide/simplify that changed nothing still
  reads as `Applied`. `ApplyEditorMeshSimplifyCommand` even computes a
  `CollapseCount` (`Mesh.cpp:8880-8882`) and does not consult it.
  - `ApplyEditorMeshRemeshCommand` (`Mesh.cpp:8534`), `RunMeshRemeshCpuWorker`
    (`:5441`), `PublishMeshRemeshCpuJob` (`:5744`)
  - `ApplyEditorMeshSubdivideCommand` (`Mesh.cpp:8745`),
    `RunMeshSubdivideCpuWorker` (`:5548`), `PublishMeshSubdivideCpuJob`
    (`:5781`)
  - `ApplyEditorMeshSimplifyCommand` (`Mesh.cpp:8896`),
    `RunMeshSimplifyCpuWorker` (`:5614`), `PublishMeshSimplifyCpuJob` (`:5818`)

  **Point-cloud outlier removal** — has an explicit change signal it ignores:
  `removal.RejectedIndices` (`Mesh.cpp:9591`) may be empty while the status is
  still `Applied`.
  - `ApplyEditorPointCloudOutlierRemovalCommand` (`Mesh.cpp:9594-9595`)
  - `PublishPointCloudOutlierRemovalCpuJob` (`:3459`), worker (`:3416`)

  **UV regeneration**
  - `RunUvRegenerationCpuWorker` (`Mesh.cpp:7715`), `ApplyUvRegenerationCommit`
    (`:7749`)

- Reference pattern to follow, both already in this file: ICP registration maps
  history state through `ToEditorCommandStatus` (`Mesh.cpp:9837` sync,
  `:6647` job), and `BUG-140`'s denoise gate returns `NoChange` before
  committing so a no-op cannot leave an undo entry.
- Line numbers are from the `BUG-140` retirement commit and are a starting
  point, not an authority; re-locate each site before editing.

## Required changes

- [ ] For each operation above, derive the terminal status from a changed
      quantity: normals that actually differ, curvature values that actually
      differ, a topology delta, a non-empty rejected set, or a UV change.
      (Slice A done: all six vertex-normal sites compare against the
      `VertexNormalPropertyState` already captured for undo. Slices B-D own
      curvature, outlier removal, topology, and UV regeneration.)
- [ ] Return `NoChange` with an actionable reason when nothing changed, and do
      not create an undo-history entry for a no-op. (Slice A done for vertex
      normals; the gate also skips the dirty stamp, because a run that
      published nothing has nothing to re-extract.)
- [ ] Keep the existing failure statuses and diagnostics unchanged. (Slice A
      done: the gate sits after every failure return, so no kernel or
      validation failure changes shape.)
- [ ] Surface the change count in each result so the UI can explain the
      outcome. (Slice A done: `ChangedNormalCount` on all three normals
      results, in the success message, and in the three panels — which now
      render their counter block for `NoChange` too, since that is exactly
      when the counts are what explains the outcome.)

## Tests

- [ ] For each operation, add a contract test that drives a genuinely no-op
      input and asserts a non-success status plus its explanatory diagnostic.
      (Slice A: `VertexNormalRecomputeThatChangesNothingReportsNoChange` covers
      the three synchronous commands and
      `VertexNormalCpuJobThatChangesNothingReportsNoChange` the job publisher,
      which is a separate code path.)
- [ ] For each operation, assert a normal run still reports `Applied` with a
      non-zero change count. (Slice A: asserted in both new tests and in
      `MeshVertexNormalsCommandPublishesCanonicalNormalsForAllWeightings`,
      whose four-weighting loop turned out to be three no-ops on a flat
      triangle and now asserts that explicitly.)
- [ ] Assert that a no-op run leaves no command-history entry. (Slice A: both
      new tests compare `EditorCommandHistory::UndoCount()` across the no-op.)
- [ ] Default CPU gate stays green. (Slice A: 4156/4156, expected GLFW/LSan
      skip.)

## Docs

- [ ] Extend the editor command-status documentation with the per-operation
      changed-count rule that `BUG-140` recorded for denoise. (Slice A: the
      rule, the exact-comparison rationale, and the panel rule are in
      `src/runtime/README.md`; later slices extend the per-operation list.)

## Acceptance criteria

- [ ] No audited operation can report `Applied` while changing nothing.
- [ ] Every no-op result explains why nothing changed.
- [ ] No no-op run creates an undo-history entry.
- [ ] The complete default CPU gate passes.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditorMeshMethods|SandboxEditorUi|GeometryProcessing' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Changing an algorithm or its defaults to avoid the no-op instead of reporting
  it.
- Introducing a shared result-construction framework to hold the change count.
- Re-opening mesh denoise, which `BUG-140` already closed.

## Maturity

- Target: `CPUContracted`; no `Operational` follow-up is owed.
