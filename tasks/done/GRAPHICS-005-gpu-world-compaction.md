# GRAPHICS-005 — GPU world compaction and relocation reporting

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-004` completion.
- Current slice: CPU/null-testable managed-buffer fragmentation diagnostics, opt-in compaction planning, explicit apply results, and generation-checked relocation reporting. No automatic render-frame compaction.

## Completion metadata
- Completion date: 2026-05-03.
- Commit/PR: pending local agent workflow handoff.
- Follow-up task: `GRAPHICS-006 — Material, shader, and pipeline registry` promoted to `tasks/active/`.

## Implementation note (2026-05-03)
- `GpuWorld` now tracks retained managed vertex/index ranges per geometry upload.
- Added explicit `PlanManagedBufferCompaction()` and `ApplyManagedBufferCompaction()` APIs with thresholds, pending-free blocking, relocation tables, and stale relocation rejection.
- Added `GetManagedBufferDiagnostics()` for managed-buffer live/fragmented bytes, compaction bytes moved, compaction count, and stale relocation diagnostics without changing the existing lifetime diagnostics ABI.
- Compaction remains opt-in and CPU/null-testable; no render-frame path performs automatic compaction.
- Added `Test.GpuWorldCompactionContract.cpp` to the CPU graphics contract target.

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
- Keep compaction disabled unless `PlanManagedBufferCompaction()` / `ApplyManagedBufferCompaction()` are called explicitly.
- Report relocations to runtime-owned sidecar/extraction caches; graphics must not patch canonical ECS components directly.
- Preserve geometry, material, transform, bounds/culling, light, and instance references through generation checks or explicit relocation tables.
- Expose diagnostics for fragmentation and bytes moved.
## Tests
- Add unit tests for fragmentation thresholds, relocation table correctness, skipped compaction, and stale relocation rejection.
- Label compaction unit tests `unit;graphics` so they run in the default CPU gate.
## Docs
- Document the compaction contract and runtime handoff expectations in graphics/runtime architecture docs.
## Acceptance criteria
- Runtime can update extracted snapshots or handles using graphics-provided relocation results.
- Geometry/material/instance references remain valid through generation checks or explicit relocation reports.
- Compaction can be tested without Vulkan or live ECS.
- Disabled compaction preserves existing allocation behavior.
## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GpuWorldCompactionContract|GpuWorldLifetimeContract|FrameRecipeContract|RenderWorldContract' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Verified on 2026-05-03:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j1
ctest --test-dir build/ci --output-on-failure -R 'GpuWorldCompactionContract|GpuWorldLifetimeContract|FrameRecipeContract|RenderWorldContract' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --build --preset ci --target IntrinsicTests -j1
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet
```

Note: as in `GRAPHICS-004`, a corrupt ignored `external/cache/draco-*`
checkout caused CMake regeneration to stall/fail until the ignored cache
directories were removed and `cmake --preset ci` was rerun.
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime or ECS ownership into `src/graphics`.
