# GRAPHICS-078E — Visualization-overlay pixel-readback parity harness

## Goal
- Add an opt-in Vulkan pixel-readback harness proving the `VisualizationOverlayPass` vector-field and isoline lanes produce expected sample colors on the swapchain/backbuffer, beyond the command-stream/counter proof from `GRAPHICS-078` Slice D.

## Non-goals
- No new visualization-overlay pass bodies or lane semantics; `GRAPHICS-078` owns command-stream operational proof.
- No Htex, UV-bake, or PropertySet adapter work.
- No routing through retained line/point cull buckets.
- No transient-debug assertions; `GRAPHICS-077E` owns that sibling path.

## Context
- Owner/layer: `graphics/renderer` test seam plus opt-in `gpu;vulkan;graphics` coverage.
- `GRAPHICS-078` Slice D added `VisualizationOverlaySurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream`, which warms the default recipe, manually submits one vector-field and one isoline packet, drives a real Vulkan frame, and asserts the pass records with non-zero per-kind counters.
- The current CPU/null helper writes placeholder vertices because it has no CPU access to source GPU attribute/scalar buffers; pixel parity needs a deliberate Vulkan-capable readback seam and deterministic fixture data.

## Required changes
- [ ] Add a narrow default-recipe or pass-scoped readback hook/counter suitable for visualization-overlay assertions without reusing MinimalDebug-only diagnostics.
- [ ] Provide deterministic vector-field and isoline fixture data that can produce stable sample colors on a real Vulkan frame.
- [ ] Keep the hook fail-closed when no buffer is armed and when the device is non-operational.
- [ ] Preserve normal renderer behavior when the hook is not armed.

## Tests
- [ ] Add `contract;graphics` coverage for the new readback hook/counter fail-closed behavior.
- [ ] Add or extend an opt-in `gpu;vulkan;graphics` smoke that reads back the frame and checks visualization-overlay sample colors.
- [ ] Preserve `VisualizationOverlaySurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream` as the command-stream/counter proof.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and `src/graphics/vulkan/README.md` with the new visualization-overlay readback hook/counter.
- [ ] Update `tasks/backlog/rendering/README.md` if this becomes a prerequisite for later overlay parity work.

## Acceptance criteria
- [ ] Vulkan readback confirms expected visualization-overlay vector-field and isoline sample colors.
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
LSAN_OPTIONS=suppressions=$PWD/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'VisualizationOverlaySurfaceGpuSmoke' --timeout 120
```

## Forbidden changes
- Weakening the existing visualization-overlay command-stream smoke.
- Reusing `MinimalDebugBackbufferReadbackCopyCount` as evidence for default-recipe visualization-overlay pixels.
- Mixing mechanical moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for visualization-overlay pixel-readback parity; `CPUContracted` everywhere else.
- This follow-up is explicitly not required for `GRAPHICS-078` Slice D's command-stream/counter graduation.

