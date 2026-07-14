---
id: UI-033
theme: F
depends_on:
  - UI-031
  - UI-032
completed: 2026-07-07
---
# UI-033 — Compositional Appearance domain windows

## Status
- Retired on 2026-07-07 at `CPUContracted`.
- PR/commit: this retirement commit.
- Fix: `Appearance` windows now key their editable body on selected-entity
  render-lane availability rather than exact provenance-domain match, while
  `Processing` and raw `Properties` windows remain exact-domain gated.
- Evidence: focused `SandboxEditorUi` contract coverage and the default
  CPU-supported gate passed locally.

## Goal
- Let `PointCloud / Appearance`, `Graph / Appearance`, and `Mesh / Appearance` expose controls for the selected entity's compatible render lane even when the entity's provenance domain is a richer domain that reuses that lane.

## Non-goals
- Changing Processing windows; they still require an exact selected provenance domain.
- Changing raw Properties windows; they remain exact-domain property explorers.
- Redesigning material shading-model or texture-source authority; GRAPHICS-105 owns that larger appearance model.
- Adding new geometry properties or import formats.

## Context
- Owner/layer: `runtime` Sandbox editor UI model/draw code.
- The existing model already computes lane-specific visualization targets: `PointCloud / Appearance` targets points, `Graph / Appearance` targets edges, and `Mesh / Appearance` targets surfaces.
- The model can discover mesh vertex properties for a mesh rendered as points and graph vertex properties for a graph rendered as points, but the draw path suppressed the Appearance body whenever `SelectedDomain != ExpectedDomain`.
- The intended domain model is compositional: point cloud owns vertex/point appearance, graph reuses the vertex/point appearance and adds connectivity/edge appearance, and mesh reuses those lower-domain affordances while adding surface/face appearance.

## Required changes
- [x] Gate the Appearance window body on selected-entity plus render-lane availability, not exact provenance-domain match.
- [x] Keep render-lane controls disabled when the selected entity does not expose the target lane.
- [x] Preserve exact-domain gating for Processing and raw Properties windows.

## Tests
- [x] Existing contract coverage continues to prove lane-target discovery for mesh-as-points and graph-as-points.
- [x] Runtime editor contract tests pass.

## Docs
- [x] Update `src/runtime/README.md` to describe compositional Appearance gating.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task.

## Acceptance criteria
- [x] `PointCloud / Appearance` can display point/vertex visualization properties for selected point-cloud, graph, and mesh entities when the points lane is available.
- [x] `Graph / Appearance` and `Mesh / Appearance` continue to expose edge/surface controls based on render-lane availability.
- [x] Processing and raw Properties windows still reject mismatched provenance domains.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R SandboxEditorUi -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
```

## Forbidden changes
- Moving editor content between layers.
- Making Processing commands operate on mismatched provenance domains.
- Enabling texture-source material controls for point clouds or graphs outside GRAPHICS-105.
