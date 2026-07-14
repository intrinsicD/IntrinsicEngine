# GRAPHICS-079 — Default-recipe `Pass.ImGui` wiring (font atlas + per-cmd bindless + transient upload)

## Status
- State: done.
- Owner/agent: codex.
- Branch: `main`.
- Maturity reached: `Operational` on Vulkan-capable hosts; `CPUContracted` on this host because the opt-in `gpu;vulkan` smoke is registered but skipped without an operational GLFW/Vulkan lane.
- Completion date: 2026-06-02.
- PR/commit: Slices A/B — commits `8f1374c6`, `61192d50`, and `84d16985`; Slice C — commit `97d34aba`; Slice D.1 — commit `9e283c72`; Slice D.2 — commit `69f9b16c`.
- Next verification step: none for this task. Downstream UI/editor acceptance is owned by `UI-001` and final sandbox acceptance by `RUNTIME-095`.

## Goal
- Wire the existing `Pass.ImGui` and `ImGuiOverlaySystem` into the renderer executor under the default recipe per `GRAPHICS-013C`/`013CQ`: graphics-owned retained font atlas allocated at `ImGuiOverlaySystem::Initialize()`, per-frame transient host-visible vertex/index buffer upload helper (mirroring `GRAPHICS-077`), backend-local pipeline variants created at startup for the active present-source color formats, executor route consumes `ImGuiOverlayFrame` records submitted by the runtime adapter (`RUNTIME-090`).

## Non-goals
- No runtime-side Dear ImGui adapter that walks `ImDrawData` (that is `RUNTIME-090`; this task only consumes the `ImGuiOverlayFrame` records).
- No editor panels content; this is pure overlay plumbing.
- No backend-native swapchain blit/copy fast path (rejected per `GRAPHICS-013CQ`).
- No mutation of `Pass.Present`'s contract.

