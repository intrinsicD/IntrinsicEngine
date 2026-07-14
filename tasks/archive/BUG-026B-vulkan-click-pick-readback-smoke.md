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
- The smoke must run on a Vulkan-capable host (same policy as `BUG-024B`);
  this retirement was verified on NVIDIA GeForce RTX 3050 / NVIDIA driver
  590.48.01.

## Required changes

- [x] Add a `gpu;vulkan;integration` smoke that submits `SelectionController::RequestClickPick` at the projected triangle center, runs bounded frames, and asserts the controller selected the triangle entity (`SelectedTag`, snapshot id) via the real GPU readback.
- [x] Assert `Engine::GetLastRefinedPrimitiveSelection()` reports `CursorFromDepth` with a world cursor on the triangle plane (within tolerance) and resolved face/vertex/edge ids.
- [x] Add a background-click case asserting a published no-hit clears the selection per controller policy.

## Tests

- [x] New smoke passes on a Vulkan-capable host with validation layers enabled (no validation errors for the depth `TransferSrc` round trip).
- [x] Default CPU gate untouched and green.

## Docs

- [x] Record the host/driver and run command in this task on completion.

## Acceptance criteria

- [x] Click pick on the default sandbox triangle selects it through the real Vulkan readback path.
- [x] Depth-derived world cursor lies on the triangle plane within documented tolerance.
- [x] Background click publishes no-hit and clears selection.

## Status

- Completed 2026-06-11 at maturity `Operational`.
- PR/commit: this retirement commit.
- Host/driver: NVIDIA GeForce RTX 3050, NVIDIA driver 590.48.01, Vulkan API
  1.4.325; validation layer `VK_LAYER_KHRONOS_validation` 1.4.309 present.
- Implementation: `RuntimeSandboxAcceptanceGpuSmoke.ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears`
  waits for the promoted Vulkan device to become operational, submits a
  center-pixel `SelectionController::RequestClickPick`, waits for the real
  GPU readback to select `ReferenceTriangle`, asserts depth-derived
  world/local cursor positions are on the triangle plane within `0.05`, then
  submits a far-background click and waits for the no-hit clear.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ClickPick' -L 'gpu' -L 'vulkan' --timeout 120
```

## Verification results
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ClickPick' -L 'gpu' -L 'vulkan' --timeout 120
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
git diff --check
```

Result: passed on 2026-06-11. The focused CTest selection ran
`RuntimeSandboxAcceptanceGpuSmoke.ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears`
on the NVIDIA RTX 3050 / driver 590.48.01 host and passed 1/1. The default
CPU-supported gate passed 2967/2967.

## Forbidden changes

- Relaxing validation layers or the operational gate.
- Adding the smoke to the default CPU gate.

## Maturity

- Target: `Operational` on Vulkan-capable hosts; this task is the `Operational` owner for `BUG-026`.
