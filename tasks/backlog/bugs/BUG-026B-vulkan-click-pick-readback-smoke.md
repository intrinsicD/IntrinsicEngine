---
id: BUG-026B
theme: G
depends_on: [BUG-026]
maturity_target: Operational
---
# BUG-026B — Vulkan click-pick readback smoke (entity id + depth round trip)

## Goal

- Prove the BUG-026 click-selection fix `Operational` on a Vulkan-capable host: an opt-in `gpu;vulkan` smoke drives a real pick request at the rendered `ReferenceTriangle`, and the GPU readback round-trips a non-zero render id plus a sub-far depth sample that unprojects onto the triangle plane, while a background pick publishes a clean no-hit.

## Non-goals

- No CPU-gate changes; the byte-layout/id/clear contracts are owned by `BUG-026`.
- No hover input bridge.
- No new picking features or selection policy changes.

## Context

- `BUG-026` fixed click selection (render-id `+1` encoding, zero-clear UINT ID targets, `SceneDepth` pixel readback) at `CPUContracted`. The historical coverage gap was that the `gpu;vulkan` smokes only exercised *hierarchy* selection, which bypasses the readback path — so a dead click path shipped while the smokes stayed green.
- Owner/layer: `tests/integration/runtime` GPU smoke against the promoted default recipe; rides `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` infrastructure (bounded `Engine` frames, validation layers on).
- This container has no GPU/display; the smoke must run on a Vulkan-capable host (same policy as `BUG-024B`).

## Required changes

- [ ] Add a `gpu;vulkan;integration` smoke that submits `SelectionController::RequestClickPick` at the projected triangle center, runs bounded frames, and asserts the controller selected the triangle entity (`SelectedTag`, snapshot id) via the real GPU readback.
- [ ] Assert `Engine::GetLastRefinedPrimitiveSelection()` reports `CursorFromDepth` with a world cursor on the triangle plane (within tolerance) and resolved face/vertex/edge ids.
- [ ] Add a background-click case asserting a published no-hit clears the selection per controller policy.

## Tests

- [ ] New smoke passes on a Vulkan-capable host with validation layers enabled (no validation errors for the depth `TransferSrc` round trip).
- [ ] Default CPU gate untouched and green.

## Docs

- [ ] Record the host/driver and run command in this task on completion.

## Acceptance criteria

- [ ] Click pick on the default sandbox triangle selects it through the real Vulkan readback path.
- [ ] Depth-derived world cursor lies on the triangle plane within documented tolerance.
- [ ] Background click publishes no-hit and clears selection.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ClickPick' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Relaxing validation layers or the operational gate.
- Adding the smoke to the default CPU gate.

## Maturity

- Target: `Operational` on Vulkan-capable hosts; this task is the `Operational` owner for `BUG-026`.
