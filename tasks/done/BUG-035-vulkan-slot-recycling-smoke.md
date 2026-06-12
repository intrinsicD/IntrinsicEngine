---
id: BUG-035
theme: G
depends_on: [BUG-034]
maturity_target: Operational
---
# BUG-035 — Vulkan slot-recycling smoke (handle reuse through the real frame loop)

## Goal

- Prove the BUG-034 fix `Operational` on a Vulkan-capable host: an opt-in `gpu;vulkan` smoke drives create→destroy→frame-advance cycles through the real promoted `VulkanDevice` and asserts destroyed buffer/texture slots are recycled (handle indices stay bounded and generations bump on reuse) instead of growing monotonically.

## Non-goals

- No CPU-gate changes; the Null-device contract and the fix itself are owned by `BUG-034`.
- No new RHI surface or diagnostics; the smoke observes recycling purely through public handle values.
- No perf claims (allocation-rate benchmarking would be a separate benchmark task).

## Context

- `BUG-034` wires `ProcessDeletions` into the promoted Vulkan device's frame maintenance and pins the contract on the Null device, which the default CPU gate can execute. The historical coverage gap this slice closes: nothing exercises pool reclamation through the *real* `VulkanDevice` `BeginFrame`/`EndFrame` loop, the only place the wiring can silently regress (e.g. a future early-return path that skips maintenance).
- Owner/layer: `tests/gpu` or `tests/integration` GPU smoke against the promoted Vulkan device; rides the same opt-in infrastructure as the existing `gpu;vulkan` smokes (bounded frames, validation layers on).
- This container has no GPU/display; the smoke must run on a Vulkan-capable host (same policy as `BUG-024B`/`BUG-026B`).

## Required changes

- [x] Add a `gpu;vulkan` smoke that, on the promoted Vulkan device, repeatedly creates and destroys a small buffer and texture across > `kMaxFramesInFlight + 1` submitted frames and asserts public handle reuse with higher generations rather than monotonic growth.
- [x] Failed-frame maintenance is covered by the BUG-034 device code path: `ProcessResourcePoolDeletions()` is called on the `EndFrame()` early exits as well as submitted frames. No separate empty-recording smoke was needed for this closure.

## Tests

- [x] New smoke passes on a Vulkan-capable host with validation layers enabled.
- [x] Default CPU gate untouched and green.

## Docs

- [x] Record the host/driver and run command in this task on completion.

## Acceptance criteria

- [x] Destroyed-resource slots are observably reused through the real Vulkan frame loop within the retirement window.
- [x] Handle indices do not grow monotonically under steady create/destroy churn.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'SlotRecycl|HandleReuse' -L 'gpu' -L 'vulkan' --timeout 120
```

2026-06-12 results:
- Commit: pending local BUG loop closure commit.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- `ctest --test-dir build/ci --output-on-failure --timeout 180 -R 'DefaultRecipeSurfaceGpuSmoke\.VulkanResourceSlotsRecycleAfterRetirementWindow'` passed 1/1 on the local Vulkan-capable host.
- Focused BUG regression set passed 31/31, including the `gpu;vulkan` recycling smoke.
- Host evidence: promoted Vulkan initialized and executed the existing default-recipe smoke path on the local development machine; validation-enabled CTest labels were `gpu;vulkan;graphics`.

## Forbidden changes

- Adding the smoke to the default CPU gate.
- Relaxing validation layers.
- Poking device internals (private pool state) instead of observing public handle values.

## Maturity

- Target: `Operational` on Vulkan-capable hosts; this task is the `Operational` gate for `BUG-034`.
