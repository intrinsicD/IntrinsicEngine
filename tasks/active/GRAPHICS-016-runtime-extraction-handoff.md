# GRAPHICS-016 — Runtime extraction and graphics handoff
## Goal
- Define runtime-owned CPU scene composition and ECS-to-graphics extraction so promoted graphics systems consume immutable snapshots/views only.
## Non-goals
- No renderer pass implementation.
- No new ECS component model unless required by extraction seams.
- No graphics imports from runtime or live ECS ownership.
- No graphics GPU handles, slots, leases, or backend resource IDs stored in canonical `src/ecs` components.
## Context
- Owner: `src/runtime` for extraction/wiring and `src/graphics/renderer` for consumed snapshot contracts.
- Legacy runtime render extraction and graphics lifecycle systems show required behavior for mesh, graph, point-cloud, selection, and visualization handoff.
## Current boundary audit (2026-05-02)
- Stage A is implemented for promoted graphics sync APIs: `TransformSyncSystem`, `LightSystem`, and `VisualizationSyncSystem` consume typed graphics snapshot records/spans instead of `entt::registry&`.
- `src/graphics/renderer/CMakeLists.txt` no longer links `ExtrinsicECS` or `EnTT::EnTT` into `ExtrinsicGraphics`.
- `tests/contract/graphics/Test.RendererRhiBoundary.cpp` guards against reintroducing `entt` or `Extrinsic.ECS` imports under `src/graphics/renderer`.
- Remaining follow-up is runtime wiring: runtime must own live ECS queries, sidecar mappings, dirty-domain filtering, deletion handling, and conversion into the graphics snapshot records.

## Stage B/C implementation note (2026-05-03)
- `Extrinsic.Runtime.RenderExtraction` now owns the promoted runtime ECS query and sidecar cache for transform/light/visualization extraction.
- `RenderExtractionCache` stores entity-to-graphics instance/material/visualization sidecars outside canonical ECS components, allocates/frees `GpuWorld` instances, clears consumed `DirtyTransform` tags, handles destroyed renderables, and submits snapshot packets to graphics.
- `IRenderer::SubmitRuntimeSnapshots(RuntimeRenderSnapshotBatch)` is the promoted runtime-to-graphics handoff; the null renderer copies records into renderer-owned frame storage before render-world extraction/prepare.
- `Engine::RunFrame()` calls runtime extraction after `BeginFrame()` and before `ExtractRenderWorld()` so renderer prep consumes runtime-submitted snapshots without importing ECS.
- CPU integration coverage lives in `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp` and the CPU-labeled `IntrinsicRuntimeGraphicsCpuTests` target.
- Full mesh/graph/point-cloud geometry upload parity, selection/picking packets, debug primitive packets, and value-only visualization packet cleanup remain downstream GRAPHICS-002/004/010/012 work.

## Staged implementation plan
- Stage A (API seam): introduced runtime-owned snapshot packets for transform/light/visualization sync and switched promoted graphics sync entry points from `entt::registry&` to typed snapshot spans.
- Stage B (wiring): perform ECS queries and dirty-domain filtering inside runtime extraction, then feed packets into graphics systems.
- Stage C (dependency cleanup): remove any remaining downstream test/runtime convenience dependencies on ECS once Stage B callsites are migrated; keep any temporary shim time-bounded in `tasks/active/`.

## Required changes
- Define extraction records for mesh, graph, point-cloud, material, transform, lighting, selection, visualization, and debug inputs.
- Define runtime-owned sidecar/cache mappings from ECS entities, asset IDs, and geometry source handles to graphics instance, geometry, material, transform, bounds/culling, and light handles.
- Extract transforms, materials, lights, world-space bounds, render flags, visibility/layer masks, dirty domains, deletion events, and compaction relocation handoffs.
- Route lifecycle/sync decisions through runtime-owned handoff APIs.
- Add compatibility shims only if they are tracked and time-bounded by active tasks.
## Tests
- Added runtime/graphics integration tests using the null renderer and extracted snapshots.
- Covered creation, update, deletion, dirty transform-domain clearing, visualization extraction, light extraction, and destroyed-entity filtering.
- Labeled extraction-handoff integration tests `integration;runtime;graphics` through `IntrinsicRuntimeGraphicsCpuTests` so they are not excluded by the default CPU gate.
## Docs
- Update runtime subsystem boundaries, graphics architecture, and migration parity docs.
## Acceptance criteria
- Graphics consumes snapshots/views only for promoted paths.
- Runtime owns live ECS access and renderer wiring.
- Runtime owns mappings from ECS/source data to graphics instance/material/geometry/transform/bounds/light handles, and those mappings are not canonical ECS component state.
- Integration tests prove lifecycle handoff without Vulkan.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding new graphics dependencies on runtime or ECS ownership.
