# GRAPHICS-033B — Vulkan operational diagnostics snapshot and startup breadcrumb

## Goal
Land the second GRAPHICS-033 implementation child: add the
`VulkanOperationalDiagnosticsSnapshot` exported by
`Extrinsic.Backends.Vulkan` with the process-monotonic counters and
reason histogram locked by GRAPHICS-033 Decision 5, and wire the runtime
`VulkanRequestedButNotOperational` startup warn breadcrumb that fires
once per engine initialization attempt when `Requested == true` and the
effective device is Null. Adds `contract;runtime` tests for requested-
Vulkan → Null fallback emission and `contract;graphics` tests for
counter/reason-histogram increments per reconciliation-matrix row. No
new operational gate logic; consumes GRAPHICS-033A.

## Non-goals
- No changes to operational gate evaluation or to
  `EvaluateVulkanOperationalStatus(...)` semantics (owned by
  GRAPHICS-033A).
- No Vulkan command-recording bodies (GRAPHICS-033C) and no real-device
  smoke (GRAPHICS-033D).
- No new CMake options. No change to
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` semantics.
- No relaxation of fail-closed behavior; counters and breadcrumbs are
  observational only.
- No migration of GRAPHICS-018Q rate-limited per-path breadcrumbs.
  Frame-loop fail-closed paths keep their existing breadcrumb policy;
  this slice adds only the startup-emission counterpart.
- No live ECS access from `src/graphics/vulkan/*`.
- No human-readable string formatting on per-frame hot paths.

## Context
- Owning subsystem/layer: `src/graphics/vulkan` for the snapshot;
  `src/runtime` for breadcrumb emission only.
- Depends on: [`GRAPHICS-033A`](GRAPHICS-033A-vulkan-operational-status-seam.md)
  for the status / reason enums and the evaluator that drives the
  reconciliation matrix.
- Planning lock: [`GRAPHICS-033`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md)
  Decision 5 specifies counter names exactly:
  - `VulkanFallbackToNullCount`
  - `VulkanInitFailureCount`
  - `VulkanValidationErrorCount`
  - `VulkanOperationalGateFailureCount`
  - `VulkanDeviceLostOperationalDropCount`
  - Plus a fixed-size histogram indexed by `VulkanOperationalReason`.
- Decision 4 truth table specifies which counters increment per matrix
  row; Decision 5 mandates the single startup breadcrumb name
  `VulkanRequestedButNotOperational` with the `VulkanOperationalStatusCode`
  + first failing reason as payload.
- Performance constraint (Decision 12): counters are process-monotonic
  atomics or fixed-size arrays; no per-frame allocations or string work.

## Required changes
- [ ] Add `VulkanOperationalDiagnosticsSnapshot` to
  `src/graphics/vulkan/` and export it via `Extrinsic.Backends.Vulkan`.
  Snapshot is read-only; the backend exposes a `Get*()` accessor that
  returns a copy of the current counter values.
- [ ] Add storage for the five counters and the per-reason histogram inside
  the Vulkan backend (atomics for counter increments; the histogram is
  also append-only / fixed-size keyed by `VulkanOperationalReason`).
- [ ] Increment counters at each reconciliation-matrix transition exactly as
  Decision 4 prescribes. Counter increment is the only side-effect of
  status evaluation; status code derivation remains in GRAPHICS-033A.
- [ ] Wire `Runtime.Engine.cpp` to emit one `VulkanRequestedButNotOperational`
  warn breadcrumb at initialization when `Requested == true` and the
  effective device is Null, carrying the status code and first failing
  reason. Repeat-emission guard: at most once per engine initialization
  attempt. After a successful device recreate that flips operational to
  `true → false`, runtime emits exactly one additional breadcrumb of the
  same kind (this is the "transient drop" case from Decision 10).
- [ ] Runtime breadcrumb formatting reads enum values only; no `vk*` call,
  no native handle inspection.

## Tests
- [ ] New `tests/contract/graphics/Test.VulkanOperationalDiagnostics.cpp`
  (labels `contract;graphics`): drives each reconciliation-matrix row
  via synthesized `VulkanOperationalInputs`, asserts the exact counter
  delta set and the histogram bin incremented matches Decision 4 +
  Decision 5.
- [ ] New `tests/contract/runtime/Test.VulkanStartupBreadcrumb.cpp`
  (labels `contract;runtime`): drives runtime initialization against a
  mock Vulkan backend that reports each non-operational status, asserts
  exactly one `VulkanRequestedButNotOperational` breadcrumb is emitted
  per init attempt with the expected status/reason payload, and asserts
  no breadcrumb is emitted when `Requested == false` or when the
  effective device is Vulkan.
- [ ] New `tests/contract/runtime/Test.VulkanTransitionBreadcrumb.cpp`
  (labels `contract;runtime`): simulates an `Operational → DeviceLost`
  transition and asserts one breadcrumb emission with the correct reason
  and `VulkanDeviceLostOperationalDropCount` increment.
- [ ] All new tests run under the default CPU gate; no GPU labels.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to enumerate the snapshot
  counters and to document that they are read-only public diagnostics.
- [ ] Update `docs/architecture/graphics.md` (Vulkan operational diagnostics
  section): list the five counters and the breadcrumb name.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` rows for "Vulkan
  operational diagnostics" to mark this slice complete.
- [ ] Refresh `docs/api/generated/module_inventory.md` after the new module
  surface lands.

## Acceptance criteria
- [ ] `Extrinsic.Backends.Vulkan` exports
  `VulkanOperationalDiagnosticsSnapshot` and a public accessor.
- [ ] Each reconciliation-matrix row produces the exact counter and
  histogram side-effects required by GRAPHICS-033 Decisions 4 and 5.
- [ ] `Runtime.Engine` emits the startup breadcrumb at most once per
  initialization attempt and exactly once on every
  `Operational → non-operational` transition.
- [ ] Backend remains fail-closed; `IsOperational()` is unchanged by this
  slice except for the inclusion of the snapshot reset on init/recreate.
- [ ] New `contract;graphics` and `contract;runtime` tests pass in the
  default CPU gate; existing tests continue to pass.
- [ ] No `Vk*` symbol crosses `src/runtime/`.
- [ ] Layering, test-layout, task-validator, and module-inventory checks
  pass.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'contract' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Changing `EvaluateVulkanOperationalStatus(...)` or the gate-checklist
  semantics — those belong to GRAPHICS-033A.
- Adding any Vulkan command-recording body — that belongs to
  GRAPHICS-033C.
- Allocating strings or formatting log messages on per-frame paths.
- Emitting more than one startup breadcrumb per init attempt, or
  emitting a transition breadcrumb on routine swapchain resize that
  does not change the operational status.
- Exposing `Vk*` handles, `VkResult`, or any native symbol through the
  snapshot or to `src/runtime/`.
- Live ECS access from `src/graphics/vulkan/*`.
- Adding new CMake options or changing
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` semantics.
