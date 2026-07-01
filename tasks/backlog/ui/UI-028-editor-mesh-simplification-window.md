---
id: UI-028
theme: F
depends_on: [GEOM-014]
---
# UI-028 — Sandbox EditorUI mesh simplification window

## Status
- Implemented (2026-07-01) on branch `claude/ui-backlog-agentic-y3oap2`; the full
  C++23-module build/`ctest` gate is deferred to CI because this sandbox cannot
  bootstrap vcpkg (clang-18, no Clang 20 toolchain) — the same constraint under
  which the `GEOM-014` kernel itself landed.
- Landed as a single cohesive change mirroring the retired UI-024/025/026/027
  seam pattern: `SandboxEditorMeshSimplifyMetric` (ClassicalQEM / FA_QEM),
  `SandboxEditorMeshSimplifyCommand` / `...Result`, and
  `ApplySandboxEditorMeshSimplifyCommand` drive
  `Geometry::Simplification::Simplify` on the selected mesh, replace its
  `GeometrySources`, mark topology dirty, and are undoable through
  `EditorCommandHistory` via the existing `CommitMeshTopologyReplacement` seam.
  A `Mesh > Processing > Simplify` window exposes Metric, target face count,
  max error, boundary preservation, and the FA_QEM feature weights, and reads
  out the `Result` diagnostics (collapses, rejections, pins).
- Verified in-session: `check_layering --strict` (the new
  `runtime -> Geometry.Simplification` import edge is allowed),
  `check_test_layout --strict`, `check_doc_links`, `validate_tasks --strict`,
  and `check_task_policy --strict` all pass; adversarial diff review against the
  kernel/editor ground truth.
- Follow-up fix (2026-07-01, review feedback): the executor now forwards the
  selected mesh's `v:texcoord` into the scratch halfedge mesh before simplifying.
  `BuildHalfedgeMeshForTopologyEdit` (via `BuildMeshSoupFromGeometrySources`)
  carries only positions + topology, so without this the FA_QEM `PreserveUvSeams`
  default was silently ineffective and textured meshes could collapse across UV
  seams while reporting zero pinned seam vertices. Added
  `SandboxEditorUi.MeshSimplifyPreservesUvSeamsWhenTexcoordsPresent` (textured
  grid plane → `SeamVerticesPinned > 0`).
- Deferred to CI: `cmake --preset ci` + `ctest -R SandboxEditorUi`.

## Goal
- Add a `Mesh > Processing > Simplify` window to the promoted
  `Runtime.SandboxEditorUi` that decimates the selected mesh entity through an
  undoable runtime-owned command calling the geometry-owned
  `Geometry::Simplification::Simplify` kernel (including the `GEOM-014`
  `Metric::FA_QEM` feature-aware path), then defers renderer synchronization
  through geometry dirty tags — exactly the seam pattern proven by the retired
  `UI-024` (denoise), `UI-025` (remesh/subdivide), and `UI-026` (curvature).

## Non-goals
- No new geometry algorithm work; `GEOM-014` owns the kernel and metric.
- No UI ownership of renderer, asset, runtime, or geometry state — the window
  only emits commands/events to the owning systems.
- No async scheduling or progressive LOD; this is a synchronous CPU command.

## Context
- Owning subsystem/layer: editor/UI over `runtime`; UI must not own simulation,
  render, or asset state.
- Today `Runtime.SandboxEditorUi` lists `Simplification` as a surface-topology
  processing capability but provides no executor, unlike the denoise/remesh/
  curvature/outlier windows that already call their geometry kernels through
  runtime command seams.
- `GEOM-014` shipped the feature-aware `Metric` (default `FA_QEM`), feature/seam
  pin parameters, and `Result` diagnostics this window surfaces.
- Cites [`tasks/backlog/ui/RORG-031-ui-integration.md`](RORG-031-ui-integration.md);
  continues the `bcg_code_base` geometry-processing port into interactive
  Sandbox workflows.

## Required changes
- [x] Add a runtime-owned simplification command (mirroring the denoise/remesh
      command seams) that runs `Geometry::Simplification::Simplify` on the
      selected mesh, replaces its `GeometrySources`, and marks geometry dirty.
- [x] Add a `Mesh > Processing > Simplify` window exposing target face count /
      ratio, `Metric` (ClassicalQEM / FA_QEM), and the FA_QEM weights, plus a
      read-out of `Result` diagnostics (collapses, pins, rejections).
- [x] Route the command through `EditorCommandHistory` so it is undoable.

## Tests
- [x] Editor contract test that the simplify command reduces the selected mesh's
      face count and is undoable (CTest labels `unit;runtime` or the existing
      editor test labels). Added
      `SandboxEditorUi.MeshSimplifyCommandReducesFaceCountAndSupportsUndoRedo`
      and `...MeshSimplifyCommandFailsClosedForInvalidTargetsAndUnavailableKernel`
      to `tests/contract/runtime/Test.SandboxEditorUi.cpp` (run in CI).

## Docs
- [ ] Add the UI-028 entry to [`tasks/backlog/ui/README.md`](README.md) on
      retirement and update `docs/migration/nonlegacy-parity-matrix.md` if the
      executor closes a deferred workflow.

## Acceptance criteria
- [ ] The `Simplify` window executes the geometry kernel through a runtime
      command, is undoable, and surfaces the FA_QEM metric + diagnostics.
- [ ] No UI ownership of renderer/asset/runtime/geometry state.
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.

## Verification
```bash
test -f tasks/backlog/ui/UI-028-editor-mesh-simplification-window.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing new geometry algorithms under cover of this UI task.

## Maturity
- Target: `CPUContracted` — a runtime-owned editor command over the existing CPU
  geometry kernel, matching the retired UI-024/025/026 simplification peers. No
  `Operational` follow-up is owed.
