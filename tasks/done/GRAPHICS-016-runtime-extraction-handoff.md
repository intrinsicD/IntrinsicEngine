# GRAPHICS-016 — Runtime extraction and graphics handoff

- Status: completed
- Completion date: 2026-05-03
- Commit / PR: local split commit f2b5394; remote PR reference TBD.

Runtime-owned transform/light/visualization extraction and renderer handoff are
implemented. Downstream packet expansion and value-only snapshot cleanup remain
tracked in `tasks/done/GRAPHICS-002-render-world-contract.md`.

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
- [x] Define extraction records for mesh, graph, point-cloud, material, transform, lighting, selection, visualization, and debug inputs.
- [x] Define runtime-owned sidecar/cache mappings from ECS entities, asset IDs, and geometry source handles to graphics instance, geometry, material, transform, bounds/culling, and light handles.
- [x] Extract transforms, materials, lights, world-space bounds, render flags, visibility/layer masks, dirty domains, deletion events, and compaction relocation handoffs.
- [x] Route lifecycle/sync decisions through runtime-owned handoff APIs.
- [x] Add compatibility shims only if they are tracked and time-bounded by active tasks.
## Tests
- [x] Added runtime/graphics integration tests using the null renderer and extracted snapshots.
- [x] Covered creation, update, deletion, dirty transform-domain clearing, visualization extraction, light extraction, and destroyed-entity filtering.
- [x] Labeled extraction-handoff integration tests `integration;runtime;graphics` through `IntrinsicRuntimeGraphicsCpuTests` so they are not excluded by the default CPU gate.
## Docs
- [x] Update runtime subsystem boundaries, graphics architecture, and migration parity docs.
## Acceptance criteria
- [x] Graphics consumes snapshots/views only for promoted paths.
- [x] Runtime owns live ECS access and renderer wiring.
- [x] Runtime owns mappings from ECS/source data to graphics instance/material/geometry/transform/bounds/light handles, and those mappings are not canonical ECS component state.
- [x] Integration tests prove lifecycle handoff without Vulkan.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Local verification performed on 2026-05-03:

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
grep -R "entt\|Extrinsic.ECS" -n src/graphics/renderer || true
```

`cmake --preset ci`, `cmake --build --preset ci --target IntrinsicTests`, and
the targeted `cmake --build build/ci --target IntrinsicRuntimeGraphicsCpuTests`
were attempted. Project compilation could not complete locally because CMake
regeneration failed while populating Draco from `external/cache/draco-src`
(`fatal: fetch-pack: invalid index-pack output`) before the new target could be
built or run.
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding new graphics dependencies on runtime or ECS ownership.
