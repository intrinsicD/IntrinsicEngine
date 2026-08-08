---
id: BUG-145
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
contracts:
  - geometry.element-domain-sources
---
# BUG-145 — Editor geometry operations report Applied from written counts

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
- [ ] Return `NoChange` with an actionable reason when nothing changed, and do
      not create an undo-history entry for a no-op.
- [ ] Keep the existing failure statuses and diagnostics unchanged.
- [ ] Surface the change count in each result so the UI can explain the outcome.

## Tests

- [ ] For each operation, add a contract test that drives a genuinely no-op
      input and asserts a non-success status plus its explanatory diagnostic.
- [ ] For each operation, assert a normal run still reports `Applied` with a
      non-zero change count.
- [ ] Assert that a no-op run leaves no command-history entry.
- [ ] Default CPU gate stays green.

## Docs

- [ ] Extend the editor command-status documentation with the per-operation
      changed-count rule that `BUG-140` recorded for denoise.

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
