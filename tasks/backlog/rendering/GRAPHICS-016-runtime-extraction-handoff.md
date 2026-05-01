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
## Required changes
- Define extraction records for mesh, graph, point-cloud, material, transform, lighting, selection, visualization, and debug inputs.
- Define runtime-owned sidecar/cache mappings from ECS entities, asset IDs, and geometry source handles to graphics instance, geometry, material, transform, bounds/culling, and light handles.
- Extract transforms, materials, lights, world-space bounds, render flags, visibility/layer masks, dirty domains, deletion events, and compaction relocation handoffs.
- Route lifecycle/sync decisions through runtime-owned handoff APIs.
- Add compatibility shims only if they are tracked and time-bounded by active tasks.
## Tests
- Add runtime/graphics integration tests using null renderer and extracted snapshots.
- Cover creation, update, deletion, dirty domains, and invalid entity filtering.
- Label these extraction-handoff integration tests `integration;runtime;graphics` so they run in the default CPU gate.
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
