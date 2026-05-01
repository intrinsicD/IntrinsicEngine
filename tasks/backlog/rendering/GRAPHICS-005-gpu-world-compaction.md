# GRAPHICS-005 — GPU world compaction and relocation reporting
## Goal
- Add optional managed-buffer compaction planning and relocation reporting for `GpuWorld` without requiring graphics to own runtime/ECS updates.
## Non-goals
- No lifecycle-system migration.
- No direct ECS writes from graphics.
- No mandatory compaction during normal rendering.
## Context
- Owner: `src/graphics/renderer` with runtime handoff expectations.
- This depends on `GRAPHICS-004` so allocations and frees have stable metadata.
## Required changes
- Define compaction thresholds, planning API, execution result, and relocation table contract.
- Ensure compaction is opt-in and safe around frames in flight.
- Report relocations to runtime-owned sidecar/extraction caches; graphics must not patch canonical ECS components directly.
- Preserve geometry, material, transform, bounds/culling, light, and instance references through generation checks or explicit relocation tables.
- Expose diagnostics for fragmentation and bytes moved.
## Tests
- Add unit tests for fragmentation thresholds, relocation table correctness, skipped compaction, and stale relocation rejection.
## Docs
- Document the compaction contract and runtime handoff expectations in graphics/runtime architecture docs.
## Acceptance criteria
- Runtime can update extracted snapshots or handles using graphics-provided relocation results.
- Geometry/material/instance references remain valid through generation checks or explicit relocation reports.
- Compaction can be tested without Vulkan or live ECS.
- Disabled compaction preserves existing allocation behavior.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime or ECS ownership into `src/graphics`.
