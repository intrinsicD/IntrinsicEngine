# GRAPHICS-018 — Vulkan renderer integration

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-017` completion.
- Current slice: renderer frame lifecycle now delegates `BeginFrame`/`EndFrame` and backbuffer import through `RHI::IDevice`, brackets render-graph execution with the frame graphics command context, records the canonical `CullingPass` command sequence on operational devices, routes `DepthPrepass` after culling when its pipeline is ready, reports structured command-recording diagnostics with CPU/mock contract coverage, keeps the promoted Vulkan `IDevice` lifecycle/services/resource overrides symbol-complete and fail-closed until full device/swapchain/resource bring-up lands, has guarded real texture allocation/view/upload and `VkSampler` creation paths for future operational Vulkan devices, and drains live backend resource pools during shutdown. Keep Vulkan opt-in and preserve the CPU/null correctness gate.
- Nonblocking questions: tracked in `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`.

## Goal
- Wire concrete Vulkan backend execution behind the promoted graphics interfaces and default frame recipe.
## Non-goals
- No CPU-only contract behavior changes that require Vulkan.
- No mandatory GPU/Vulkan tests in the default CPU gate.
- No platform-window ownership move into renderer passes.
## Context
- Owner: `src/graphics/vulkan`, `src/graphics/rhi`, `src/graphics/framegraph`, and renderer backend wiring.
- CPU/null contracts should be stable before Vulkan execution is tightened.
## Required changes
- Connect Vulkan device, swapchain, surface, command context, descriptors, pipelines, barriers, transfer, and profiler support to canonical frame execution.
- Implement Vulkan descriptor/scene-table integration for renderable-instance, transform, bounds/culling, material, geometry, and light SSBOs.
- Add resize, swapchain recreation, presentation, and backend-failure diagnostics.
- Keep backend-specific code behind RHI/backend seams.
## Tests
- Add opt-in `gpu`/`vulkan` tests for device creation, swapchain lifecycle, command recording, barriers, transfer, resize, and presentation where environment support exists.
- Add opt-in Vulkan smoke coverage for scene-table descriptor binding and GPU-driven culling/draw-bucket execution.
- Preserve CPU/null tests for all backend-independent behavior.
- Label Vulkan/GPU smoke tests `gpu;vulkan` so they remain opt-in; keep backend-independent CPU tests under `unit;graphics` or `contract;graphics` so they continue to run in the default CPU gate.
## Docs
- Document GPU verification requirements, headless/fallback behavior, and Vulkan label policy.
## Acceptance criteria
- Vulkan backend can execute the canonical frame recipe when opt-in GPU tests are enabled.
- CPU-supported tests remain independent of Vulkan availability.
- Backend failures are surfaced through structured diagnostics.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R '^RendererRhiBoundary\.' --timeout 60

# Optional non-headless Vulkan backend sanity check. Use only a configured build tree
# whose C++23 compiler/toolchain has been confirmed current; do not use stale trees
# with older compilers as verification evidence.
compiler=$(cmake -LA -N build/dev-clang-ninja | sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p')
"$compiler" --version | head -n 1
set -o pipefail
cmake --build build/dev-clang-ninja --target ExtrinsicBackendsVulkan -j2 2>&1 | tee /tmp/intrinsic-vulkan-backend-build.log | tail -n 120

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Optional when hardware/driver support is available:
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan mandatory for normal CI or CPU correctness gates.
