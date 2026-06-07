# GRAPHICS-083 — Graphics/RHI module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of promoted graphics, RHI, and Vulkan `.cppm` interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No renderer, framegraph, RHI, or Vulkan behavior changes.
- No shader, pass-ordering, synchronization, queue-family, descriptor, pipeline, or backend-selection semantic changes.
- No runtime/ECS/live asset-service/platform ownership changes.
- No broad core, geometry, platform, runtime, physics, or legacy cleanup in this task.
- No attempt to move templates or constexpr bodies that must remain visible to importers.

## Context
- Status: done (retired 2026-06-07; implementation split landed in `bfcd2751`).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after a clean no-cache rebuild, explicit benchmark-smoke target build, and default CPU gate on 2026-06-07.
- Owning subsystem/layer: `graphics/*`, `graphics/rhi`, and `graphics/vulkan` per `AGENTS.md` dependency boundaries.
- Static audit on 2026-06-06 identified these promoted graphics/RHI/Vulkan cleanup targets:
  `src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm`,
  `src/graphics/renderer/Graphics.CameraSnapshots.cppm`,
  `src/graphics/renderer/Graphics.Material.cppm`,
  `src/graphics/renderer/Passes/Pass.PostProcess.Bloom.cppm`,
  `src/graphics/rhi/RHI.PipelineRegistry.cppm`,
  `src/graphics/rhi/RHI.Profiler.cppm`,
  `src/graphics/rhi/RHI.QueueAffinity.cppm`,
  `src/graphics/rhi/RHI.TextureUpload.cppm`,
  `src/graphics/rhi/RHI.Types.cppm`, and
  `src/graphics/vulkan/Backends.Vulkan.cppm`.
- Prefer existing implementation units where available, especially pass helpers and RHI pipeline registry files, before adding new `.cpp` files.

## Required changes
- [x] Slice A: move audited bodies for targets that already have matching `.cpp` implementation units.
- [x] Slice B: add missing implementation units for renderer/RHI public-helper modules where moved bodies justify `.cpp` files.
- [x] Slice C: move backend-local Vulkan helpers out of `Backends.Vulkan.cppm` without changing public Vulkan factory/diagnostic behavior.
- [x] Register any newly added `.cpp` files as private sources in the relevant `src/graphics/**/CMakeLists.txt` file.
- [x] Keep templates, ABI structs, simple constexpr enum classifiers, and importer-required inline helpers in `.cppm` when required; record retained exceptions before retirement.
- [x] Remove `inline` from moved non-template exported declarations where no longer required.
- [x] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [x] Audit imports in touched `.cppm` files and keep only public-surface imports in interfaces.

## Tests
- [x] Existing graphics contract tests remain green.
- [x] Existing RHI contract tests remain green for pipeline registry, profiler, queue affinity, texture upload, and draw-bucket type helpers.
- [x] Existing Vulkan opt-in tests are not required for CPU-only cleanup unless Vulkan factory/diagnostic declarations change; if they do, run the appropriate `gpu;vulkan` smoke on a capable host.
- [x] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [x] Update graphics/RHI architecture docs only if public behavior or ownership wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with per-slice completion notes before retirement.

## Acceptance criteria
- [x] Touched graphics/RHI/Vulkan `.cppm` files expose declarations, ABI/value types, templates, and small inline/constexpr importer-required helpers only, or record justified retained exceptions.
- [x] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [x] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [x] CPU graphics/RHI tests and the default CPU gate pass without changed expectations.
- [x] The change preserves graphics/RHI/Vulkan layering and introduces no live ECS/runtime/platform ownership into promoted graphics APIs.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- 2026-06-07 retirement verification: the previous rendergraph ASan blocker was reproduced as stale C++23 module layout state from ccache/incremental module artifacts, not a source defect in the implementation split. A `CCACHE_DISABLE=1` `IntrinsicTests` rebuild plus an explicit `IntrinsicBenchmarkSmoke` build restored a clean default CPU gate; final `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2816/2816.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Graphics|RHI|CameraSnapshots|Material|Bloom|PipelineRegistry|Profiler|QueueAffinity|TextureUpload|Vulkan' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change rendering output, pass ordering, RHI ABI semantics, Vulkan operational status, queue-family resolution, descriptor behavior, or texture-upload layout results.
- Do not introduce live ECS/runtime/platform ownership into promoted graphics/RHI APIs.
- Do not move templates or importer-required constexpr definitions into `.cpp`.
- Do not mix this mechanical implementation split with renderer/RHI behavior refactors.
