# GRAPHICS-033A — Vulkan operational status seam and reconciliation matrix

## Goal
Land the first GRAPHICS-033 implementation child: introduce the single
graphics-public Vulkan diagnostic seam that everyone else consumes for
operational status. Specifically, add `VulkanOperationalStatusCode`,
`VulkanOperationalReason`, `VulkanOperationalInputs`, `VulkanOperationalStatus`,
and `EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) -> VulkanOperationalStatus`
in `Extrinsic.Backends.Vulkan`, wire `VulkanDevice::IsOperational()` to consume
it, and add CPU `contract;graphics` tests for the ordered gate evaluation and
the full reconciliation truth table from GRAPHICS-033 Decision 4. No
diagnostic counters, no startup breadcrumb, no Vulkan command-recording
bodies, and no behavior change for default CPU-supported builds.

## Non-goals
- No diagnostic snapshot, counters, histogram, or runtime startup
  breadcrumb (those land in GRAPHICS-033B).
- No Vulkan command-recording bodies and no real-device smoke (those land
  in GRAPHICS-033C / GRAPHICS-033D).
- No new CMake options. `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` stays as-is
  and its semantics are unchanged.
- No relaxation of the fail-closed gate. `RHI::IDevice::IsOperational()`
  must still return `false` whenever any required gate item is unsatisfied.
- No re-derivation of operational state from CMake options, config flags,
  bootstrap snapshots, or fallback counters outside this seam.
- No `Vk*` handles in the public seam — `VulkanOperationalInputs` is a
  Vulkan-public aggregate of booleans and reason bits only.
- No live ECS access, no app/renderer policy code added.

## Context
- Owning subsystem/layer: `src/graphics/vulkan`. Runtime consumes the
  seam but does not own the evaluator.
- Architecture rule: `graphics/vulkan -> core, graphics/rhi, backend-local
  Vulkan deps` (`/AGENTS.md` §2). Renderer/runtime reads only
  `RHI::IDevice::IsOperational()` and the existing backend-neutral stats.
- Predecessor: [`GRAPHICS-033`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md)
  Decisions 1, 2, 3, 4, 8, 12, 14 fully specify this slice. Quoted
  enumerations:
  - **Gate checklist** (Decision 1, ordered): build/run gate; volk +
    instance + surface + physical device + queue families; logical device +
    feature chain + `VK_KHR_swapchain` + VMA + heap-budget diagnostics;
    swapchain create/acquire/present/resize/recreate per GRAPHICS-013CQ;
    per-frame command/sync resources; minimal recipe pass-recording
    parity (consumed-only here, executed in 033C); barrier/layout
    validation through GRAPHICS-022; public service reconciliation
    (bindless/transfer/pipeline/swapchain-import/command-context);
    validation-layer gate.
  - **Status codes** (Decision 3): `NotCompiled`, `NotRequested`,
    `RequestedButUnsupported`, `RequestedButFailedInit`,
    `RequestedButValidationFailed`, `RequestedButIncompleteGate`,
    `Operational`. Append-only.
  - **Reason enum** (Decision 3): `MissingInstance`, `MissingSurface`,
    `NoSuitablePhysicalDevice`, `MissingRequiredExtension`,
    `MissingRequiredFeature`, `LogicalDeviceFailed`, `AllocatorFailed`,
    `SwapchainFailed`, `CommandSyncFailed`,
    `MinimalRecipeRecordingMissing`, `BarrierValidationFailed`,
    `PublicServiceReconciliationFailed`, `ValidationLayerError`,
    `DeviceLost`, `SurfaceLost`. Append-only.
  - **Reconciliation matrix** (Decision 4): nine rows mapping
    `(CompiledIn, Requested, HostSupports, InitSucceeded, GateStatus) ->
    (Effective device, Counter increments, Breadcrumb, Runtime result)`.
    Counter and breadcrumb side-effects are wired by GRAPHICS-033B; this
    task only enforces the device-selection column.

## Required changes
- [ ] Add the new types and free function to `src/graphics/vulkan/`. Export
  them from `Extrinsic.Backends.Vulkan` as the single public seam.
