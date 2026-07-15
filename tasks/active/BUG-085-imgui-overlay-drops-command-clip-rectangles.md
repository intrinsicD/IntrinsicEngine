---
id: BUG-085
theme: B
depends_on: []
maturity_target: Operational
---
# BUG-085 — ImGui overlay drops draw-command clip rectangles

## Status
- In progress on branch `codex/arch-006-completion`; owner: Codex.
- Discovered during the live Vulkan acceptance run for `UI-036`: after an
  LSCM apply, the UV checkers extended above their child pane and editor
  window even though the panel pushed an ImGui clip rectangle.
- Next verification: pin the missing adapter/pass dataflow with CPU contracts,
  then replay the real `UI-036` panel on promoted Vulkan.

## Goal
- Preserve Dear ImGui's per-command framebuffer clip rectangle through the
  runtime-to-graphics overlay records and apply it as a per-draw scissor in
  `Pass.ImGui`, so clipped widgets and custom draw-list content cannot escape
  their windows on the promoted renderer.

## Non-goals
- No `UI-036` panel layout or parameterization-method changes.
- No docking, multi-viewport, texture-binding, or font-atlas redesign.
- No Vulkan-specific ImGui backend or `Vk*` type outside `graphics/vulkan`;
  the existing RHI `ICommandContext::SetScissor(...)` seam is sufficient.

## Context
- Owner/layers: `runtime` translates `ImDrawData` into pointer-free graphics
  records; `graphics/renderer` validates, uploads, and records those records.
  The existing `runtime -> graphics` composition edge remains unchanged.
- `Runtime.ImGuiAdapter::BuildOverlayDrawCommand(...)` currently copies index,
  vertex, and texture metadata but drops `ImDrawCmd::ClipRect`.
  `ImGuiOverlayDrawCommand` and `ImGuiDrawCommandUploadResult` have no clip
  fields, and `ImGuiPass::Execute(...)` never calls `SetScissor(...)` despite
  the promoted pipeline declaring dynamic scissor state.
- Clip conversion follows Dear ImGui's backend contract: subtract
  `ImDrawData::DisplayPos`, multiply by `FramebufferScale`, clamp to the
  framebuffer extent, and reject empty or non-finite rectangles.

## Required changes
- [ ] Add a backend-neutral framebuffer scissor record to each
  `ImGuiOverlayDrawCommand` and preserve it through `ImGuiUploadHelper`.
- [ ] Convert each `ImDrawCmd::ClipRect` to a finite, framebuffer-relative,
  extent-clamped scissor in `Runtime.ImGuiAdapter`.
- [ ] Apply the scissor before every ImGui draw command, skip empty scissors,
  and use the submitted display extent for the legacy no-command fallback.
- [ ] Keep draw/index/texture validation fail-closed and reset no unrelated
  overlay or renderer behavior.

## Tests
- [ ] Extend the runtime ImGui-adapter contract with non-zero display offset,
  framebuffer scale, clamping, and empty-clip cases.
- [ ] Extend graphics upload/pass contracts to prove clip metadata survives
  and exact `SetScissor(...)` calls precede the corresponding indexed draws.
- [ ] Replay the `UI-036` window, selection, and LSCM action in the production
  promoted-Vulkan Sandbox and verify the checker/grid stay inside the UV pane.
- [ ] Run the focused runtime/graphics CPU contracts and default CPU gate.

## Docs
- [ ] Update renderer/runtime ImGui documentation to state that
  `ImGuiOverlayDrawCommand` carries framebuffer scissors and `Pass.ImGui`
  applies them per command.
- [ ] Record the discovered cause and verified closure in the bug index and
  retirement log; refresh generated task/module inventories as required.

## Acceptance criteria
- [ ] Nested ImGui child/window clip rectangles survive the pointer-free
  overlay bridge with correct HiDPI/display-offset conversion.
- [ ] Every recorded ImGui indexed draw uses its non-empty command scissor.
- [ ] The live `UI-036` checker/grid no longer renders outside the UV pane on
  a Vulkan-capable host, with validation enabled and no panel-local clipping
  workaround.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractCpuTests IntrinsicTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='ImGuiAdapter.*'
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='ImGuiPass.*'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGraphicsVulkanSmokeTests
env -u INTRINSIC_ENGINE_CONFIG DISPLAY=:1 ctest --test-dir build/ci-vulkan --output-on-failure -R '^ImGuiSurfaceGpuSmoke\.' -L gpu -L vulkan --timeout 120
```

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `CPUContracted` on
  CPU/null builds.
- Retirement requires both exact CPU command/scissor contracts and a live
  promoted-Vulkan replay of the originally visible escape.

## Forbidden changes
- Adding CPU polygon clipping to individual panels instead of fixing the
  owning ImGui command/scissor seam.
- Bypassing `RHI::ICommandContext` with Vulkan calls in the renderer.
- Mixing unrelated ImGui input, docking, or texture work into this repair.
