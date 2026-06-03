# GRAPHICS-078E — Visualization-overlay pixel-readback parity harness

## Status
- Status: done locally.
- Completed: 2026-06-03.
- Commit/PR: pending local commit.
- Owner/agent: Codex multi-task GRAPHICS loop.
- Maturity: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.

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
- The CPU/null helper still does not claim source-buffer parity because it has no CPU access to source GPU attribute/scalar buffers. This slice makes the placeholder geometry deterministic so the Vulkan readback harness can sample vector-field and isoline lanes independently until source-BDA expansion lands.

## Result
- Added `IRenderer::SetVisualizationOverlayBackbufferReadbackBuffer(...)` / `GetVisualizationOverlayBackbufferReadbackBuffer()` for caller-owned HostVisible + TransferDst readback buffers.
- Added `RenderGraphFrameStats::VisualizationOverlayBackbufferReadbackCopyCount`, incremented only when an operational frame records `VisualizationOverlayPass` and the visualization readback buffer is armed.
- Extended the post-graph backbuffer copy seam with the visualization-overlay branch without reusing `DefaultRecipeBackbufferReadbackCopyCount` or `TransientDebugBackbufferReadbackCopyCount`.
- Replaced degenerate all-origin visualization placeholder vertices with deterministic vector-field and isoline `LineList` fixture segments while preserving the existing packet counts, push constants, and draw shapes.
- Extended `VisualizationOverlaySurfaceGpuSmoke` with an opt-in readback test that enables debug-view presentation of `SceneColorHDR` and samples vector-field red, isoline green, and clear pixels.

## Required changes
- [x] Add a narrow default-recipe or pass-scoped readback hook/counter suitable for visualization-overlay assertions without reusing the canonical surface-readback diagnostics.
- [x] Provide deterministic vector-field and isoline fixture data that can produce stable sample colors on a real Vulkan frame.
- [x] Keep the hook fail-closed when no buffer is armed and when the device is non-operational.
- [x] Preserve normal renderer behavior when the hook is not armed.

## Tests
- [x] Add `contract;graphics` coverage for the new readback hook/counter fail-closed behavior.
- [x] Add or extend an opt-in `gpu;vulkan;graphics` smoke that reads back the frame and checks visualization-overlay sample colors.
- [x] Preserve `VisualizationOverlaySurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream` as the command-stream/counter proof.

## Docs
- [x] Update `src/graphics/renderer/README.md` and `src/graphics/vulkan/README.md` with the new visualization-overlay readback hook/counter.
- [x] Update `tasks/backlog/rendering/README.md` if this becomes a prerequisite for later overlay parity work.

## Acceptance criteria
- [x] Vulkan readback confirms expected visualization-overlay vector-field and isoline sample colors.
- [x] Counters distinguish command recording from readback copy recording.
- [x] CPU/null tests prove fail-closed behavior without requiring Vulkan.
- [x] The opt-in readback smoke remains outside the default CPU gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'VisualizationOverlayPass' -L 'contract' -L 'graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'VisualizationOverlaySurfaceGpuSmoke' --timeout 120
```

## Forbidden changes
- [x] Did not weaken the existing visualization-overlay command-stream smoke.
- [x] Did not reuse the canonical surface-readback counter as evidence for default-recipe visualization-overlay pixels.
- [x] Did not mix mechanical moves with semantic refactors.

## Maturity
- Reached `Operational` on this Vulkan-capable host for visualization-overlay pixel-readback parity; remains `CPUContracted` everywhere else via CPU/null readback-gate coverage.
- This follow-up is explicitly not required for `GRAPHICS-078` Slice D's command-stream/counter graduation.
