# GRAPHICS-013CQ — ImGui/present backend clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-013BQ` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/setup-agentic-workflow-tayDz`.
- Implementation commit: `2d2370f` (resolve decisions and sync rendering-three-pass / graphics / renderer-README docs).
- Task-state commit: pending retirement commit (this commit moves the file from `tasks/active/` to `tasks/done/`).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/rendering-three-pass.md` (ImGui overlay and present/finalization contract block), `docs/architecture/graphics.md` (`ImGuiOverlaySystem`/`PresentPass` ownership bullet under the GPU scene ownership block), and `src/graphics/renderer/README.md` (matching ownership-contract bullet next to the existing `ImGuiOverlaySystem` line). The rendering backlog README entry for `GRAPHICS-013CQ` is redirected to the `tasks/done/` location by this retirement commit. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (187 relative links, no broken links).

## Decisions
- **`ImDrawData` -> `ImGuiOverlayFrame` translation and submission timing.**
  Runtime/editor code (specifically the runtime-side Dear ImGui
  platform/renderer adapter layer, **not** graphics) walks the
  `ImDrawData` produced by `ImGui::Render()` and constructs an
  `ImGuiOverlayFrame`: `Enabled` is true only when `ImDrawData::Valid`
  is true and `ImDrawData::DisplaySize.x/y` are both `> 0`;
  `DisplayWidth`/`DisplayHeight` are the rounded pixel extents from
  `ImDrawData::DisplaySize` (premultiplied by `ImDrawData::FramebufferScale`
  so backends never reapply DPI scale); `DrawLists` is populated per
  `ImDrawList` with `CommandCount = CmdBuffer.Size`,
  `VertexCount = VtxBuffer.Size`, `IndexCount = IdxBuffer.Size`, and
  `UsesUserTexture = true` whenever any `ImDrawCmd::TextureId` differs
  from the runtime-tracked font-atlas `ImTextureID`. Submission timing:
  runtime calls `ImGuiOverlaySystem::SubmitFrame(...)` once per frame
  after `ImGui::Render()` and **before** `IRenderer::PrepareFrame()`,
  alongside the `IRenderer::SubmitRuntimeSnapshots()` handoff so that
  pass execution always sees a stable overlay state. The matching
  `ImGuiOverlaySystem::ClearFrame()` is invoked by the renderer at
  end-of-frame after `Pass.Present` has finalized the imported
  backbuffer, mirroring the runtime-snapshot drain pattern. Graphics
  never imports `imgui.h`, never calls Dear ImGui platform/renderer
  backend functions, and never sees `ImDrawData` directly; the runtime
  adapter is the sole translator.
- **Overlay vertex/index buffer upload ownership, font/user texture
  descriptor policy, and backend pipeline state.** Vertex/index payload
  upload mirrors the transient debug expansion pattern from
  `GRAPHICS-007Q`/`GRAPHICS-008Q`: per-frame host-visible (transient)
  GPU buffers owned by a backend-local upload helper under
  `src/graphics/vulkan/` and recycled each frame, never retained on
  `GpuWorld` and never exposed through `RHI` or renderer module
  surfaces. The `ImGuiOverlayFrame` published to graphics remains a
  CPU-only diagnostics summary; the actual `ImDrawVert`/`ImDrawIdx`
  byte spans are handed to the backend through the same pre-record
  composition seam (alongside `SubmitFrame`) and the backend stages
  them into the transient ring. Font texture is **graphics-owned
  retained**, mirroring the SMAA `AreaTex`/`SearchTex` ownership from
  `GRAPHICS-013AQ`: `ImGuiOverlaySystem::Initialize()` accepts the
  runtime-supplied font atlas pixel buffer (`R8_UNORM` for the default
  build, `R8G8B8A8_UNORM` when the runtime configures a colored atlas)
  and allocates a single retained `RHI::TextureHandle` through
  `RHI::TextureManager`, released at `Shutdown()`. DPI/font rebuilds
  go through the same `Shutdown()`/`Initialize()` cycle rather than
  introducing a new mutator. User textures (for example image previews
  referenced by `ImTextureID` in editor panels) flow through the
  existing `RHI::Bindless` heap as bindless texture indices; the per-cmd
  texture index is carried in a backend-local per-cmd parameter buffer
  produced from the runtime-supplied draw-cmd list at upload time, and
  no new graphics-visible descriptor surface is added.
  `ImGuiOverlayFrame::DrawLists[i].UsesUserTexture` remains the only
  graphics-visible diagnostics flag for user-texture presence. Pipeline
  state: `ImGuiPass` owns exactly one pipeline created by the backend
  at startup and bound through the existing `SetPipeline` /
  `RHI::PipelineHandle` seam. Backend Vulkan pipeline state (dynamic
  rendering against the present-source attachment, premultiplied-alpha
  blend, no depth test, scissor enabled, viewport derived from
  `DisplayWidth`/`DisplayHeight`, vertex stride `sizeof(ImDrawVert)`)
  remains backend-local under `src/graphics/vulkan` and never leaks
  through RHI or renderer module surfaces. The CPU/null backend
  exercises the same `SetPipeline`/`HasOverlayWork`/`BuildPushConstants`
  seam without Vulkan-specific code so the default CPU correctness
  gate stays authoritative.
- **Present/finalization strategy.** `Pass.Present` keeps the existing
  CPU-testable fullscreen-triangle finalization contract
  (`Draw(3, 1, 0, 0)` after binding the present pipeline): the backend
  samples `FrameRecipe.PresentSource` (the post-overlay LDR color
  attachment) and writes the imported backbuffer through
  `TextureUsage::Present`. Backend-native swapchain `vkCmdCopyImage` /
  `vkCmdBlitImage` paths are **rejected** as the contract finalization
  form because they would require graphics to assume identical
  source/backbuffer formats and a `TRANSFER_DST_OPTIMAL` swapchain
  layout — neither of which graphics can guarantee without owning
  swapchain state. The fullscreen-draw form is format-agnostic, lets
  the backend apply any LDR colorspace handling required by the
  swapchain image format (sRGB write conversion or HDR10 PQ encode)
  inside the backend-owned present pipeline, and matches the current
  `Pass.Present::Execute()` shim. A backend may internally opt into a
  copy/blit fast-path only when it can prove identical formats and a
  compatible source layout after the overlay barrier; that decision
  remains backend-local under `src/graphics/vulkan` and never alters
  the `Pass.Present` command contract, the frame-recipe `Present`
  declaration, or the render-graph backbuffer-write rejection. No
  retained graphics-owned present resources exist beyond the backend
  pipeline handle; CPU/null testing remains authoritative.
- **Platform/backend responsibility boundaries.** Platform
  (`src/platform/`) owns window creation/destroy, window-event pump
  (resize/focus/close), and DPI/display reporting back to runtime/
  editor; it must not import `graphics`, `ecs`, or `runtime` per the
  `AGENTS.md` layering rules. Backend (under `src/graphics/vulkan/`)
  owns surface (`VkSurfaceKHR`) creation against the platform window
  handle, swapchain (`VkSwapchainKHR`) creation/recreation and per-
  image `VkImage`/`VkImageView`/`RHI::TextureHandle` registration,
  acquire (`vkAcquireNextImageKHR`) timing through `IDevice::BeginFrame`,
  and present (`vkQueuePresentKHR`) timing through `IDevice::Present`
  — both already exposed through `IDevice` and observable via
  `GetVulkanFrameLifecycleDiagnosticsSnapshot()`. Resize handling: the
  backend records the requested extent through the existing
  `IDevice::Resize` seam, defers zero-extent requests, and recreates
  the swapchain on the next `BeginFrame` for nonzero extents; pending-
  resize and out-of-date results route back through fail-closed skips
  per `GRAPHICS-018`. Runtime (`src/runtime/`) owns composition: it
  pumps platform events, calls `IRenderer::BeginFrame`/.../`EndFrame`
  bracketed around graphics frame work, calls `IDevice::Present(frame)`
  after `IRenderer::EndFrame()`, and forwards window-resize events to
  `IDevice::Resize(...)` without graphics involvement. Graphics owns
  the backbuffer-import declaration in the frame recipe, the
  `Pass.Present` finalization command contract, and render-graph
  rejection of non-present writes to the imported backbuffer; it
  never imports platform window/surface types, never calls swapchain
  acquire/present directly, and never owns swapchain image lifecycle.
  This boundary is consistent with the existing `IRenderer::EndFrame()`
  -> `IDevice::Present()` runtime composition pattern documented in
  `docs/architecture/graphics.md` and with the `GRAPHICS-018` Vulkan
  integration scope.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/rendering-three-pass.md` (ImGui overlay and
  present/finalization contract block),
  `docs/architecture/graphics.md` (`ImGuiOverlaySystem`/`PresentPass`
  ownership bullet under the GPU scene ownership block), and
  `src/graphics/renderer/README.md` (matching ownership-contract
  bullet next to the existing `ImGuiOverlaySystem` line). The rendering
  backlog README entry for `GRAPHICS-013CQ` is redirected to the
  `tasks/done/` location by the retirement commit.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify concrete backend/runtime details that remain after the CPU/null `GRAPHICS-013C` ImGui overlay and present/finalization contracts.

