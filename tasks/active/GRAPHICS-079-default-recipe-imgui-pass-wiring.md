# GRAPHICS-079 — Default-recipe `Pass.ImGui` wiring (font atlas + per-cmd bindless + transient upload)

## Goal
- Wire the existing `Pass.ImGui` and `ImGuiOverlaySystem` into the renderer executor under the default recipe per `GRAPHICS-013C`/`013CQ`: graphics-owned retained font atlas allocated at `ImGuiOverlaySystem::Initialize()`, per-frame transient host-visible vertex/index buffer upload helper (mirroring `GRAPHICS-077`), one backend-local pipeline created at startup, executor route consumes `ImGuiOverlayFrame` records submitted by the runtime adapter (`RUNTIME-090`).

## Non-goals
- No runtime-side Dear ImGui adapter that walks `ImDrawData` (that is `RUNTIME-090`; this task only consumes the `ImGuiOverlayFrame` records).
- No editor panels content; this is pure overlay plumbing.
- No backend-native swapchain blit/copy fast path (rejected per `GRAPHICS-013CQ`).
- No mutation of `Pass.Present`'s contract.

## Context
- Status: in-progress (Slice B landed; Slice C next). Owner/branch: `codex/main`.
- Owner/layer: `graphics/renderer` for executor route + `ImGuiOverlaySystem::Initialize` allocation; `graphics/vulkan` for the per-frame transient upload helper + pipeline. Runtime composition (`Engine` producer↔consumer handoff) is owned by `runtime`.
- Planning anchors: `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
- Today: `Pass.ImGui.cpp` exists as a shell; `ImGuiOverlaySystem` accepts overlay records but no GPU resources are allocated; the executor lambda has no branch.
- Reconciliation with current code (verified 2026-06-02): the canonical recipe pass name is `"ImGuiPass"` (not `"Pass.ImGui"`); `features.EnableImGui` defaults `true`, so the pass is declared every default-recipe frame. Slice A wired the renderer-side `ImGuiPass` executor route, `IRenderer::SetImGuiOverlaySystem`, `IRenderer::HasImGuiOverlaySystem`, and `m_ImGuiPipelineLease`; Slice B wires `Engine::Initialize()` to hand its `Graphics::ImGuiOverlaySystem` value to the renderer and `Engine::Shutdown()` to detach it before overlay teardown. The existing `Test.ImGuiPresentContract.cpp` already covers the recipe-declaration shape and the "render-graph rejects a non-present `Backbuffer` write" negative case (Tests bullet 4).
- Per `GRAPHICS-013CQ`: the font atlas is graphics-owned retained (`R8_UNORM` default or `R8G8B8A8_UNORM` for colored atlases); user textures via existing `RHI::Bindless`; one backend pipeline (premultiplied-alpha blend, no depth test, scissor enabled, viewport from `DisplayWidth`/`DisplayHeight`, vertex stride `sizeof(ImDrawVert)`); pass writes `FrameRecipe.PresentSource` (NOT the imported backbuffer).

## Required changes
- [ ] In `ImGuiOverlaySystem::Initialize(device, textureManager)`: allocate the font atlas texture (`R8_UNORM` 4K×4K default; size negotiated via existing `ImGuiOverlayFrame::FontAtlas` metadata at first submission); free at `Shutdown()`. DPI/font rebuilds re-run `Shutdown()`/`Initialize()`.
- [ ] Add an `ImGuiTransientUploadHelper` (backend-local under `src/graphics/vulkan`) that allocates per-frame transient vertex/index buffers from submitted `ImGuiOverlayFrame::DrawLists[i]` entries.
- [x] Add `m_ImGuiPipelineLease` to `NullRenderer`. Create the pipeline at renderer init (premultiplied-alpha blend, no depth test, dynamic viewport). _(Slice A; `BuildImGuiPipelineDesc`. The `ImDrawVert` vertex stride + dynamic scissor are bound when the transient vertex/index buffers land in Slice C.)_
- [x] Add the canonical-named `"ImGuiPass"` branch (recipe name; the task prose's `"Pass.ImGui"` refers to the module) in the executor lambda routing through `RecordImGuiPass(...)`. _(Slice A; plus the `IRenderer::SetImGuiOverlaySystem` consumer handoff seam. The `Recorded` path is gated on the live `activeRenderPass.HasAttachments` signal: the SideEffect-only recipe declaration begins no render pass for ImGui, and a `BindPipeline + DrawIndexed` outside a render pass is invalid on Vulkan, so an attached overlay with work reports `SkippedUnavailable` until the recipe write topology lands. Non-operational → `SkippedNonOperational`.)_
- [x] Wire the runtime producer↔renderer consumer handoff: `Engine::Initialize()`
  hands the engine-owned `m_ImGuiOverlay` to the renderer via
  `SetImGuiOverlaySystem`, and `Engine::Shutdown()` detaches it before adapter
  teardown. _(Slice B.)_
- [ ] Ensure `Pass.ImGui` writes `FrameRecipe.PresentSource` (not `Backbuffer`); `Pass.Present` finalizes the imported backbuffer per `GRAPHICS-076`. _(Deferred to Slice D — operational graph write topology.)_
- [ ] User textures referenced by `ImTextureID` in editor panels resolve through the existing `RHI::Bindless` heap as bindless indices in a backend-local per-cmd parameter buffer; no new graphics-visible descriptor surface.

## Tests
- [ ] `contract;graphics` test: with one submitted `ImGuiOverlayFrame` containing two draw lists, the helper allocates per-frame buffers and the pass records two `BindIndexBuffer`/`Draw` blocks; `ImGuiOverlaySystemDiagnostics::DrawCalls` increments by 2.
- [x] `contract;graphics` test: with no submitted frame (or no attached overlay system), the pass returns `SkippedUnavailable`. _(Slice A — `Test.ImGuiPass.cpp`; also pins the render-target safety invariant: an attached overlay **with** work on an operational device still reports `SkippedUnavailable` because the SideEffect-only recipe gives ImGui no render pass — both attach orders + post-`RebuildOperationalResources`, with `Present` still recording — plus `SkippedNonOperational` on a non-operational device and detach-to-`nullptr`. The `Recorded` proof is owned by Slice D, where the render-pass scope exists.)_
- [ ] `contract;graphics` test: font atlas survives `RebuildGpuResources()` byte-identical. _(Deferred to Slice C — font-atlas allocation.)_
- [x] `contract;graphics` test: a `Pass.ImGui` write to `Backbuffer` (negative case) produces a render-graph validation finding (rejected per `GRAPHICS-013CQ`). _(Already covered by `Test.ImGuiPresentContract.cpp::RenderGraphRejectsNonPresentBackbufferWrites`.)_
- [x] `contract;runtime` test: `Engine::Initialize()` attaches the shared
  overlay to the renderer, and a bounded live-window frame reaches the explicit
  `"ImGuiPass"` route on the Null device. _(Slice B —
  `Test.ImGuiAdapterEngineWiring.cpp`.)_

## Docs
- [x] Update `src/graphics/renderer/README.md` to record the `Pass.ImGui` executor route + the `SetImGuiOverlaySystem` handoff seam. _(Slice A; the graphics-owned font-atlas ownership policy is recorded when it lands in Slice C.)_
- [x] Update `src/runtime/README.md` for the Slice B engine-owned overlay
  handoff to the renderer.
- [ ] Update `src/graphics/vulkan/README.md` to add the helper + pipeline rows. _(Deferred to Slice C — backend-local transient upload helper.)_

## Acceptance criteria
- [ ] Submitted overlay frames draw into `FrameRecipe.PresentSource` deterministically.
- [ ] No graphics-side `imgui.h` import; no `ImDrawData` direct consumption.
- [ ] No regression in `Pass.Present` contract.
- [ ] **Closing-cleanup assertion for the Phase-2 wiring family.** This task is the final default-recipe pass to wire. With all of `GRAPHICS-070..079` operational, every canonical default-recipe pass name resolves to a real `Record*Pass(...)` helper in the renderer executor lambda; no canonical pass name falls through to the executor's `"everything else"` soft-skip default branch. Add a `contract;graphics` test that drives one default-recipe frame in the operational state, enumerates `RenderGraphCommandPassStats::Passes`, and asserts that none of the canonical default-recipe pass names report `SkippedNonOperational` or `SkippedUnavailable`. The soft-skip default branch is preserved (it remains a safety net for non-operational devices and for future pass names that haven't been wired yet), but no canonical default-recipe pass name should reach it.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted` for the renderer-side consumer
  route (executor branch + handoff seam + pipeline lease). `Operational` owned by `GRAPHICS-079`
  Slices C/D of this same task: font atlas, transient
  host-visible upload, per-draw-list `BindIndexBuffer`/`Draw`, `gpu;vulkan`
  smoke, and the closing-cleanup assertion.

