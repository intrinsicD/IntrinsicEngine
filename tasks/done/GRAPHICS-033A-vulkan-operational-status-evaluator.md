# GRAPHICS-033A — Vulkan operational-status evaluator surface

## Goal
- Add the single-source operational-status seam declared by `GRAPHICS-033`: `EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) -> VulkanOperationalStatus`, exposing `VulkanOperationalStatusCode` (`NotCompiled`, `NotRequested`, `RequestedButUnsupported`, `RequestedButFailedInit`, `RequestedButValidationFailed`, `RequestedButIncompleteGate`, `Operational`) and `VulkanOperationalReason` (`MissingInstance`, `MissingSurface`, `NoSuitablePhysicalDevice`, `MissingRequiredExtension`, `MissingRequiredFeature`, `LogicalDeviceFailed`, `AllocatorFailed`, `SwapchainFailed`, `CommandSyncFailed`, `MinimalRecipeRecordingMissing`, `BarrierValidationFailed`, `PublicServiceReconciliationFailed`, `ValidationLayerError`, `DeviceLost`, `SurfaceLost`).

## Non-goals
- No runtime breadcrumb wiring (`GRAPHICS-033B`).
- No Vulkan command-recording bodies for the GRAPHICS-032 minimal recipe (`GRAPHICS-033C`).
- No `gpu;vulkan` smoke (`GRAPHICS-033D`).
- No mutation of `VulkanDevice::IsOperational()` consumers; the evaluator is the source of truth but `IsOperational()` remains backed by it for backward compatibility.

## Context
- Status: done.
- Owner/agent: Claude on branch `claude/setup-agentic-workflow-obMTZ`.
- Owner/layer: `graphics/vulkan` for the evaluator + types.
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-A in the parent's Required changes.
- The 9-step gate ordering and the runtime-reconciliation truth table are already locked in `src/graphics/vulkan/README.md:326–386`.

## Required changes
- [x] Declare `VulkanOperationalInputs` (CPU-public, no `Vk*` handles) capturing the booleans needed by each gate step (compiledIn, requested, hostSupports*, logical-device/allocator/swapchain/command-sync readiness, minimalRecipeRecordingPresent, barrierValidationClean, publicServiceReconciled, validationClean, deviceLost, surfaceLost). Declared in `src/graphics/vulkan/Backends.Vulkan.cppm`.
- [x] Declare `VulkanOperationalStatus { VulkanOperationalStatusCode Code; VulkanOperationalReason Reason; }`. Enums are append-only and exported from `Extrinsic.Backends.Vulkan`.
- [x] Implement `EvaluateVulkanOperationalStatus(...)` in the documented gate order in `src/graphics/vulkan/Backends.Vulkan.OperationalStatus.cpp`. Lifecycle loss (`DeviceLost`/`SurfaceLost`) short-circuits to `RequestedButFailedInit`; validation-layer failures fail closed before incomplete-gate reasons; gates 8–10 (`MinimalRecipeRecordingPresent`, `BarrierValidationClean`, `PublicServiceReconciled`) map to `RequestedButIncompleteGate`.
- [x] Wire `VulkanDevice::IsOperational()` through `ComputeOperationalPredicate()` to consume `EvaluateVulkanOperationalStatus(BuildOperationalInputs())`. Until `GRAPHICS-033B`/`033C` land, the higher-gate inputs stay `false`, preserving the existing fail-closed contract; `HasLiveOperationalPrerequisites()` is retained for the finer-grained transfer-queue / buffer / texture fail-closed paths that need it independently of the binary operational predicate.

## Tests
- [x] `contract;graphics` test: each gate-failure path returns the documented `Reason` (one row per truth-table line). Implemented as the `MissingX`/`XNotReady` cases in `tests/contract/graphics/Test.VulkanOperationalStatusEvaluator.cpp`, covering all 15 reason values.
- [x] `contract;graphics` test: `Operational` is reachable only when all gate inputs are true; flipping any single input back to false returns to a non-operational status (`AllInputsTrueReachesOperational` + `FlippingAnyGateInputDropsBackToNonOperational`).
- [x] `contract;graphics` test: `EvaluateVulkanOperationalStatus` is total — brute-forces all 2^17 input combinations, asserts every returned `Code`/`Reason` is within the declared enum range, and asserts the documented pairing constraints (`Operational`/`NotCompiled`/`NotRequested` carry `None`; every other code carries a non-`None` reason). (`IsTotalAcrossAllBooleanCombinations`.)
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/graphics/vulkan/README.md` to flip the planned `EvaluateVulkanOperationalStatus` row to current state.
- [x] Refresh `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py` after adding the symbol.

## Acceptance criteria
- [x] The evaluator is the single source of truth consumed by `VulkanDevice::IsOperational()`.
- [x] All 7 status codes and all 15 reason values are reachable through tests.
- [x] No new RHI-visible enum or branching seam for renderer/runtime — types live under `Extrinsic.Backends.Vulkan` and `RHI::IDevice::IsOperational()` keeps its bool contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsVulkanContractTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Note: `IntrinsicGraphicsVulkanContractTests` is the umbrella executable that
links `GraphicsVulkanContractTestObjs` (the planning-task entry referenced
`ExtrinsicBackendsVulkanContractTests`, which does not exist as a target).

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Exposing `Vk*` types through the evaluator.
- Adding runtime breadcrumbs (reserved for `GRAPHICS-033B`).
- Adding any Vulkan command-recording body (reserved for `GRAPHICS-033C`).

## Completion
- Completed: 2026-05-13.
- Commit reference: `7a5886d` ("GRAPHICS-033A Add Vulkan operational-status evaluator surface") via PR #820 from `claude/setup-agentic-workflow-obMTZ`, merged to `main` at 2026-05-13T13:16:28Z.
- Verification:
  - Project CI ran on PR #820 (`ci` preset, clang-20 toolchain) and passed before merge to `main`.
  - Authoring session ran the structural checks locally; the focused `cmake --preset ci` / `ctest -L contract` gate ran in the PR's CI environment because the authoring container shipped clang-18 only.
  - `docs/api/generated/module_inventory.md` was regenerated as part of the PR.