## Non-goals
- No C++ behavior changes.
- No postprocess or debug-view behavior work.
- No platform/window ownership migration into graphics.

## Context
- `GRAPHICS-013C` established `ImGuiOverlaySystem` draw-data summary import, overlay diagnostics, guarded `ImGuiPass` command recording, `PresentPass` finalization command recording, and render-graph rejection of non-present writes to imported backbuffers.
- Remaining questions affect concrete Dear ImGui backend translation, descriptor buffers/textures, swapchain finalization implementation, and runtime/platform wiring.

## Required changes
- [x] Clarify how runtime/editor translates `ImDrawData` into `ImGuiOverlayFrame` records and when those records are submitted.
- [x] Clarify overlay vertex/index buffer upload ownership, font/user texture descriptor policy, and backend pipeline state.
- [x] Clarify whether present/finalization uses fullscreen draw, texture copy, or backend-native swapchain resolve once concrete backends are wired.
- [x] Clarify platform/backend responsibility boundaries for acquire/present timing, swapchain image ownership, and resize handling.

## Tests
- [x] Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- [x] Update renderer/backend docs and `docs/architecture/rendering-three-pass.md` with selected backend/runtime handoff policies.

## Acceptance criteria
- [x] Concrete backend/runtime ImGui and present work can proceed without changing the CPU/null graphics contracts from `GRAPHICS-013C`.
- [x] Graphics remains decoupled from platform/window ownership.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Introducing platform/window ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