## Slice plan
- **Slice A (this slice).** Renderer-side `ImGuiPass` executor route +
  consumer handoff seam (`Scaffolded → CPUContracted`). Adds
  `IRenderer::SetImGuiOverlaySystem(ImGuiOverlaySystem*)` (the runtime-owned
  composition handoff edge), a renderer-owned `std::optional<ImGuiPass>` over
  the handed-in overlay system, `BuildImGuiPipelineDesc(...)` (premultiplied-
  alpha blend, no depth, scissor, `PushConstantSize =
  sizeof(ImGuiOverlayPushConstants)`) + an `m_ImGuiPipelineLease` created last
  in `InitializeOperationalPassResources()` (next `FailPipelineCreateCall`
  index, fail-closed), `ImGuiPass::GetPipeline()`, the `RecordImGuiPass(cmd)`
  helper mirroring `RecordPresentPass`/`RecordDebugViewPass` taxonomy
  (non-operational → `SkippedNonOperational`; no render-pass attachment / no
  overlay system / no valid pipeline / no overlay work → `SkippedUnavailable`;
  else `Execute` + `Recorded`), and the `"ImGuiPass"` executor branch. New
  `Test.ImGuiPass.cpp` `contract;graphics` cases drive the renderer with an
  injected overlay system. **Render-target safety:** the SideEffect-only recipe
  declaration begins no render pass for ImGui, so `RecordImGuiPass` gates the
  `Recorded` path on the live `activeRenderPass.HasAttachments` signal — an
  attached overlay with work is deliberately kept at `SkippedUnavailable` in
  Slice A, because recording a `BindPipeline + DrawIndexed` outside a render
  pass would be invalid on Vulkan. The `Recorded` proof moves to Slice D, where
  the write topology gives the pass a real render target; no `RecordImGuiPass`
  change is needed then (the gate flips automatically). Preserves the CPU gate.
  **Defers:** the runtime `Engine` producer↔consumer
  handoff (Slice B); the transient host-visible vertex/index upload helper,
  per-draw-list `BindIndexBuffer`/`Draw` blocks, `ImGuiOverlayDiagnostics::DrawCalls`,
  and graphics-owned retained font-atlas allocation at
  `ImGuiOverlaySystem::Initialize(device, textureManager)` (Slice C); the recipe
  `Pass.ImGui` → `FrameRecipe.PresentSource` write topology, bindless user
  textures, the `gpu;vulkan` smoke, and the closing-cleanup assertion (Slice D).
