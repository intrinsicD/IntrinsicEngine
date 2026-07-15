---
id: BUG-086
theme: B
depends_on: []
maturity_target: Operational
completed_on: 2026-07-15
---
# BUG-086 — ImGui adapter omits the vertex-offset renderer capability

## Status
- Completed on 2026-07-15 at `Operational` maturity on the promoted Vulkan
  path and `CPUContracted` on CPU/null builds.
- Commit: shared repair `1766253c` advertises
  `ImGuiBackendFlags_RendererHasVtxOffset` and preserves non-zero command
  vertex offsets through the runtime adapter, graphics upload, and ImGui pass.
- `BUG-085` and `BUG-086` were deliberately batched because both repairs
  traverse the same adapter/overlay/pass chain and were closed by the same
  validation-enabled live Vulkan replay.
- Session-local evidence `/tmp/ui036-live/dolphin-dense-selected.png` shows
  the production panel with the dense selected dolphin mesh after crossing
  the former 65,535-vertex draw-list threshold without the ImGui assertion.

## Goal
- Advertise `ImGuiBackendFlags_RendererHasVtxOffset` so Dear ImGui may split
  large draw lists into commands with base vertex offsets instead of asserting
  at the 16-bit index limit.

## Non-goals
- No promise that the CPU `ImDrawList` UV view is the scalable dense-mesh
  backend; optional GPU-scale rendering remains `GRAPHICS-122`.
- No change to index width, geometry decimation, panel layout, or
  parameterization algorithms.

## Context
- At discovery, `Runtime.ImGuiAdapter` already copied `ImDrawCmd::VtxOffset`
  into `ImGuiOverlayDrawCommand::VertexOffset`.
- `ImGuiUploadHelper` already preserved that field, and `Pass.ImGui` added it
  to the draw-list base vertex when building push constants. The missing
  backend flag prevented Dear ImGui from using the implemented path.
- Owner/layers: runtime capability declaration plus existing graphics contract
  evidence; no new dependency edge or backend-specific API is required.

## Required changes
- [x] Set `ImGuiBackendFlags_RendererHasVtxOffset` during adapter IO
  configuration while retaining existing platform flags.
- [x] Preserve the existing per-command vertex-offset upload/draw behavior.

## Tests
- [x] Assert the runtime adapter advertises the capability after initialize.
- [x] Retain the graphics contract proving a non-zero command vertex offset is
  preserved into the ImGui pass push constants.
- [x] Replay the production Vulkan Sandbox with a selected UV mesh whose panel
  draw list exceeds 65,535 vertices and verify it exits without the ImGui
  assertion.

## Docs
- [x] Document the advertised vertex-offset capability in runtime/renderer
  ImGui notes and record verified closure in the bug index/retirement log.

## Acceptance criteria
- [x] Large ImGui draw lists use command vertex offsets instead of aborting.
- [x] The promoted Vulkan ImGui pass records the dense selected-mesh panel with
  validation enabled; `GRAPHICS-122` remains the performance/scalability
  follow-up.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractCpuTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapter.*'
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='ImGuiPassContract.UploadHelperPreservesPerCommandTextureBindlessIndices'
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
```

Completed evidence on 2026-07-15:

- Focused `Parameterization|SandboxEditor|MethodPanel|ImGui` selection:
  255/255 passed, including the large-draw-list adapter regression.
- Default CPU-supported gate: 3,753/3,753 passed.
- Vulkan ImGui GPU smoke: 3/3 passed.
- Default-config runtime Vulkan acceptance: 1/1 passed.
- Validation-enabled production Vulkan replay selected the dense dolphin mesh
  and remained operational above the former 16-bit draw-list threshold;
  session-local screenshot: `/tmp/ui036-live/dolphin-dense-selected.png`.

## Maturity
- Achieved: `Operational` on Vulkan-capable hosts and `CPUContracted` on
  CPU/null builds.
- The production Vulkan replay crossed the former 16-bit draw-list threshold;
  no GPU-scale frame-time claim is part of this task.

## Forbidden changes
- Replacing the CPU panel with the `GRAPHICS-122` offscreen renderer here.
- Widening Dear ImGui's configured index type as a substitute for the existing
  command vertex-offset contract.
- Adding panel-local mesh truncation without explicit user diagnostics.
