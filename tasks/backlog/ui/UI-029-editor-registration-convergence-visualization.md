---
id: UI-029
theme: F
depends_on: [GEOM-055]
---
# UI-029 — Editor ICP registration panel + convergence visualization

## Goal
- Let the Sandbox editor run ICP registration between two selected point-cloud
  entities and visualize the source shape converging onto the target, consuming
  the per-iteration observer seam from `GEOM-055` via the runtime controller
  `Extrinsic::Runtime::AlignPointClouds`. Realizes the observability payoff in
  [`docs/architecture/geometry-pipeline-modularity.md`](../../../docs/architecture/geometry-pipeline-modularity.md)
  §3.4 and the editor-decoupling direction (roadmap §8).

## Non-goals
- No new registration algorithm behavior (correspondence/transform/global/
  non-rigid are separate geometry slices).
- No GPU/overlay-packet visualization in this slice: preview the convergence by
  driving the source (or a preview) entity's `Transform` with the trajectory,
  which is the simplest idiomatic path (the investigation found no editor
  debug-line seam; vector-field overlay packets are heavier and deferred).
- No schema-driven param auto-UI yet (that is the broader Slice 5 decoupling).

## Context
- Owner layer: `runtime` (editor UI + composition). `runtime -> all lower layers`.
- Runtime controller (LANDED with this change): `Extrinsic.Runtime.RegistrationAlignment`
  (`src/runtime/Runtime.RegistrationAlignment.cppm/.cpp`) runs
  `Geometry::Registration::AlignICP` with a trace-collecting observer and returns
  `RegistrationAlignmentOutcome { HasResult, Result, Traces }` plus
  `TrajectoryPose(outcome, index) -> glm::mat4` for scrubbing the convergence
  (index 0 = identity, index i = pose after iteration i, clamped). Layer:
  `runtime -> geometry` only; verified by `tests/unit/runtime/Test.RegistrationAlignment.cpp`.
- Confirmed editor APIs to build the panel on (from `Runtime.SandboxEditorUi.cpp`):
  - Selection: `context.Selection->SelectedStableIds()` (multi-select span) →
    take two entities as target + source (or add an explicit target picker).
  - Geometry read: `ResolveStableEntity(raw, stableId)` →
    `GS::BuildConstView(raw, entity)` → `view.VertexSource->Properties.Get<glm::vec3>(GS::PropertyNames::kPosition).Vector()`
    (mirror the `CollectKMeansPositions` extractor); require `Domain::PointCloud`.
  - Writeback/preview: update the source entity `Transform::Component`
    (Position from `glm::vec3(T[3])`, Rotation from `glm::quat_cast(glm::mat3(T))`)
    and `Dirty::MarkVertexPositionsDirty` / world-matrix as the other point-cloud
    commands do; wrap in `EditorCommandHistory`.
  - Panel plumbing mirrors `PointCloudOutlierRemovalUiState` + the
    `SandboxEditor*Command`/`*Result` + `SandboxEditorContext::Last*Result`
    pattern.

## Control surfaces
- Config: none new.
- UI: new registration panel (source/target selection, `ICPVariant`,
  MaxIterations, MaxCorrespondenceDistance, a "trajectory step" slider over
  `[0, IterationCount()]` that sets the previewed pose via `TrajectoryPose`).
- Agent/CLI: unaffected; the controller is independently callable/serializable.

## Required changes
- [x] Add runtime controller `Extrinsic.Runtime.RegistrationAlignment`
      (`AlignPointClouds`, `TrajectoryPose`, `RegistrationAlignmentOutcome`) and
      register it in `src/runtime/CMakeLists.txt`.
- [ ] Add `SandboxEditorRegistrationCommand` / `SandboxEditorRegistrationResult`
      structs + a `SandboxEditorContext::LastRegistrationResult` pointer in
      `Runtime.SandboxEditorUi.cppm`.
- [ ] Implement `ApplySandboxEditorRegistrationCommand` in
      `Runtime.SandboxEditorUi.cpp`: read source+target point clouds from the two
      selected entities, call `Runtime::AlignPointClouds`, store the outcome, and
      apply `TrajectoryPose(outcome, step)` to the source entity `Transform`.
- [ ] Add the panel (UiState + `SandboxEditorUi` member state + draw block +
      registration in the panel-draw frame) with the trajectory-step slider.

## Tests
- [x] `tests/unit/runtime/Test.RegistrationAlignment.cpp` (headless): rejects
      degenerate input; captures a per-iteration trajectory
      (`Traces.size() == Result.IterationsPerformed`, `Traces[i].RMSE ==
      RMSEHistory[i]`); `TrajectoryPose` returns identity at step 0, the final
      transform at step N, and clamps beyond N.
- [ ] Add an editor-command CPU test for `ApplySandboxEditorRegistrationCommand`
      (two synthetic point-cloud entities → aligned source transform; missing/
      wrong-domain selection → precise failure status).

## Docs
- [x] `docs/architecture/geometry-pipeline-modularity.md` §3.4 records the runtime
      controller + this editor consumer.
- [ ] Note the registration panel in the editor/runtime docs when the panel lands.

## Acceptance criteria
- [x] The runtime controller runs ICP capturing the convergence trajectory and is
      covered by a headless CPU test (`runtime -> geometry`; no ECS/RHI).
- [ ] Selecting a source + target point cloud and running the panel aligns the
      source onto the target; the slider scrubs the intermediate poses.
- [ ] `python3 tools/repo/check_layering.py --root src --strict` passes.
- [ ] Editor-command CPU test passes under the default gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRegistrationAlignment' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Reaching into ECS/RHI/UI from the geometry layer, or duplicating registration
  logic in the editor instead of calling `Runtime::AlignPointClouds`.
- Introducing per-point observer overhead on the no-observer registration path.
- Mixing this UI slice with geometry-algorithm behavior changes.

## Maturity
- Target: `Operational` — the runtime controller is `Operational` (headless CPU
  test). The editor panel closes to `Operational` when the panel + its
  command-level CPU test land and the source aligns onto the target in the editor.
- This slice partially lands (`Runtime.RegistrationAlignment` + test); the editor
  panel is the remaining open work owned by this task.