- **Slice B (landed).** Runtime composition: `Engine` hands its engine-owned
  `m_ImGuiOverlay` to the renderer via `SetImGuiOverlaySystem` after the
  adapter initializes the overlay, and detaches it during shutdown before the
  overlay is torn down. `contract;runtime` coverage asserts the renderer has the
  overlay attached after `Engine::Initialize()` and, on live-window lanes, that a
  bounded frame reaches the explicit `"ImGuiPass"` route on the Null device.
  Closes the producer↔consumer seam.
- **Slice C.** Operational upload + font atlas. `ImGuiTransientUploadHelper`
  (backend-local under `src/graphics/vulkan`), per-draw-list vertex/index
  upload, `ImGuiPass::Execute` records two `BindIndexBuffer`/`Draw` blocks,
  `ImGuiOverlayDiagnostics::DrawCalls`, retained font atlas surviving
  `RebuildGpuResources()` byte-identical, bindless user textures. Adds Tests
  bullets 1–3.
- **Slice D.** Operational backend wiring + recipe write topology
  (`Pass.ImGui` writes `FrameRecipe.PresentSource`, giving the pass a real
  color attachment + render-pass scope). This is what flips
  `RecordImGuiPass`'s `hasActiveRenderPass` gate to true, so the route finally
  records on the CPU/null path and the `Recorded`-path contract assertions
  land here (plus the closing-cleanup assertion + `gpu;vulkan` smoke). Cites
  the opt-in Vulkan run.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests ExtrinsicBackendsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Importing `imgui.h` into graphics layers.
- Adding a backend-native blit/copy present fast path.
- Allowing `Pass.ImGui` to write `Backbuffer`.
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Allocate font atlas, create pipeline + helper, wire executor route + render-graph validation negative test, exercise contract tests above.
