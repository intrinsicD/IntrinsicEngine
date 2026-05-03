# GRAPHICS-004 — GPU world allocation and lifetime
## Goal
- Harden `GpuWorld` allocation, lifetime tracking, frees, deferred reuse, and diagnostics for retained GPU scene pools: instances, geometry records, transforms, bounds/culling records, materials, lights, and scene-table bindings.
## Non-goals
- No new rendering pass behavior.
- No buffer compaction or relocation in this task.
- No direct ECS mutation from graphics.
## Context
- Owner: `src/graphics/renderer` with RHI allocation seams from `src/graphics/rhi`.
- Existing managed geometry paths need explicit ownership and failure states before lifecycle-heavy rendering features are promoted.
## Required changes
- Replace bump-only allocation assumptions with tracked ranges and stable handles.
- Define generation-checked handles and a shared instance-slot space that indexes renderable, transform, bounds/culling, material-reference, picking, and draw-bucket records consistently.
- Add explicit free/deferred-free behavior suitable for multiple frames in flight.
- Keep ECS-to-GPU handle mappings in runtime-owned sidecar/extraction state, not in canonical ECS components.
- Report allocation overflow, stale handles, null-device behavior, and leak diagnostics.
## Tests
- Add unit tests for allocate/free/reuse, stale handles, overflow, frame-delayed deletion, and null backend behavior.
- Add regression coverage for deterministic diagnostics.
- Label allocator unit tests `unit;graphics` and diagnostics regression tests `regression;graphics` so both run in the default CPU gate.
## Docs
- Update `docs/migration/nonlegacy-parity-matrix.md` and any GPU-world architecture notes touched by the implementation.
## Acceptance criteria
- Geometry, instance, transform, bounds/culling, material, and light frees are observable and ranges can be reused after the configured safety window.
- Invalid handles fail deterministically for every GPU scene pool without corrupting other allocations.
- Diagnostics make retained-buffer pressure visible.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Performing compaction or relocation updates in this task.
