# GRAPHICS-012 — Picking and selection outline
## Goal
- Reimplement picking, selection ID passes, readback seams, and selection outline behavior for surface, line, and point domains.
## Non-goals
- No transform gizmo editing.
- No runtime selection policy rewrite beyond graphics handoff seams.
- No legacy picking pass dependency.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/renderer/Passes`, with runtime owning selection resolution and ECS mutation.
- `docs/architecture/rendering-three-pass.md` defines `EntityId`, `PrimitiveId`, and sub-element selection contracts.
## Required changes
- Fill entity/face/edge/point ID pass contracts and selection outline pass resource behavior.
- Generate `EntityId` and `PrimitiveId` from canonical renderable-instance records, including stable extracted entity/source IDs and primitive-domain metadata.
- Define pending-pick request, readback result, and invalid/no-hit diagnostics.
- Ensure line/point/surface primitive domains preserve stable ID encoding.
## Tests
- Add contract/integration tests for entity IDs, primitive domain bits, pending-pick gating, readback seams, outline resource use, and no-hit behavior.
- Use CPU/mock tests for command order and data encoding.
## Docs
- Update picking, sub-element selection, and outline sections in architecture docs.
## Acceptance criteria
- Surface, line, and point picking have non-legacy CPU-testable seams.
- Selection outline is scheduled after post-process/geometry resources as documented.
- Runtime resolves selected ECS entities from extracted stable IDs; graphics never mutates ECS selection state.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Falling back to legacy picking modules.
