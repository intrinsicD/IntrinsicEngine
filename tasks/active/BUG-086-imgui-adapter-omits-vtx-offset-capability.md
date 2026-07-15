---
id: BUG-086
theme: B
depends_on: []
maturity_target: Operational
---
# BUG-086 — ImGui adapter omits the vertex-offset renderer capability

## Status
- In progress on branch `codex/arch-006-completion`; owner: Codex.
- Discovered during the extended live Vulkan replay for `UI-036`: a dropped,
  selected dolphin mesh made the UV draw list exceed 65,535 vertices and Dear
  ImGui aborted in `AddDrawListToDrawDataEx(...)`.
- Next verification: enable the capability already implemented by the overlay
  command/pass path, then replay the dense selected-mesh panel.

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
- `Runtime.ImGuiAdapter` already copies `ImDrawCmd::VtxOffset` into
  `ImGuiOverlayDrawCommand::VertexOffset`.
- `ImGuiUploadHelper` preserves that field, and `Pass.ImGui` adds it to the
  draw-list base vertex when building push constants. The missing backend flag
  prevents Dear ImGui from using the implemented path.
- Owner/layers: runtime capability declaration plus existing graphics contract
  evidence; no new dependency edge or backend-specific API is required.

## Required changes
- [ ] Set `ImGuiBackendFlags_RendererHasVtxOffset` during adapter IO
  configuration while retaining existing platform flags.
- [ ] Preserve the existing per-command vertex-offset upload/draw behavior.

## Tests
- [ ] Assert the runtime adapter advertises the capability after initialize.
- [ ] Retain the graphics contract proving a non-zero command vertex offset is
  preserved into the ImGui pass push constants.
- [ ] Replay the production Vulkan Sandbox with a selected UV mesh whose panel
  draw list exceeds 65,535 vertices and verify it exits without the ImGui
  assertion.

## Docs
- [ ] Document the advertised vertex-offset capability in runtime/renderer
  ImGui notes and record verified closure in the bug index/retirement log.

## Acceptance criteria
- [ ] Large ImGui draw lists use command vertex offsets instead of aborting.
- [ ] The promoted Vulkan ImGui pass records the dense selected-mesh panel with
  validation enabled; `GRAPHICS-122` remains the performance/scalability
  follow-up.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractCpuTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapter.*'
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='ImGuiPassContract.UploadHelperPreservesPerCommandTextureBindlessIndices'
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
```

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `CPUContracted` on
  CPU/null builds.
- Retirement requires a production Vulkan replay above the former 16-bit
  draw-list threshold; no GPU-scale frame-time claim is part of this task.

## Forbidden changes
- Replacing the CPU panel with the `GRAPHICS-122` offscreen renderer here.
- Widening Dear ImGui's configured index type as a substitute for the existing
  command vertex-offset contract.
- Adding panel-local mesh truncation without explicit user diagnostics.
