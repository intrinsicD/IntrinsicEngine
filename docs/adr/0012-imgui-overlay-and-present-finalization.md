# ADR 0012 — ImGui Overlay Submission and `Pass.Present` Finalization

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (`Extrinsic.Graphics.ImGuiOverlaySystem`, `Extrinsic.Graphics.Pass.Present`), Runtime composition (event pump, frame bracketing), Platform (`src/platform/`), Backend (`src/graphics/vulkan/`)
- **Related tasks:** [`tasks/done/GRAPHICS-013C`](../../tasks/archive/GRAPHICS-013C-imgui-overlay-and-present.md), [`GRAPHICS-013CQ`](../../tasks/archive/GRAPHICS-013CQ-imgui-present-backend-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.ImGuiOverlaySystem` / `Pass.Present` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0009](0009-visualization-packets-and-overlay-upload.md) and the transient-debug expansion in `GRAPHICS-007Q` / `GRAPHICS-008Q` record the sibling backend-local upload helpers whose per-frame transient buffer pattern the ImGui overlay upload reuses. [ADR-0010](0010-postprocess-chain-backend-policy.md) records the retained `AreaTex` / `SearchTex` lookup-texture ownership pattern that the retained font-atlas texture mirrors.

## Context

`GRAPHICS-013C` established the data-only graphics contracts for `ImGuiOverlaySystem` (`ImGuiOverlayFrame`, `SubmitFrame` / `ClearFrame`, `HasOverlayWork`, `BuildPushConstants` diagnostics) and for the `Pass.Present` finalization shim that imports the backbuffer and composites the frame.

`GRAPHICS-013CQ` answered four producer-side / backend-side questions that `GRAPHICS-013C` deferred:

1. Who translates `ImDrawData` into `ImGuiOverlayFrame`, and when does it call `SubmitFrame` / `ClearFrame`?
2. Who owns the overlay vertex / index buffer upload, the retained font atlas, the per-cmd user-texture descriptors, and the backend pipeline state?
3. Is `Pass.Present` finalization a fullscreen draw or a swapchain `vkCmdCopyImage` / `vkCmdBlitImage`?
4. Where do platform window state, swapchain lifecycle, frame bracketing, and present timing live?

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of `Extrinsic.Graphics.ImGuiOverlaySystem` and `Pass.Present` (overlay summary + import-backbuffer + fullscreen-triangle present finalization) and retains a single pointer line to this ADR for the translation / submission timing, upload + descriptor + pipeline policy, finalization strategy, and the platform / backend / runtime / graphics boundary table.

## Decision

### 1. `ImDrawData` → `ImGuiOverlayFrame` translation and submission timing

**Runtime / editor code** — specifically the runtime-side Dear ImGui platform / renderer adapter layer, **not** graphics — walks the `ImDrawData` produced by `ImGui::Render()` and constructs an `ImGuiOverlayFrame`:

- `Enabled` is `true` only when `ImDrawData::Valid` is true **and** `ImDrawData::DisplaySize.x/y` are both `> 0`.
- `DisplayWidth` / `DisplayHeight` are the rounded pixel extents from `ImDrawData::DisplaySize` premultiplied by `ImDrawData::FramebufferScale` so backends never reapply DPI scale.
- `DrawLists` is populated per `ImDrawList`:
  - `CommandCount = CmdBuffer.Size`.
  - `VertexCount = VtxBuffer.Size`.
  - `IndexCount = IdxBuffer.Size`.
  - `UsesUserTexture = true` whenever any `ImDrawCmd::TextureId` differs from the runtime-tracked font-atlas `ImTextureID`.

Submission timing per frame:

1. Runtime calls `ImGui::Render()`.
2. Runtime calls `ImGuiOverlaySystem::SubmitFrame(...)` **once per frame**, after `ImGui::Render()` and **before** `IRenderer::PrepareFrame()`, alongside the `IRenderer::SubmitRuntimeSnapshots()` handoff so pass execution always sees a stable overlay state.
3. The renderer invokes the matching `ImGuiOverlaySystem::ClearFrame()` at end-of-frame **after** `Pass.Present` has finalized the imported backbuffer, mirroring the runtime-snapshot drain pattern.

Graphics never imports `imgui.h`, never calls Dear ImGui platform / renderer backend functions, and never sees `ImDrawData` directly. The runtime adapter is the sole translator.

### 2. Overlay upload, font / user textures, and backend pipeline

**Vertex / index payload upload.** Mirrors the renderer-owned transient debug and visualization-overlay helper pattern:

- Per-frame **host-visible (transient)** GPU buffers owned by `Extrinsic.Graphics.ImGuiUploadHelper` under `src/graphics/renderer/`.
- Reused/grown across frames through RHI `BufferManager` leases.
- Uploaded through `RHI::IDevice::WriteBuffer`.
- Consumed through `RHI::ICommandContext::BindIndexBuffer`, BDA push constants, and `DrawIndexed`.
- Never retained on `GpuWorld`.
- Never exposed to runtime or as Vulkan-native types.

The `ImGuiOverlayFrame` published to graphics carries copied POD vertex/index payloads, not Dear ImGui types. The runtime adapter is still the only code that reads `ImDrawVert` / `ImDrawIdx`; graphics receives `ImGuiOverlayVertex` and `std::uint32_t` indices and stages them through the renderer-owned helper.

**Font atlas texture.** Graphics-owned **retained**, mirroring the SMAA `AreaTex` / `SearchTex` ownership pattern from [ADR-0010](0010-postprocess-chain-backend-policy.md) §4:

- `ImGuiOverlaySystem::InitializeGpuResources(...)` accepts RHI managers and uses the latest submitted font-atlas pixel buffer.
- Default build: `R8_UNORM`.
- Colored atlases: `R8G8B8A8_UNORM`.
- A single retained `RHI::TextureHandle` is allocated through `RHI::TextureManager`, released at `ShutdownGpuResources()` before renderer manager teardown.
- DPI / font rebuilds go through the same overlay shutdown / initialize cycle rather than introducing a new mutator.

**User textures.** Image previews referenced by `ImTextureID` in editor panels will flow through the existing `RHI::Bindless` heap as bindless texture indices once per-command metadata is added:

- Per-cmd texture index is carried in a backend-local per-cmd parameter buffer produced from the runtime-supplied draw-cmd list at upload time.
- No new graphics-visible descriptor surface is added.
- Until that path lands, `ImGuiOverlayFrame::DrawLists[i].UsesUserTexture` remains the **only** graphics-visible diagnostics flag for user-texture presence.

**Pipeline state.** `ImGuiPass` owns exactly **one** pipeline created by the backend at startup and bound through the existing `SetPipeline` / `RHI::PipelineHandle` seam. Backend Vulkan pipeline state stays backend-local under `src/graphics/vulkan` and never leaks through RHI or renderer module surfaces:

- Dynamic rendering against the present-source attachment.
- Premultiplied-alpha blend.
- No depth test.
- Scissor enabled.
- Viewport derived from `DisplayWidth` / `DisplayHeight`.
- BDA vertex fetch from `ImGuiOverlayVertex` records.

The CPU/null backend exercises the same `SetPipeline` / `HasOverlayWork` / `BuildPushConstants` seam without Vulkan-specific code so the default CPU correctness gate stays authoritative.

### 3. `Pass.Present` finalization strategy

`Pass.Present` keeps the existing **CPU-testable fullscreen-triangle** finalization contract: `Draw(3, 1, 0, 0)` after binding the present pipeline. The backend samples `FrameRecipe.PresentSource` (the post-overlay LDR color attachment) and writes the imported backbuffer through `TextureUsage::Present`.

Backend-native swapchain `vkCmdCopyImage` / `vkCmdBlitImage` paths are **rejected** as the contract finalization form because they would require graphics to assume:

- Identical source / backbuffer formats.
- A `TRANSFER_DST_OPTIMAL` swapchain layout.

Neither of which graphics can guarantee without owning swapchain state.

The fullscreen-draw form is **format-agnostic**, lets the backend apply any LDR color-space handling required by the swapchain image format (sRGB write conversion, HDR10 PQ encode) inside the backend-owned present pipeline, and matches the current `Pass.Present::Execute()` shim.

A backend **may internally** opt into a copy / blit fast-path only when it can prove identical formats and a compatible source layout after the overlay barrier. That decision remains backend-local under `src/graphics/vulkan` and never alters:

- The `Pass.Present` command contract.
- The frame-recipe `Present` declaration.
- The render-graph backbuffer-write rejection.

No retained graphics-owned present resources exist beyond the backend pipeline handle; CPU/null testing remains authoritative.

### 4. Platform / backend / runtime / graphics boundary

| Layer | Owns |
| --- | --- |
| **Platform** (`src/platform/`) | Window creation / destroy, window-event pump (resize / focus / close), DPI / display reporting back to runtime / editor. Must not import `graphics`, `ecs`, or `runtime` per `AGENTS.md` §2 layering. |
| **Backend** (`src/graphics/vulkan/`) | Surface (`VkSurfaceKHR`) creation against the platform window handle; swapchain (`VkSwapchainKHR`) creation / recreation and per-image `VkImage` / `VkImageView` / `RHI::TextureHandle` registration; acquire (`vkAcquireNextImageKHR`) timing through `IDevice::BeginFrame`; present (`vkQueuePresentKHR`) timing through `IDevice::Present`. Resize handling: records the requested extent through `IDevice::Resize`, defers zero-extent requests, recreates the swapchain on the next `BeginFrame` for nonzero extents; pending-resize and out-of-date results route through fail-closed skips per `GRAPHICS-018` ([ADR-0004](0004-vulkan-backend-bringup-and-fallback.md)). Observable via `GetVulkanFrameLifecycleDiagnosticsSnapshot()`. |
| **Runtime** (`src/runtime/`) | Composition: pumps platform events, calls `IRenderer::BeginFrame` / … / `EndFrame` bracketed around graphics frame work, calls `IDevice::Present(frame)` after `IRenderer::EndFrame()`, and forwards window-resize events to `IDevice::Resize(...)` without graphics involvement. |
| **Graphics** (`src/graphics/`) | Backbuffer-import declaration in the frame recipe, the `Pass.Present` finalization command contract, and render-graph rejection of non-present writes to the imported backbuffer. Never imports platform window / surface types, never calls swapchain acquire / present directly, and never owns swapchain image lifecycle. |

This boundary is consistent with the existing `IRenderer::EndFrame()` → `IDevice::Present()` runtime composition pattern and with the `GRAPHICS-018` Vulkan integration scope captured in [ADR-0004](0004-vulkan-backend-bringup-and-fallback.md).

## Consequences

Positive:

- Graphics never imports `imgui.h` and never owns ImGui translation, so the editor adapter can iterate freely without graphics churn.
- The overlay upload reuses the same transient-buffer ring pattern as `GRAPHICS-007Q` / `GRAPHICS-008Q` and the visualization overlay upload from [ADR-0009](0009-visualization-packets-and-overlay-upload.md), so backend reviewers have one shape to validate.
- The retained font-atlas texture lives behind the same `RHI::TextureHandle` seam as SMAA lookup textures from [ADR-0010](0010-postprocess-chain-backend-policy.md), reducing the count of retained-state owners reviewers must remember.
- `Pass.Present` finalization stays fullscreen-draw and therefore format-agnostic; backends can apply swapchain-specific color-space encoding inside the backend pipeline without leaking swapchain state through RHI / renderer.
- The platform / backend / runtime / graphics boundary is explicit and tabular; reviewers can reject any new code that violates it by checking which row owns the touched API.

Trade-offs and risks:

- The overlay vertex / index byte spans are handed to the backend through a side-channel alongside `SubmitFrame` (not through the CPU-only `ImGuiOverlayFrame` summary). Reviewers must check that no future code starts publishing the bytes through `ImGuiOverlayFrame` itself, which would force graphics to grow a CPU-side ImDraw mirror.
- DPI / font rebuilds re-run `Shutdown()` / `Initialize()`. A runtime that resizes fonts frequently will tear down and recreate the retained atlas — this is intentional (no new mutator) but expensive. A future task may introduce an explicit `ReplaceFontAtlas(...)` mutator if profiling shows it is worth the surface growth.
- The fullscreen-draw present locks out a potentially faster swapchain copy / blit. The ADR allows backends to opt into copy / blit internally when they can prove identical formats; reviewers must check that such opt-ins do not alter the command contract or the frame-recipe `Present` declaration.
- The platform / backend / runtime / graphics row split is strict. A "helpful" runtime call into `vkQueuePresentKHR` directly, or a graphics call into the platform window event pump, violates the boundary even if it produces correct output today.

Follow-up tasks required: none from this ADR. The opt-in copy / blit fast-path, any future `ReplaceFontAtlas(...)` mutator, and any extension to per-cmd parameter buffers land under their own task IDs.

## Alternatives Considered

- **Graphics-side `ImDrawData` translation.** Rejected per §1: would require graphics to import `imgui.h` and the Dear ImGui platform / renderer backends, coupling graphics to ImGui iteration.
- **Retained overlay vertex / index buffers on `GpuWorld`.** Rejected per §2: overlay traffic is transient and would poison `GpuWorld` retained-resource lifetime; the transient-ring pattern is the established seam.
- **New graphics-visible user-texture descriptor surface.** Rejected per §2: `UsesUserTexture` plus the existing `RHI::Bindless` heap covers the need; a new surface would duplicate descriptor state without a clear owner.
- **`vkCmdCopyImage` / `vkCmdBlitImage` as the `Pass.Present` contract form.** Rejected per §3: graphics cannot guarantee identical source / backbuffer formats or a `TRANSFER_DST_OPTIMAL` swapchain layout without owning swapchain state.
- **Graphics owns swapchain acquire / present timing.** Rejected per §4: violates the platform / backend / runtime / graphics boundary and would couple graphics to swapchain state it cannot validate without backend code paths.
- **Single combined `ImGuiOverlayFrame` field that carries both diagnostics summary and raw `ImDrawVert` bytes.** Rejected per §2: would force graphics to grow a CPU-side ImDraw mirror; the side-channel keeps the public summary CPU-only and small.

## Validation

- [`tasks/done/GRAPHICS-013C`](../../tasks/archive/GRAPHICS-013C-imgui-overlay-and-present.md) records the underlying `ImGuiOverlaySystem` and `Pass.Present` data-only contracts.
- [`tasks/done/GRAPHICS-013CQ`](../../tasks/archive/GRAPHICS-013CQ-imgui-present-backend-clarifications.md) records the four clarification decisions captured in §§1–4.
- `docs/architecture/rendering-three-pass.md` carries the matching ImGui overlay and present / finalization contract block authored by `GRAPHICS-013CQ`.
- `src/graphics/renderer/README.md` carries the matching ownership-contract bullet next to the existing `ImGuiOverlaySystem` line.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `SetPipeline` / `HasOverlayWork` / `BuildPushConstants` for the overlay seam and the fullscreen-triangle present finalization without a Vulkan device.
