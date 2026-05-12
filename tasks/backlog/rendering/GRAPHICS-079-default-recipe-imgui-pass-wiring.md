# GRAPHICS-079 — Default-recipe `Pass.ImGui` wiring (font atlas + per-cmd bindless + transient upload)

## Goal
- Wire the existing `Pass.ImGui` and `ImGuiOverlaySystem` into the renderer executor under the default recipe per `GRAPHICS-013C`/`013CQ`: graphics-owned retained font atlas allocated at `ImGuiOverlaySystem::Initialize()`, per-frame transient host-visible vertex/index buffer upload helper (mirroring `GRAPHICS-077`), one backend-local pipeline created at startup, executor route consumes `ImGuiOverlayFrame` records submitted by the runtime adapter (`RUNTIME-090`).

## Non-goals
- No runtime-side Dear ImGui adapter that walks `ImDrawData` (that is `RUNTIME-090`; this task only consumes the `ImGuiOverlayFrame` records).
- No editor panels content; this is pure overlay plumbing.
- No backend-native swapchain blit/copy fast path (rejected per `GRAPHICS-013CQ`).
- No mutation of `Pass.Present`'s contract.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer` for executor route + `ImGuiOverlaySystem::Initialize` allocation; `graphics/vulkan` for the per-frame transient upload helper + pipeline.
- Planning anchors: `tasks/done/GRAPHICS-013C-imgui-overlay-and-present.md`, `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md`.
- Today: `Pass.ImGui.cpp` exists as a shell; `ImGuiOverlaySystem` accepts overlay records but no GPU resources are allocated; the executor lambda has no branch.
- Per `GRAPHICS-013CQ`: the font atlas is graphics-owned retained (`R8_UNORM` default or `R8G8B8A8_UNORM` for colored atlases); user textures via existing `RHI::Bindless`; one backend pipeline (premultiplied-alpha blend, no depth test, scissor enabled, viewport from `DisplayWidth`/`DisplayHeight`, vertex stride `sizeof(ImDrawVert)`); pass writes `FrameRecipe.PresentSource` (NOT the imported backbuffer).

## Required changes
- [ ] In `ImGuiOverlaySystem::Initialize(device, textureManager)`: allocate the font atlas texture (`R8_UNORM` 4K×4K default; size negotiated via existing `ImGuiOverlayFrame::FontAtlas` metadata at first submission); free at `Shutdown()`. DPI/font rebuilds re-run `Shutdown()`/`Initialize()`.
- [ ] Add an `ImGuiTransientUploadHelper` (backend-local under `src/graphics/vulkan`) that allocates per-frame transient vertex/index buffers from submitted `ImGuiOverlayFrame::DrawLists[i]` entries.
- [ ] Add `m_ImGuiPipelineLease` to `NullRenderer`. Create the pipeline at renderer init (vertex stride `sizeof(ImDrawVert)`, premultiplied-alpha blend, no depth test, scissor enabled, dynamic viewport).
- [ ] Add `"Pass.ImGui"` branch in the executor lambda routing through `RecordImGuiPass(...)` consuming the latest `ImGuiOverlayFrame` (or `SkippedUnavailable` when no frame was submitted that frame).
- [ ] Ensure `Pass.ImGui` writes `FrameRecipe.PresentSource` (not `Backbuffer`); `Pass.Present` finalizes the imported backbuffer per `GRAPHICS-076`.
- [ ] User textures referenced by `ImTextureID` in editor panels resolve through the existing `RHI::Bindless` heap as bindless indices in a backend-local per-cmd parameter buffer; no new graphics-visible descriptor surface.

## Tests
- [ ] `contract;graphics` test: with one submitted `ImGuiOverlayFrame` containing two draw lists, the helper allocates per-frame buffers and the pass records two `BindIndexBuffer`/`Draw` blocks; `ImGuiOverlaySystemDiagnostics::DrawCalls` increments by 2.
- [ ] `contract;graphics` test: with no submitted frame, the pass returns `SkippedUnavailable`.
- [ ] `contract;graphics` test: font atlas survives `RebuildGpuResources()` byte-identical.
- [ ] `contract;graphics` test: a `Pass.ImGui` write to `Backbuffer` (negative case) produces a render-graph validation finding (rejected per `GRAPHICS-013CQ`).

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record `Pass.ImGui` as operationally wired and the font-atlas ownership policy.
- [ ] Update `src/graphics/vulkan/README.md` to add the helper + pipeline rows.

## Acceptance criteria
- [ ] Submitted overlay frames draw into `FrameRecipe.PresentSource` deterministically.
- [ ] No graphics-side `imgui.h` import; no `ImDrawData` direct consumption.
- [ ] No regression in `Pass.Present` contract.
- [ ] **Closing-cleanup assertion for the Phase-2 wiring family.** This task is the final default-recipe pass to wire. With all of `GRAPHICS-070..079` operational, every canonical default-recipe pass name resolves to a real `Record*Pass(...)` helper in the renderer executor lambda; no canonical pass name falls through to the executor's `"everything else"` soft-skip default branch. Add a `contract;graphics` test that drives one default-recipe frame in the operational state, enumerates `RenderGraphCommandPassStats::Passes`, and asserts that none of the canonical default-recipe pass names report `SkippedNonOperational` or `SkippedUnavailable`. The soft-skip default branch is preserved (it remains a safety net for non-operational devices and for future pass names that haven't been wired yet), but no canonical default-recipe pass name should reach it.

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
