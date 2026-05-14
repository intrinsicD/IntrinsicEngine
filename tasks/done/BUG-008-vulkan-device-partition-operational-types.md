# BUG-008 — Vulkan Device partition cannot name `VulkanOperationalInputs`

## Goal
- Restore `ExtrinsicBackendsVulkan` compilation under clang-20 by giving the `:Device` partition module-local access to the operational-status types it declares in member function signatures, without exposing the private `BuildOperationalInputs()` method through `IDevice`.

## Non-goals
- No semantic changes to the operational-status evaluator, diagnostics counters, or runtime breadcrumb behavior (covered by `GRAPHICS-033A`/`B`/`C`).
- No relaxation of the layering invariant `graphics/vulkan -> graphics/rhi -> core`.
- No new exported symbols beyond what was already public on `Extrinsic.Backends.Vulkan`.
- No widening of `IDevice` to expose backend-private state.

## Context
- Status: done.
- Owner/agent: Claude (session `claude/inspect-engine-state-k3aik`).
- Owner/layer: `src/graphics/vulkan` (module structure only).
- Symptom: `cmake --build --preset ci --target ExtrinsicBackendsVulkan` fails under clang-20.1.2 with
  ```
  src/graphics/vulkan/Backends.Vulkan.Device.cppm:177:23:
    error: unknown type name 'VulkanOperationalInputs'
      [[nodiscard]] VulkanOperationalInputs BuildOperationalInputs() const noexcept;
                    ^
  ```
  After moving the type, a follow-on diagnostic surfaces in the implementation TU:
  ```
  src/graphics/vulkan/Backends.Vulkan.Device.cpp:718:57:
    error: 'BuildOperationalInputs' is a private member of
      'Extrinsic::Backends::Vulkan::VulkanDevice'
  ```
- Repro command:
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target ExtrinsicBackendsVulkan
  ```
  Both diagnostics reproduce on baseline `c4c63ca` (no work-in-progress slice required).
- Root cause:
  - Module-partition visibility: `Backends.Vulkan.cppm` is the primary module interface for `Extrinsic.Backends.Vulkan` and declares `VulkanOperationalInputs` directly inside the primary. `Backends.Vulkan.Device.cppm` is the `:Device` partition. Under C++20 module rules a partition cannot `import` the primary module of its own module (the partition is itself part of that primary's interface), so the partition cannot name a type defined exclusively in the primary translation unit. Clang-20 enforces this strictly; earlier compilers accepted the construct.
  - Private access: `BuildOperationalInputs()` was declared `private` inside `VulkanDevice`, but the umbrella's `EvaluateVulkanDeviceOperationalStatus(const RHI::IDevice*)` (defined in `Backends.Vulkan.Device.cpp`) accesses it through a `static_cast<const VulkanDevice&>`. Same module ≠ friend; clang-20 reports this as a private-access violation.
- Impact: blocks the Theme A Vulkan-side targets (`ExtrinsicBackendsVulkan`, `IntrinsicGraphicsVulkanContractTests`) from compiling, which forces the contract gate to run with `-LE 'vulkan'` excluded and silently masks the `VulkanOperationalStatusEvaluator` / `VulkanOperationalDiagnosticsSnapshot` / `VulkanFailClosedContract` coverage.

## Required changes
- [x] Extract the operational-status surface (types, evaluator, diagnostics functions) into a new partition `Extrinsic.Backends.Vulkan:OperationalStatus` at `src/graphics/vulkan/Backends.Vulkan.OperationalStatus.cppm`. Move `VulkanOperationalStatusCode`, `VulkanOperationalReason`, `kVulkanOperationalReasonCount`, `VulkanOperationalInputs`, `VulkanOperationalStatus`, the `ToString(...)` overloads, `EvaluateVulkanOperationalStatus`, `VulkanOperationalDiagnosticsSnapshot`, `GetVulkanOperationalDiagnosticsSnapshot`, `RecordVulkanOperationalFallback`, `NoteVulkanOperationalDeviceLostDrop`, and `EvaluateVulkanDeviceOperationalStatus`.
- [x] Replace the umbrella's redeclarations with `export import :OperationalStatus;` so consumers that `import Extrinsic.Backends.Vulkan` continue to see every symbol verbatim.
- [x] Add `export import :OperationalStatus;` to `Backends.Vulkan.Device.cppm` so the `:Device` partition can name `VulkanOperationalInputs` in `BuildOperationalInputs()`'s signature.
- [x] Declare `EvaluateVulkanDeviceOperationalStatus` as a friend of `VulkanDevice` so it can call the private `BuildOperationalInputs()` from `Backends.Vulkan.Device.cpp` without widening the `IDevice` surface.
- [x] Register `Backends.Vulkan.OperationalStatus.cppm` in `src/graphics/vulkan/CMakeLists.txt`.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Tests
- [x] `cmake --build --preset ci --target ExtrinsicBackendsVulkan` succeeds under clang-20.
- [x] `cmake --build --preset ci --target IntrinsicGraphicsVulkanContractTests` succeeds.
- [x] `bin/IntrinsicGraphicsVulkanContractTests` runs cleanly (44/44 passing, no behavioral regression on the evaluator/diagnostics truth table).
- [x] `ctest --test-dir build/ci -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` reports the Vulkan-side fixtures alongside the rest of the contract gate without regression.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` to list `Extrinsic.Backends.Vulkan:OperationalStatus`.
- [x] No README changes required: the public surface of `Extrinsic.Backends.Vulkan` is unchanged (every previously listed symbol is still exported, just via the partition).

## Acceptance criteria
- [x] `ExtrinsicBackendsVulkan` builds under clang-20 with sanitizers enabled.
- [x] No public symbol churn for consumers (`import Extrinsic.Backends.Vulkan` continues to resolve every operational symbol).
- [x] No semantic change to the operational gate truth table; `VulkanOperationalStatusEvaluator` and `VulkanOperationalDiagnosticsSnapshot` tests still pass byte-identically.
- [x] Layering invariants intact (`graphics/vulkan -> graphics/rhi -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicBackendsVulkan IntrinsicGraphicsVulkanContractTests \
    IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Widening `IDevice` to expose backend-private state.
- Adding new exported symbols beyond what was already public on `Extrinsic.Backends.Vulkan`.
- Inlining operational-status definitions inside `Backends.Vulkan.Device.cppm` (would entangle the partition with diagnostics types it does not own).
- Relaxing layering checks or adding allowlist entries.

## Completion
- Completed: 2026-05-14.
- Commit reference: pending (this session's commit on `claude/inspect-engine-state-k3aik`).
- Verification:
  - All commands above ran clean.
  - `IntrinsicGraphicsVulkanContractTests` ran 44/44 passing.
  - `ctest -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'` reported 222/224; the two remaining failures (`MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailable...`, `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence`) are tracked separately and unrelated to the module-partition wiring.
