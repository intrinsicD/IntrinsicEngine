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
## Implementation note (2026-05-03)
- `RenderWorld` now exposes immutable renderer-owned spans for `RenderableSnapshot` and `LightSnapshot` plus `InvalidSnapshotRecordCount` diagnostics.
- `RenderableSnapshot` captures stable renderable ID, canonical `GpuInstanceHandle`, model matrix, bounds, render flags, and material slot metadata.
- `PickRequestSnapshot`, `SelectionSnapshot`, `ShadowSnapshot`, `DebugPrimitiveSnapshot`, and `PostProcessSnapshot` provide defaulted optional frame-feature packets for downstream pass tasks to populate.
- `NullRenderer::SubmitRuntimeSnapshots()` copies runtime-submitted transform/light packets into renderer-owned frame storage and filters invalid transform records before `ExtractRenderWorld()` exposes spans.
- `TransformSyncRecord` now carries `StableId` so render-world snapshots can preserve runtime identity without importing ECS.
- Follow-up packet expansion remains for selection/picking, debug primitives, shadows, postprocess/readback, and value-only visualization packets.
## Required changes
- Define stable packets for surface, line, point, debug, light, shadow, selection, pick-request, visualization-atlas, auxiliary-attribute, post-process/readback, and runtime-only handoff data.
- Define canonical renderable-instance records that reference geometry records, transform slots, bounds/culling records, material slots, entity/pick IDs, render flags, visibility/layer flags, and dirty-domain metadata.
- Define the CPU snapshot fields that feed renderable-entity, transform, culling/bounds, material, and light GPU buffers without exposing live ECS ownership to graphics.
- Distinguish canonical instance-slot fields from auxiliary GPU resource references and CPU/runtime-only inputs.
- Preserve explicit ownership and default states for optional frame features.
- Add failure/diagnostic fields for invalid or unsupported snapshot data.
## Tests
- Added non-legacy contract tests for immutable render-world spans, stable IDs, optional feature flags, frame clearing, and invalid-record diagnostics.
- Ensured tests do not import legacy modules.
- Labeled CPU contract tests `contract;graphics` through `IntrinsicGraphicsContractCpuTests` so they run in the default CPU gate.
## Docs
- Update `docs/architecture/rendering-three-pass.md` and `docs/architecture/graphics.md` when the contract changes.
## Clarification questions from GRAPHICS-016
- Should `VisualizationSyncRecord` become a value/handle-only packet before pass work begins, replacing the current runtime-owned pointer sidecar used by `VisualizationSyncSystem`?
- Should `RuntimeRenderSnapshotBatch` be folded into `RenderWorld` as a fully immutable per-frame snapshot, or remain a renderer handoff API that copies runtime packets into renderer-owned frame storage?
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