## Context
- Status: done (see `## Status`). Owner/branch: `codex/main`.
- Owner/layer: `graphics/renderer` for executor route, retained `ImGuiOverlaySystem` font-atlas resources, and the renderer-owned transient upload helper; Vulkan consumes the same RHI command/buffer surface when Slice D gives the pass a render target. Runtime composition (`Engine` producer↔consumer handoff) is owned by `runtime`.
- Planning anchors: `tasks/archive/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/archive/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
- Current state: `Pass.ImGui.cpp` consumes uploaded draw-list/command metadata, `ImGuiOverlaySystem` owns retained font-atlas resources when initialized through the renderer, and the executor has an explicit `"ImGuiPass"` route. Slice D.1 promotes `"ImGuiPass"` from SideEffect-only to a load/store `FrameRecipe.PresentSource` color attachment so the route records under the CPU/null render-pass scope when an uploadable overlay payload is attached. Slice D.2 adds per-command user-texture bindless metadata and shader sampling; the opt-in `ImGuiSurfaceGpuSmoke` is registered and reports `SKIPPED` on hosts without an operational GLFW/Vulkan lane.
- Verification note (2026-06-02): Slice D.2 focused ImGui coverage passed under the CPU/null gate, the shader targets compiled, `IntrinsicTests` built, and the opt-in `ImGuiSurfaceGpuSmoke` was registered and skipped on this host. The full default CTest gate still reports three out-of-scope failures: the missing `IntrinsicBenchmarkSmoke.HalfedgeSmoke` executable pair and `RHICommandContext.SubmitBarriersFallbackRoutesTextureAndBufferBarriers` in untouched RHI barrier code. These are not addressed in this GRAPHICS-079 task.
- Reconciliation with current code (verified 2026-06-02, updated for RUNTIME-095 Vulkan/debug-view follow-up): the canonical recipe pass name is `"ImGuiPass"` (not `"Pass.ImGui"`); `features.EnableImGui` defaults `true`, so the pass is declared every default-recipe frame. Slice A wired the renderer-side `ImGuiPass` executor route, `IRenderer::SetImGuiOverlaySystem`, `IRenderer::HasImGuiOverlaySystem`, and ImGui pipeline leases for the active present-source formats; Slice B wires `Engine::Initialize()` to hand its `Graphics::ImGuiOverlaySystem` value to the renderer and `Engine::Shutdown()` to detach it before overlay teardown; Slice C adds retained font-atlas resources, renderer-owned transient vertex/index upload, and direct per-list draw recording contracts. The existing `Test.ImGuiPresentContract.cpp` already covers the recipe-declaration shape and the "render-graph rejects a non-present `Backbuffer` write" negative case (Tests bullet 4).
- Per `GRAPHICS-013CQ`: the font atlas is graphics-owned retained (`R8_UNORM` default or `R8G8B8A8_UNORM` for colored atlases); user textures via existing `RHI::Bindless`; backend pipeline variants match the active `FrameRecipe.PresentSource` format (swapchain-format `SceneColorLDR`/AA-resolved presentation or `RGBA8_UNORM` `DebugViewRGBA`) with Dear ImGui straight-alpha color blend, no depth test, scissor enabled, viewport from `DisplayWidth`/`DisplayHeight`, vertex stride `sizeof(ImDrawVert)`; pass writes `FrameRecipe.PresentSource` (NOT the imported backbuffer).

## Required changes
- [x] In `ImGuiOverlaySystem::InitializeGpuResources(device, textureManager, samplerManager)`: allocate the font atlas texture (`R8_UNORM` 4K×4K fallback; size negotiated via existing `ImGuiOverlayFrame::FontAtlas` metadata at first submission); free at `ShutdownGpuResources()`. DPI/font rebuilds re-run overlay shutdown/initialize. _(Slice C; renderer-owned retained resource, released before manager teardown.)_
- [x] Add an `ImGuiUploadHelper` (renderer-owned RHI helper matching the existing transient-debug / visualization-overlay helper pattern) that allocates growing transient vertex/index buffers from submitted `ImGuiOverlayFrame::DrawLists[i]` payload entries. _(Slice C; no Vulkan-native types leak through the helper surface.)_
- [x] Add ImGui pipeline leases to `NullRenderer`. Create the swapchain-format pipeline at renderer init and an `RGBA8_UNORM` variant when the swapchain format differs, both with Dear ImGui straight-alpha color blend, no depth test, and dynamic viewport. _(Slice A plus RUNTIME-095 Vulkan/debug-view follow-up; `BuildImGuiPipelineDesc`. The `ImDrawVert` vertex stride + dynamic scissor are bound when the transient vertex/index buffers land in Slice C.)_
- [x] Add the canonical-named `"ImGuiPass"` branch (recipe name; the task prose's `"Pass.ImGui"` refers to the module) in the executor lambda routing through `RecordImGuiPass(...)`. _(Slice A; plus the `IRenderer::SetImGuiOverlaySystem` consumer handoff seam. Slice D.1 keeps the live `activeRenderPass.HasAttachments` safety gate, but the recipe now supplies that render-pass scope by writing `FrameRecipe.PresentSource`; missing overlay/work/upload resources still report `SkippedUnavailable`, and non-operational devices report `SkippedNonOperational`.)_
- [x] Wire the runtime producer↔renderer consumer handoff: `Engine::Initialize()`
  hands the engine-owned `m_ImGuiOverlay` to the renderer via
  `SetImGuiOverlaySystem`, and `Engine::Shutdown()` detaches it before adapter
  teardown. _(Slice B.)_
- [x] Ensure `Pass.ImGui` writes `FrameRecipe.PresentSource` (not `Backbuffer`); `Pass.Present` finalizes the imported backbuffer per `GRAPHICS-076`. _(Slice D.1 — operational CPU/null graph write topology.)_
- [x] User textures referenced by `ImTextureID` in editor panels resolve through the existing `RHI::Bindless` heap as bindless indices in per-command metadata pushed to `Pass.ImGui`; no new graphics-visible descriptor surface. _(Slice D.2 — `ImGuiOverlayDrawCommand::TextureBindlessIndex`, runtime adapter `ImTextureID` downcast, `ImGuiOverlayPushConstants::TextureBindlessIndex`, and `assets/shaders/imgui.frag` sampling.)_

## Tests
- [x] `contract;graphics` test: with one submitted `ImGuiOverlayFrame` containing two draw lists, the helper allocates per-frame buffers and the pass records two `BindIndexBuffer`/`Draw` blocks; `ImGuiOverlaySystemDiagnostics::DrawCalls` increments by 2. _(Slice C — `Test.ImGuiPass.cpp::UploadHelperPacksTwoDrawListsAndPassRecordsPerList`.)_
- [x] `contract;graphics` test: with no submitted frame (or no attached overlay system), the pass returns `SkippedUnavailable`. _(Slice A/D.1 — `Test.ImGuiPass.cpp`; missing overlay, no work, detach-to-`nullptr`, and failed upload remain fail-closed, while Slice D.1 flips attached uploadable overlay frames to `Recorded` under the CPU/null render-pass scope. Non-operational devices still report `SkippedNonOperational`.)_
- [x] `contract;graphics` test: attached uploadable overlay frames record through the renderer route and no canonical default-recipe pass reports `SkippedUnavailable`/`SkippedNonOperational` in that CPU/null frame. _(Slice D.1 — `Test.ImGuiPass.cpp::AttachedOverlayWithWorkRecordsAfterInitialize`.)_
- [x] `contract;graphics` test: font atlas survives `RebuildGpuResources()` byte-identical. _(Slice C — `Test.ImGuiPass.cpp::FontAtlasUploadSurvivesOperationalRebuildByteIdentical`; renderer seam is `RebuildOperationalResources()`.)_
- [x] `contract;graphics` test: a `Pass.ImGui` write to `Backbuffer` (negative case) produces a render-graph validation finding (rejected per `GRAPHICS-013CQ`). _(Already covered by `Test.ImGuiPresentContract.cpp::RenderGraphRejectsNonPresentBackbufferWrites`.)_
- [x] `contract;runtime` test: `Engine::Initialize()` attaches the shared
  overlay to the renderer, and a bounded live-window frame reaches the explicit
  `"ImGuiPass"` route on the Null device. _(Slice B —
  `Test.ImGuiAdapterEngineWiring.cpp`.)_
- [x] `contract;graphics` + `contract;runtime` tests: per-command user-texture
  bindless indices survive adapter production, upload-helper packing, and
  `Pass.ImGui` push-constant recording. _(Slice D.2 —
  `Test.ImGuiAdapter.cpp::ImageDrawPreservesUserTextureBindlessCommand` and
  `Test.ImGuiPass.cpp::UploadHelperPreservesPerCommandTextureBindlessIndices`.)_

## Docs
- [x] Update `src/graphics/renderer/README.md` to record the `Pass.ImGui` executor route + the `SetImGuiOverlaySystem` handoff seam. _(Slice A; the graphics-owned font-atlas ownership policy is recorded when it lands in Slice C.)_
- [x] Update `src/runtime/README.md` for the Slice B engine-owned overlay
  handoff to the renderer.
- [x] Update `src/graphics/vulkan/README.md` to add the helper + pipeline rows. _(Slice C records that the helper is renderer-owned over RHI buffers; Slice D.1 records that Vulkan consumes `BindIndexBuffer`/BDA push constants under the `FrameRecipe.PresentSource` render-pass scope; Slice D.2 records per-command bindless texture sampling, with Vulkan smoke still host-dependent.)_

## Acceptance criteria
- [x] Submitted overlay frames draw into `FrameRecipe.PresentSource` deterministically. _(CPU/null recorded path in Slice D.1; per-command texture selection in Slice D.2.)_
- [x] No graphics-side `imgui.h` import; no `ImDrawData` direct consumption.
- [x] No regression in `Pass.Present` contract.
- [x] **Closing-cleanup assertion for the Phase-2 wiring family.** This task is the final default-recipe pass to wire. With all of `GRAPHICS-070..079` operational, every canonical default-recipe pass name resolves to a real `Record*Pass(...)` helper in the renderer executor lambda; no canonical pass name falls through to the executor's `"everything else"` soft-skip default branch. Add a `contract;graphics` test that drives one default-recipe frame in the operational state, enumerates `RenderGraphCommandPassStats::Passes`, and asserts that none of the canonical default-recipe pass names report `SkippedNonOperational` or `SkippedUnavailable`. The soft-skip default branch is preserved (it remains a safety net for non-operational devices and for future pass names that haven't been wired yet), but no canonical default-recipe pass name should reach it.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted` for the renderer-side consumer
  route (executor branch + handoff seam + pipeline lease). `Operational` owned by `GRAPHICS-079`
  Slices C/D of this same task: Slice C landed retained font atlas, transient
  host-visible upload, and per-draw-list `BindIndexBuffer`/`Draw` contract
  coverage; Slice D owns the render-target write topology, per-command user
  textures, `gpu;vulkan` smoke, and the closing-cleanup assertion.

