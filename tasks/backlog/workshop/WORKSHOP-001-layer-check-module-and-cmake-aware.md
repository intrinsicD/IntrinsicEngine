# WORKSHOP-001 — Make layer enforcement module- and CMake-aware

## Goal
- Make `tools/repo/check_layering.py --strict` catch promoted-layer dependency violations in both C++23 module imports and CMake target links, so the written architecture contract cannot silently drift away from the actual build graph.

## Non-goals
- Do not change production C++ architecture in this task except for test fixtures under the checker's own test data.
- Do not remove existing layering allowlist entries unless they are proven obsolete by the updated checker.
- Do not weaken any dependency rule from `/AGENTS.md`.
- Do not make broad formatting or mechanical moves.

## Context
- `/AGENTS.md` is the authoritative layer contract.
- The current checker scans includes/imports but is likely blind to `Extrinsic.Platform.*` module imports and CMake `target_link_libraries(...)` edges.
- Known current issue to expose: `src/graphics/rhi/RHI.Device.cppm` imports `Extrinsic.Platform.Window`, and `src/graphics/rhi/CMakeLists.txt` links `ExtrinsicPlatform`, even though the contract says `graphics/rhi -> core` only.
- This task should intentionally make that violation visible before WORKSHOP-002 fixes it.

## Required changes
- [ ] Extend `tools/repo/check_layering.py` target-layer detection to recognize C++23 module imports using promoted module prefixes:
  - `Extrinsic.Core.*` -> `core`
  - `Extrinsic.Geometry.*` and `Geometry.*` promoted modules -> `geometry`
  - `Extrinsic.Asset.*` -> `assets`
  - `Extrinsic.ECS.*` -> `ecs`
  - `Extrinsic.RHI.*` -> `graphics_rhi`
  - `Extrinsic.Graphics.*` -> `graphics`
  - `Extrinsic.Backends.Vulkan*` -> `graphics`
  - `Extrinsic.Platform.*` -> `platform`
  - `Extrinsic.Runtime.*` -> `runtime`
- [ ] Add CMake target dependency scanning for promoted targets in `target_link_libraries(...)` calls.
- [ ] Map promoted CMake targets to layers, at minimum:
  - `ExtrinsicCore`, `IntrinsicCore` -> `core`
  - `IntrinsicGeometry` -> `geometry`
  - `ExtrinsicAssets` -> `assets`
  - `ExtrinsicECS`, `IntrinsicECS` -> `ecs`
  - `ExtrinsicRHI` -> `graphics_rhi`
  - `ExtrinsicGraphics`, `ExtrinsicGraphicsAssets`, `ExtrinsicGraphicsRenderGraph`, `ExtrinsicBackendsVulkan` -> `graphics`
  - `ExtrinsicPlatform` -> `platform`
  - `ExtrinsicRuntime`, `IntrinsicRuntime` -> `runtime`
- [ ] Make the checker report source file path, line number, source layer, target layer, reference, and whether the violation came from a C++ import/include or a CMake link edge.
- [ ] Add a small fixture directory under `tests/contract/repo/layering_fixtures/` or another repo-appropriate test fixture location.
- [ ] Add regression fixtures proving these fail:
  - `graphics/rhi` importing `Extrinsic.Platform.Window`
  - `graphics/rhi` linking `ExtrinsicPlatform`
  - `graphics` importing `Extrinsic.ECS.*`
  - `platform` importing `Extrinsic.Graphics.*`
  - `core` importing anything promoted above core
- [ ] Add regression fixtures proving these pass:
  - `runtime` importing `Extrinsic.ECS.*`, `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Platform.*`, and `Extrinsic.Asset.*`
  - `graphics` importing `Extrinsic.RHI.*`, `Extrinsic.Asset.Registry`, `Extrinsic.Core.*`, and allowed geometry GPU-view/value modules
- [ ] Ensure allowlist handling still works and still requires task/expiry/reason metadata.
- [ ] Update failure text so agents know whether to fix by moving the dependency downward, introducing a seam, or adding a temporary tracked allowlist entry.

## Tests
- [ ] Add or update a repo-tooling test for `check_layering.py` fixture cases.
- [ ] Run the updated checker against fixtures and assert expected pass/fail behavior.
- [ ] Run the updated checker against the real `src/` tree and confirm it reports the known `graphics/rhi -> platform` violation before WORKSHOP-002 lands.
- [ ] Keep existing task-policy and docs-link checks passing.

## Docs
- [ ] Update `docs/agent/architecture-review-checklist.md` to state that layer checks cover both C++23 imports and CMake target links.
- [ ] Update `docs/agent/review-checklist.md` if needed so agents treat CMake link edges as architecture edges.
- [ ] Update any `tools/repo/check_layering.py` usage docs or comments to mention module-prefix and CMake-edge coverage.

## Acceptance criteria
- [ ] `tools/repo/check_layering.py --root src --strict` fails on the current RHI/platform dependency until WORKSHOP-002 fixes it.
- [ ] The checker catches C++ module import violations involving `Extrinsic.*` module names.
- [ ] The checker catches CMake `target_link_libraries(...)` violations between promoted targets.
- [ ] Fixture tests prove both positive and negative cases.
- [ ] No architecture rule in `/AGENTS.md` is weakened.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not edit `/AGENTS.md` to legalize current violations.
- Do not add broad permanent allowlist entries for promoted layers.
- Do not change runtime/RHI/renderer behavior in this task.
- Do not rename promoted targets or modules.
