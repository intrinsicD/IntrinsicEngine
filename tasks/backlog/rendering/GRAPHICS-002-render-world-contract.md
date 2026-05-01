# GRAPHICS-002 — Render world and frame input contract
## Goal
- Expand `RenderWorld` and `RenderFrameInput` into the immutable graphics snapshot contract consumed by renderer passes.
## Non-goals
- No live ECS access from graphics.
- No Vulkan-only implementation or pass command recording.
- No legacy module dependency.
- No graphics GPU handles, leases, or backend resource IDs stored in canonical `src/ecs` components.
## Context
- Owner: `src/graphics/renderer`.
- `docs/architecture/rendering-three-pass.md` requires graphics to consume snapshots/views and canonical frame resources.
- Legacy render systems expose useful behavior for drawables, lights, picking, debug, and selection, but the promoted contract must be independent and typed.
## Required changes
- Define stable packets for surface, line, point, debug, light, shadow, selection, pick-request, visualization-atlas, auxiliary-attribute, post-process/readback, and runtime-only handoff data.
- Define canonical renderable-instance records that reference geometry records, transform slots, bounds/culling records, material slots, entity/pick IDs, render flags, visibility/layer flags, and dirty-domain metadata.
- Define the CPU snapshot fields that feed renderable-entity, transform, culling/bounds, material, and light GPU buffers without exposing live ECS ownership to graphics.
- Distinguish canonical instance-slot fields from auxiliary GPU resource references and CPU/runtime-only inputs.
- Preserve explicit ownership and default states for optional frame features.
- Add failure/diagnostic fields for invalid or unsupported snapshot data.
## Tests
- Add non-legacy contract tests for packet defaults, immutability expectations, stable IDs, optional feature flags, and invalid-data handling.
- Ensure tests do not import legacy modules.
- Label CPU contract tests `contract;graphics` so they run in the default CPU gate.
## Docs
- Update `docs/architecture/rendering-three-pass.md` and `docs/architecture/graphics.md` when the contract changes.
## Acceptance criteria
- Passes can consume render snapshots without higher-layer imports.
- Snapshot fields cover surface, line, point, lighting, shadow, picking, selection, and transient debug needs.
- The contract defines one canonical instance-slot identity shared by renderable records, transforms, bounds/culling data, material references, picking IDs, and draw buckets.
- Contract tests document expected defaults and invalid states.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing ECS or runtime ownership into `src/graphics/renderer`.