- [ ] Wire `VulkanDevice::IsOperational()` to consult
  `EvaluateVulkanOperationalStatus(...)` exactly once per evaluation
  (init, swapchain/device recreate, and explicit transition checks per
  GRAPHICS-033 Decision 12).
- [ ] Wire runtime's `SelectRuntimeDeviceBackend()` in
  `src/runtime/Runtime.Engine.cppm`/`.cpp` to consult the status code
  for device selection. Runtime must not branch on `VkResult`, native
  handles, or any non-public Vulkan symbol.
- [ ] Define and document the "first failing reason" rule (the reason
  returned must correspond to the earliest unsatisfied gate item).
- [ ] Make sure all current call sites that previously consulted booleans,
  CMake flags, or fallback counters to decide operational state now go
  through the new seam. Remove any duplicate gate checks in renderer,
  runtime, app, or asset-service code.

## Tests
- [ ] New `tests/contract/graphics/Test.VulkanOperationalGate.cpp`
  (CTest labels `contract;graphics`): table-driven property tests over
  every reconciliation-matrix row in Decision 4 and over the ordered
  gate items in Decision 1. Inputs are synthesized `VulkanOperationalInputs`
  values (no real Vulkan device, no GLFW).
- [ ] New `tests/contract/graphics/Test.VulkanOperationalReason.cpp`
  (labels `contract;graphics`): asserts the first-failing-reason
  selection for each ordering permutation in the gate checklist, and
  asserts enum values are stable / append-only by comparing to a
  baseline list copied from this task.
- [ ] Update the existing `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`
  to consume the new status code rather than a bare bool, without
  changing its observable assertions.
- [ ] Default CPU gate
  (`ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`)
  must include the new tests and pass.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to document the
  `VulkanOperationalStatusCode` / `VulkanOperationalReason` enums and the
  single-source-of-truth rule (no other code may re-derive operational
  state).
- [ ] Update `docs/architecture/graphics.md` and
  `docs/architecture/rendering-three-pass.md` operational-readiness
  sections to point at the new seam.
- [ ] Refresh `docs/api/generated/module_inventory.md` with
  `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] No changes to `docs/migration/nonlegacy-parity-matrix.md` rows in this
  slice (Vulkan operational row stays "pending"; GRAPHICS-033B flips
  diagnostics columns; GRAPHICS-033C flips the recording column).

## Acceptance criteria
- [ ] `Extrinsic.Backends.Vulkan` exports the five new symbols
  (`VulkanOperationalStatusCode`, `VulkanOperationalReason`,
  `VulkanOperationalInputs`, `VulkanOperationalStatus`,
  `EvaluateVulkanOperationalStatus`) and the existing operational call
  sites consume the seam unchanged in behavior.
- [ ] `VulkanDevice::IsOperational()` does not duplicate any gate check that
  the seam already evaluates.
- [ ] Runtime device selection consults only the status code; no `Vk*`
  symbols are visible to `src/runtime/`.
- [ ] The Vulkan backend remains fail-closed: `IsOperational()` returns
  `false` for every state where the gate is unsatisfied, including all
  rows of the reconciliation matrix except `Operational`.
- [ ] New `contract;graphics` tests cover every row of the reconciliation
  matrix and every first-failing-reason ordering.
- [ ] Layering, test-layout, task-validator, and module-inventory checks
  pass.
- [ ] The default CPU gate remains green.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'contract' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Flipping the operational gate to `true` without satisfying the full
  ordered checklist (Decision 1).
- Reintroducing duplicate gate checks anywhere in renderer, runtime,
  app, or asset-service code.
- Exposing `Vk*` handles or `VkResult` through the new seam or through
  `src/runtime/`.
- Adding diagnostic counters, snapshots, or breadcrumbs — those belong
  to GRAPHICS-033B.
- Adding Vulkan command-recording bodies — those belong to
  GRAPHICS-033C.
- Adding new CMake options or changing
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` semantics.
- Live ECS access from `src/graphics/vulkan/*`.