## Slice plan
- **Slice A (this slice).** Renderer-side `ImGuiPass` executor route +
  consumer handoff seam (`Scaffolded → CPUContracted`). Adds
  `IRenderer::SetImGuiOverlaySystem(ImGuiOverlaySystem*)` (the runtime-owned
  composition handoff edge), a renderer-owned `std::optional<ImGuiPass>` over
  the handed-in overlay system, `BuildImGuiPipelineDesc(...)` (Dear ImGui
  straight-alpha color blend, no depth, scissor, `PushConstantSize =
  sizeof(ImGuiOverlayPushConstants)`) + ImGui pipeline leases created last in
  `InitializeOperationalPassResources()` (fail-closed),
  `ImGuiPass::GetPipeline()`, the `RecordImGuiPass(cmd)`
  helper mirroring `RecordPresentPass`/`RecordDebugViewPass` taxonomy
  (non-operational → `SkippedNonOperational`; no render-pass attachment / no
  overlay system / no valid pipeline / no overlay work → `SkippedUnavailable`;
  else `Execute` + `Recorded`), and the `"ImGuiPass"` executor branch. New
  `Test.ImGuiPass.cpp` `contract;graphics` cases drive the renderer with an
  injected overlay system. **Render-target safety:** Slice A deliberately kept
  attached overlay work at `SkippedUnavailable` because the side-effect-only
  recipe declaration began no render pass for ImGui; Slice D.1 keeps the live
  `activeRenderPass.HasAttachments` safety gate and flips it by giving
  `"ImGuiPass"` a real `FrameRecipe.PresentSource` color attachment. Preserves
  the CPU gate.
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
- **Slice C (landed).** Upload + retained font atlas. `ImGuiUploadHelper`
  is renderer-owned over RHI buffers (matching the transient-debug /
  visualization-overlay helper pattern rather than introducing Vulkan-native
  types), and packs submitted POD vertices/indices into one growing
  host-visible vertex buffer and one growing index buffer. `ImGuiPass::Execute`
  consumes `ImGuiUploadResult`, records one
  `BindIndexBuffer + PushConstants + DrawIndexed` block per uploaded draw list,
  and increments `ImGuiOverlayDiagnostics::DrawCalls`. `ImGuiOverlaySystem`
  retains the font atlas texture/sampler through `TextureManager` /
  `SamplerManager`, uploads changed atlas bytes through `ITransferQueue`, and
  releases leases before renderer manager teardown. Runtime `ImGuiAdapter`
  copies font-atlas bytes and POD vertex/index payloads into the frame record;
  graphics still never imports `imgui.h`. Adds Tests bullets 1 and 3. Defers
  per-command user-texture metadata/sampling because current frame records only
  expose the per-list `UsesUserTexture` diagnostics flag.
- **Slice D.1.** CPU/null render-target topology and recorded-path proof.
  Promote `"ImGuiPass"` from `Read(FrameRecipe.PresentSource) + SideEffect()`
  to a load/store render-pass-scope pass that reads and writes the current
  `FrameRecipe.PresentSource` texture without ever writing `Backbuffer`.
  Update the renderer route comments, flip the attached-overlay contracts from
  `SkippedUnavailable` to `Recorded`, and add the closing-cleanup assertion
  that the canonical default-recipe pass family no longer has an ImGui
  soft-skip when an overlay frame with payload is attached. Defers per-command
  user texture metadata/sampling and the opt-in Vulkan smoke to Slice D.2.
- **Slice D.2.** User-texture sampling + Vulkan smoke. With Slice D.1's
  `FrameRecipe.PresentSource` render-pass scope in place, add per-command
  user-texture bindless metadata/sampling and the opt-in `gpu;vulkan` smoke.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanContractTests
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
- None for this task. Downstream UI/editor acceptance is owned by `UI-001` and final sandbox acceptance by `RUNTIME-095`.
