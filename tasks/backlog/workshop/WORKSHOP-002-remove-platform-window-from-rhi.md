# WORKSHOP-002 — Remove platform/window ownership from RHI

## Goal
- Restore the intended `graphics/rhi -> core` boundary by removing direct platform dependency from `ExtrinsicRHI` and moving window/surface-specific initialization responsibility into runtime/backend factory wiring.

## Non-goals
- Do not redesign the entire Vulkan backend.
- Do not implement new rendering features.
- Do not change the public renderer frame lifecycle beyond what is required to remove `Platform::IWindow` from the abstract RHI surface.
- Do not delete legacy Vulkan/RHI paths.

## Context
- `RHI::IDevice` currently imports `Extrinsic.Platform.Window` and takes `Platform::IWindow&` in `Initialize(...)`.
- `src/graphics/rhi/CMakeLists.txt` currently links `ExtrinsicPlatform`.
- This violates the intended promoted contract where `graphics/rhi` depends on `core` only.
- Runtime should own composition between platform window and concrete backend. Abstract RHI should stay backend-neutral and platform-neutral.
- WORKSHOP-001 makes this violation detectable in `check_layering.py --strict`; this task fixes it.

## Required changes
- [ ] Replace `RHI::IDevice::Initialize(Platform::IWindow&, const Core::Config::RenderConfig&)` with a platform-neutral initialization API.
- [ ] Introduce a minimal platform-neutral RHI device creation descriptor, for example `RHI::DeviceCreateDesc`, containing only render config and backend-neutral options.
- [ ] Move window/surface handoff into a concrete backend factory or runtime-owned creation function, for example:
  - `CreateVulkanDevice(Platform::IWindow&, const Core::Config::RenderConfig&)`, or
  - Runtime constructs a backend-specific presentation attachment and passes only backend-neutral state through RHI.
- [ ] Ensure `ExtrinsicRHI` no longer imports any `Extrinsic.Platform.*` module.
- [ ] Ensure `src/graphics/rhi/CMakeLists.txt` no longer links `ExtrinsicPlatform`.
- [ ] Keep Null/headless behavior intact.
- [ ] Update all concrete device implementations and mocks to match the new initialization contract.
- [ ] Update runtime device selection/wiring so platform-aware code lives in `runtime` and/or concrete backend factory code, not in abstract RHI.
- [ ] If a temporary bridge is unavoidable, isolate it outside `graphics/rhi` and document it with a removal task ID.

## Tests
- [ ] Update existing RHI manager tests to compile without `ExtrinsicPlatform` as an RHI dependency.
- [ ] Add or update a contract test proving `src/graphics/rhi/**` contains no `Extrinsic.Platform` imports.
- [ ] Add or update a contract test proving `ExtrinsicRHI` target does not link `ExtrinsicPlatform`.
- [ ] Update runtime device-selection tests to cover the new wiring path.
- [ ] Keep Vulkan fail-closed contract tests compiling when the Vulkan backend target is available.

## Docs
- [ ] Update `docs/architecture/graphics.md` or equivalent RHI architecture doc to state that RHI is platform-neutral.
- [ ] Update runtime subsystem boundary docs to say runtime wires platform windows to concrete backend factories.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` RHI/platform/runtime rows to reflect the corrected ownership.
- [ ] Update generated module inventory if module surfaces changed.

## Acceptance criteria
- [ ] `src/graphics/rhi/RHI.Device.cppm` has no `Extrinsic.Platform.*` import.
- [ ] `src/graphics/rhi/CMakeLists.txt` has no `ExtrinsicPlatform` link edge.
- [ ] `tools/repo/check_layering.py --root src --strict` passes after WORKSHOP-001 is merged.
- [ ] Runtime remains the only promoted layer that knows both platform and concrete graphics backend selection.
- [ ] CPU-supported CI remains green.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not make `graphics/rhi` depend on `platform` through another target name.
- Do not move platform abstractions into `core` just to satisfy the checker.
- Do not introduce Vulkan types into RHI public renderer APIs.
- Do not delete or bypass existing runtime fallback diagnostics.
