# GRAPHICS-012 — Picking and selection outline

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-011` completion.
- Current slice: promoted from backlog; implementation not started in this handoff.

## Goal
- Reimplement picking, selection ID passes, readback seams, and selection outline behavior for surface, line, and point domains.
## Non-goals
- No transform gizmo editing.
- No runtime selection policy rewrite beyond graphics handoff seams.
- No legacy picking pass dependency.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/renderer/Passes`, with runtime owning selection resolution and ECS mutation.
- `docs/architecture/rendering-three-pass.md` defines `EntityId`, `PrimitiveId`, the logical `PickingPass` stage, and sub-element selection contracts.
- `PickingPass` is a logical picking/selection ID stage owned by this task. It is composed of these source modules and seams (no source consolidation is required by this task):
  - `Extrinsic.Graphics.Pass.Selection.EntityId` (class `EntityIdPass`)
  - `Extrinsic.Graphics.Pass.Selection.FaceId` (class `FaceIdPass`)
  - `Extrinsic.Graphics.Pass.Selection.EdgeId` (class `EdgeIdPass`)
  - `Extrinsic.Graphics.Pass.Selection.PointId` (class `PointIdPass`)
  - readback/result seam owned by `SelectionSystem`
  - `Extrinsic.Graphics.Pass.Selection.Outline` (class `SelectionOutlinePass`) — scheduled later in the pipeline as the separate `SelectionOutlinePass`, but its contract is co-owned by this task
- `Pass.Culling` (`CullingPass`) is owned by GRAPHICS-007 and is a prerequisite for all picking/selection ID passes; this task consumes its draw-bucket contracts but does not redefine them.
## Required changes
- Fill entity/face/edge/point ID pass contracts and selection outline pass resource behavior. Each split source module above retains its own contract; the logical `PickingPass` stage groups them.
- Generate `EntityId` and `PrimitiveId` from canonical renderable-instance records, including stable extracted entity/source IDs and primitive-domain metadata.
- Define pending-pick request, readback result, and invalid/no-hit diagnostics.
- Ensure line/point/surface primitive domains preserve stable ID encoding.
- Do **not** consolidate the split selection source modules. Splitting per primitive domain is intentional and sanctioned by `docs/architecture/rendering-three-pass.md`.
## Tests
- Add contract/integration tests for entity IDs, primitive domain bits, pending-pick gating, readback seams, outline resource use, and no-hit behavior.
- Use CPU/mock tests for command order and data encoding.
- Cover each split selection module (`EntityIdPass`, `FaceIdPass`, `EdgeIdPass`, `PointIdPass`, `SelectionOutlinePass`) with at least one contract test exercising `Execute` against a recording command context.
- Label graphics-only contract tests `contract;graphics` and runtime-handoff tests `integration;runtime;graphics` so both run in the default CPU gate.
## Docs
- Update picking, sub-element selection, and outline sections in architecture docs.
- Keep the Pass module naming map in `docs/architecture/rendering-three-pass.md` in sync with the split selection source modules; if a module is added or renamed, update the map in the same change.
## Acceptance criteria
- Surface, line, and point picking have non-legacy CPU-testable seams.
- Selection outline is scheduled after post-process/geometry resources as documented.
- Runtime resolves selected ECS entities from extracted stable IDs; graphics never mutates ECS selection state.
- The split selection source modules (`Pass.Selection.EntityId`, `Pass.Selection.FaceId`, `Pass.Selection.EdgeId`, `Pass.Selection.PointId`, `Pass.Selection.Outline`) and the Pass module naming map in `docs/architecture/rendering-three-pass.md` agree on names, classes, and the logical `PickingPass` grouping.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Falling back to legacy picking modules.
