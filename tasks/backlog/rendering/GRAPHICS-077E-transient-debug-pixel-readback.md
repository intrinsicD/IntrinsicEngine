# GRAPHICS-077E — Transient-debug pixel-readback parity harness

## Goal
- Add an opt-in Vulkan pixel-readback harness proving the `TransientDebugSurfacePass` triangle, line, and point lanes produce expected sample colors on the swapchain/backbuffer, beyond the command-stream/counter proof from `GRAPHICS-077` Slice D.

## Non-goals
- No new transient-debug pass bodies or lane semantics; `GRAPHICS-077` owns command-stream operational proof.
- No routing through retained line/point cull buckets.
- No `GpuWorld` retention for transient debug buffers.
- No visualization-overlay assertions; `GRAPHICS-078E` owns that sibling path.

## Context
- Owner/layer: `graphics/renderer` test seam plus opt-in `gpu;vulkan;graphics` coverage.
- `GRAPHICS-077` Slice D added `TransientDebugSurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream`, which warms the default recipe, manually submits one triangle/line/point packet, drives a real Vulkan frame, and asserts the pass records with non-zero per-lane counters.
- The existing public readback seam is `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(...)`, intentionally scoped to `FrameRecipeKind::MinimalDebug`. This follow-up exists so `GRAPHICS-077` can close without silently expanding Slice D into a renderer readback API change.

## Required changes
- [ ] Add a narrow default-recipe or pass-scoped readback hook/counter suitable for transient-debug assertions without reusing the MinimalDebug-only counter.
- [ ] Keep the hook fail-closed when no buffer is armed and when the device is non-operational.
- [ ] Preserve normal renderer behavior when the hook is not armed.
- [ ] Use deterministic sample points/colors that isolate at least one triangle, one line, and one point contribution.

## Tests
- [ ] Add `contract;graphics` coverage for the new readback hook/counter fail-closed behavior.
- [ ] Add or extend an opt-in `gpu;vulkan;graphics` smoke that reads back the frame and checks transient-debug sample colors.
- [ ] Preserve `TransientDebugSurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream` as the command-stream/counter proof.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and `src/graphics/vulkan/README.md` with the new transient-debug readback hook/counter.
- [ ] Update `tasks/backlog/rendering/README.md` if this becomes a prerequisite for a later scaffold-retirement task.

## Acceptance criteria
- [ ] Vulkan readback confirms expected transient-debug triangle/line/point sample colors.
- [ ] Counters distinguish command recording from readback copy recording.
- [ ] CPU/null tests prove fail-closed behavior without requiring Vulkan.
- [ ] The opt-in readback smoke remains outside the default CPU gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'TransientDebugSurfaceGpuSmoke' --timeout 120
```

## Forbidden changes
- Weakening the existing transient-debug command-stream smoke.
- Reusing `MinimalDebugBackbufferReadbackCopyCount` as evidence for default-recipe transient-debug pixels.
- Mixing mechanical moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for transient-debug pixel-readback parity; `CPUContracted` everywhere else.
- This follow-up is explicitly not required for `GRAPHICS-077` Slice D's command-stream/counter graduation.

