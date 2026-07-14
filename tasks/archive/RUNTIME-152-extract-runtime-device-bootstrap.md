---
id: RUNTIME-152
theme: F
depends_on:
  - RUNTIME-151
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-152 — Extract runtime device bootstrap out of Engine

## Goal
- Move runtime device selection, device factory wiring, Vulkan fallback breadcrumb policy, and GPU asset fallback-texture descriptor construction out of `Runtime.Engine.{cppm,cpp}` into a narrow runtime bootstrap module, leaving `Engine` as the composition caller.

## Non-goals
- No change to promoted Vulkan opt-in semantics, Null fallback behavior, device initialization order, renderer initialization order, or GPU asset cache lifetime.
- No new graphics/RHI API and no movement of renderer-owned resource managers.
- No compatibility re-export from `Extrinsic.Runtime.Engine` for the moved pure helpers.

## Context
- Owning subsystem/layer: `runtime`.
- `RUNTIME-146..151` retired the large `Runtime.Engine` decomposition series, but `Runtime.Engine.cpp` still owns device factory logic and the runtime-baked fallback texture bytes. That is specific boot policy bleeding into the composition root.
- The new module may import runtime-allowed lower layers (`core`, `platform-neutral RHI`, `graphics` asset cache) and backend-local factories behind the runtime target's existing private backend link. `Engine` remains responsible for platform window creation and `IDevice::Initialize(...)` because that call needs the live platform window handle.

## Required changes
- [x] Add `Extrinsic.Runtime.DeviceBootstrap` with `RuntimeDeviceSelection`, `SelectRuntimeDeviceBackend(...)`, `ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(...)`, `CreateRuntimeDevice(...)`, and `InitializeRuntimeGpuAssetFallbackTexture(...)`.
- [x] Remove device factory and fallback texture descriptor helpers from `Runtime.Engine.cpp`; import/call the new module instead.
- [x] Remove the moved pure-helper declarations/imports from `Runtime.Engine.cppm`.
- [x] Register the new module interface and implementation in `src/runtime/CMakeLists.txt`.

## Tests
- [x] Update runtime device-selection and Vulkan breadcrumb tests to import `Extrinsic.Runtime.DeviceBootstrap`.
- [x] Add/adjust a layering contract proving `Runtime.Engine.cpp` no longer owns fallback texture bytes or device factory helper bodies.
- [x] Run focused runtime contract tests for device selection, Vulkan breadcrumb, and Engine layering.
- [x] Run the default CPU-supported correctness gate.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Update `src/runtime/README.md` and `tasks/backlog/runtime/README.md` with the factual new bootstrap-module location.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer exports `RuntimeDeviceSelection`, `SelectRuntimeDeviceBackend(...)`, or `ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(...)`.
- [x] `Runtime.Engine.cpp` contains no fallback texture byte array/descriptor builder and no concrete backend factory helper body.
- [x] Existing startup behavior and breadcrumb tests pass unchanged in semantics.
- [x] CPU gate and layering checks pass.

## Status
- Completed on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.DeviceBootstrap` now owns runtime device-selection policy, backend factory dispatch, the Vulkan-requested breadcrumb predicate, and GPU asset fallback-texture descriptor construction.
- `Runtime.Engine` remains the composition caller that creates the platform window, initializes the selected `RHI::IDevice`, records diagnostics, and initializes renderer-facing services.
- PR/commit: pending.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeDeviceSelection.*:RuntimeVulkanBreadcrumb.*:RuntimeEngineLayering.*'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeDeviceSelection|RuntimeVulkanBreadcrumb|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
```

## Forbidden changes
- Changing the device-selection truth table.
- Moving platform window creation or renderer initialization out of `Engine`.
- Adding app/Sandbox/method-specific policy to the bootstrap module.
- Touching selected-editor async/cache work; that remains `RUNTIME-138`.

## Maturity
- Target: `Operational` — behavior-preserving boot-path extraction verified through existing Engine initialization and runtime contract tests. No backend-specific `gpu;vulkan` follow-up is owed because this task does not change Vulkan execution behavior.
