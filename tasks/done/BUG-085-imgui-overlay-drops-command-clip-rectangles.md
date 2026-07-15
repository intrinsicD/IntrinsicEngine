---
id: BUG-085
theme: B
depends_on: []
maturity_target: Operational
completed_on: 2026-07-15
---
# BUG-085 — ImGui overlay drops draw-command clip rectangles

## Status
- Completed on 2026-07-15 at `Operational` maturity on the promoted Vulkan
  path and `CPUContracted` on CPU/null builds.
- Commit: shared repair `1766253c` preserves command scissors through the
  runtime adapter, graphics overlay/upload records, and ImGui pass, including
  the native Y mirroring required by the renderer's negative-height Vulkan
  viewport convention.
- `BUG-085` and `BUG-086` were deliberately batched because both repairs
  traverse the same adapter/overlay/pass chain and were closed by the same
  validation-enabled live Vulkan replay.
- Session-local evidence `/tmp/ui036-live/parameterize-ran-fixed4.png` shows
  the selected-mesh LSCM result with the checker/grid contained by its UV child
  pane and no panel-local clipping workaround.

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
- At discovery, `Runtime.ImGuiAdapter::BuildOverlayDrawCommand(...)` copied
  index, vertex, and texture metadata but dropped `ImDrawCmd::ClipRect`.
  `ImGuiOverlayDrawCommand` and `ImGuiDrawCommandUploadResult` had no clip
  fields, and `ImGuiPass::Execute(...)` did not call `SetScissor(...)` despite
  the promoted pipeline declaring dynamic scissor state.
- Clip conversion follows Dear ImGui's backend contract: subtract
  `ImDrawData::DisplayPos`, multiply by `FramebufferScale`, clamp to the
  framebuffer extent, and reject empty or non-finite rectangles.

## Required changes
- [x] Add a backend-neutral framebuffer scissor record to each
  `ImGuiOverlayDrawCommand` and preserve it through `ImGuiUploadHelper`.
- [x] Convert each `ImDrawCmd::ClipRect` to a finite, framebuffer-relative,
  extent-clamped scissor in `Runtime.ImGuiAdapter`.
- [x] Apply the scissor before every ImGui draw command, skip empty scissors,
  and use the submitted display extent for the legacy no-command fallback.
- [x] Keep draw/index/texture validation fail-closed and reset no unrelated
  overlay or renderer behavior.

## Tests
- [x] Extend the runtime ImGui-adapter contract with non-zero display offset,
  framebuffer scale, clamping, and empty-clip cases.
- [x] Extend graphics upload/pass contracts to prove clip metadata survives
  and exact `SetScissor(...)` calls precede the corresponding indexed draws.
- [x] Replay the `UI-036` window, selection, and LSCM action in the production
  promoted-Vulkan Sandbox and verify the checker/grid stay inside the UV pane.
- [x] Run the focused runtime/graphics CPU contracts and default CPU gate.

## Docs
- [x] Update renderer/runtime ImGui documentation to state that
  `ImGuiOverlayDrawCommand` carries framebuffer scissors and `Pass.ImGui`
  applies them per command.
- [x] Record the discovered cause and verified closure in the bug index and
  retirement log; refresh generated task/module inventories as required.

## Acceptance criteria
- [x] Nested ImGui child/window clip rectangles survive the pointer-free
  overlay bridge with correct HiDPI/display-offset conversion.
- [x] Every recorded ImGui indexed draw uses its non-empty command scissor.
- [x] The live `UI-036` checker/grid no longer renders outside the UV pane on
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

Completed evidence on 2026-07-15:

- Focused `Parameterization|SandboxEditor|MethodPanel|ImGui` selection:
  255/255 passed.
- Default CPU-supported gate: 3,753/3,753 passed.
- Vulkan ImGui GPU smoke: 3/3 passed.
- Default-config runtime Vulkan acceptance: 1/1 passed.
- Validation-enabled production Vulkan replay completed with the UV
  checker/grid contained in its child pane; session-local screenshot:
  `/tmp/ui036-live/parameterize-ran-fixed4.png`.

## Maturity
- Achieved: `Operational` on Vulkan-capable hosts and `CPUContracted` on
  CPU/null builds.
- Exact CPU command/scissor contracts and the live promoted-Vulkan replay of
  the originally visible escape both passed.

## Forbidden changes
- Adding CPU polygon clipping to individual panels instead of fixing the
  owning ImGui command/scissor seam.
- Bypassing `RHI::ICommandContext` with Vulkan calls in the renderer.
- Mixing unrelated ImGui input, docking, or texture work into this repair.
