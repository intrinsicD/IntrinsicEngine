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
## Implementation note (2026-05-03)
- `GpuWorld::InitDesc` now declares a `DeferredFreeFrames` safety window.
- Instance and geometry slot pools keep generation-checked diagnostics for capacity, live slots, reusable slots, pending deferred frees, allocation overflow, invalid handles, and stale handles.
- Freed instance/geometry slots are invalidated immediately and become reusable only after `SyncFrame()` advances past the configured safety window.
- `GpuWorld::Diagnostics` reports retained-buffer byte pressure, vertex/index overflow, light overflow, and null-device mode.
- Added CPU graphics contract tests for deferred instance reuse, stale/invalid diagnostics, geometry overflow, and null-device lifetime observability.
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
- Added `Test.GpuWorldLifetimeContract.cpp` to the CPU graphics contract target for the current retained-pool lifetime/diagnostics slice.
## Docs
- Update `docs/migration/nonlegacy-parity-matrix.md` and any GPU-world architecture notes touched by the implementation.
## Acceptance criteria
- Geometry, instance, transform, bounds/culling, material, and light frees are observable and ranges can be reused after the configured safety window.
- Invalid handles fail deterministically for every GPU scene pool without corrupting other allocations.
- Diagnostics make retained-buffer pressure visible.
## Verification
Current slice checks:

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j1
ctest --test-dir build/ci --output-on-failure -R 'GpuWorldLifetimeContract|FrameRecipeContract|RenderWorldContract' --timeout 60
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j1
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet
```

Note: a corrupt ignored `external/cache/draco-*` checkout caused CMake
regeneration to fail with missing Draco source files. Removing the ignored cache
directories allowed `cmake --preset ci`, `IntrinsicTests`, and the default CPU
CTest gate to complete.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Performing compaction or relocation updates in this task.
