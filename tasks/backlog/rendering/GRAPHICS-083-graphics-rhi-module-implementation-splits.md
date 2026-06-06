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
- Status: backlog.
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
- [ ] Slice A: move audited bodies for targets that already have matching `.cpp` implementation units.
- [ ] Slice B: add missing implementation units for renderer/RHI public-helper modules where moved bodies justify `.cpp` files.
- [ ] Slice C: move backend-local Vulkan helpers out of `Backends.Vulkan.cppm` without changing public Vulkan factory/diagnostic behavior.
- [ ] Register any newly added `.cpp` files as private sources in the relevant `src/graphics/**/CMakeLists.txt` file.
- [ ] Keep templates, ABI structs, simple constexpr enum classifiers, and importer-required inline helpers in `.cppm` when required; record retained exceptions before retirement.
- [ ] Remove `inline` from moved non-template exported declarations where no longer required.
- [ ] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [ ] Audit imports in touched `.cppm` files and keep only public-surface imports in interfaces.

## Tests
- [ ] Existing graphics contract tests remain green.
- [ ] Existing RHI contract tests remain green for pipeline registry, profiler, queue affinity, texture upload, and draw-bucket type helpers.
- [ ] Existing Vulkan opt-in tests are not required for CPU-only cleanup unless Vulkan factory/diagnostic declarations change; if they do, run the appropriate `gpu;vulkan` smoke on a capable host.
- [ ] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [ ] Update graphics/RHI architecture docs only if public behavior or ownership wording changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [ ] Update this task with per-slice completion notes before retirement.

## Acceptance criteria
- [ ] Touched graphics/RHI/Vulkan `.cppm` files expose declarations, ABI/value types, templates, and small inline/constexpr importer-required helpers only, or record justified retained exceptions.
- [ ] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [ ] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [ ] CPU graphics/RHI tests and the default CPU gate pass without changed expectations.
- [ ] The change preserves graphics/RHI/Vulkan layering and introduces no live ECS/runtime/platform ownership into promoted graphics APIs.

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
