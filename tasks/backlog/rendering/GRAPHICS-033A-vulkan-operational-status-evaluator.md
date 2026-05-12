# GRAPHICS-033A — Vulkan operational-status evaluator surface

## Goal
- Add the single-source operational-status seam declared by `GRAPHICS-033`: `EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) -> VulkanOperationalStatus`, exposing `VulkanOperationalStatusCode` (`NotCompiled`, `NotRequested`, `RequestedButUnsupported`, `RequestedButFailedInit`, `RequestedButValidationFailed`, `RequestedButIncompleteGate`, `Operational`) and `VulkanOperationalReason` (`MissingInstance`, `MissingSurface`, `NoSuitablePhysicalDevice`, `MissingRequiredExtension`, `MissingRequiredFeature`, `LogicalDeviceFailed`, `AllocatorFailed`, `SwapchainFailed`, `CommandSyncFailed`, `MinimalRecipeRecordingMissing`, `BarrierValidationFailed`, `PublicServiceReconciliationFailed`, `ValidationLayerError`, `DeviceLost`, `SurfaceLost`).

## Non-goals
- No runtime breadcrumb wiring (`GRAPHICS-033B`).
- No Vulkan command-recording bodies for the GRAPHICS-032 minimal recipe (`GRAPHICS-033C`).
- No `gpu;vulkan` smoke (`GRAPHICS-033D`).
- No mutation of `VulkanDevice::IsOperational()` consumers; the evaluator is the source of truth but `IsOperational()` remains backed by it for backward compatibility.

## Context
- Status: not started.
- Owner/layer: `graphics/vulkan` for the evaluator + types.
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-A in the parent's Required changes.
- The 9-step gate ordering and the runtime-reconciliation truth table are already locked in `src/graphics/vulkan/README.md:326–386`.

## Required changes
- [ ] Declare `VulkanOperationalInputs` (CPU-public, no `Vk*` handles) capturing the booleans needed by each gate step (compiledIn, requested, hostSupports, initSucceeded, swapchainReady, minimalRecipeRecordingPresent, barrierValidationClean, publicServiceReconciled, validationClean, deviceLost, surfaceLost).
- [ ] Declare `VulkanOperationalStatus { VulkanOperationalStatusCode Code; VulkanOperationalReason Reason; }`.
- [ ] Implement `EvaluateVulkanOperationalStatus(...)` in the documented gate order: each step that fails returns the corresponding `Reason` paired with the appropriate `Code`. Only when all 9 gates pass returns `{ Code = Operational, Reason = None }`.
- [ ] Wire `VulkanDevice::IsOperational()` to consume `EvaluateVulkanOperationalStatus(BuildOperationalInputs())` so backend code has one source of truth.

## Tests
- [ ] `contract;graphics` test: each gate-failure path returns the documented `Reason` (one row per truth-table line).
- [ ] `contract;graphics` test: `Operational` is reachable only when all gate inputs are true; flipping any single input back to false returns to a non-operational status.
- [ ] `contract;graphics` test: `EvaluateVulkanOperationalStatus` is total (every input combination returns a defined status; no UB / undefined enum values).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to flip the planned `EvaluateVulkanOperationalStatus` row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the symbol.

## Acceptance criteria
- [ ] The evaluator is the single source of truth consumed by `VulkanDevice::IsOperational()`.
- [ ] All 7 status codes and all 15 reason values are reachable through tests.
- [ ] No new RHI-visible enum or branching seam for renderer/runtime.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicBackendsVulkanContractTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Exposing `Vk*` types through the evaluator.
- Adding runtime breadcrumbs (reserved for `GRAPHICS-033B`).
- Adding any Vulkan command-recording body (reserved for `GRAPHICS-033C`).

## Next verification step
- Add the types + evaluator + IsOperational rewire, exercise the contract tests above.
