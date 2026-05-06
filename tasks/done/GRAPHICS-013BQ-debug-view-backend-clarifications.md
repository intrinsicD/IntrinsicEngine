# GRAPHICS-013BQ — Debug-view backend clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-013AQ` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/setup-agentic-workflow-5MN6O`.
- Implementation commit: `53e8b35` (resolve decisions and sync rendering-three-pass / graphics / renderer-README docs).
- Task-state commit: pending retirement commit (this commit moves the file from `tasks/active/` to `tasks/done/`).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/rendering-three-pass.md` (debug-view contract block), `docs/architecture/graphics.md` (`DebugViewSystem` ownership bullet under the GPU scene ownership block), and `src/graphics/renderer/README.md` (matching ownership-contract bullet next to the existing `DebugViewSystem` line). The rendering backlog README entry for `GRAPHICS-013BQ` is redirected to the `tasks/done/` location by this retirement commit. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (187 relative links, no broken links).

## Decisions
- **Shader visualization modes.** Visualization mode is derived
  deterministically from the resolved selection's
  `FrameRecipeResourceKind` + `DebugViewResourceClass`; no
  user-selectable visualization-mode field is added to
  `DebugViewSettings`, and `DebugViewPushConstants` keeps its existing
  four-`uint32` packing (`ResourceKind`, `ResourceClass`,
  `UsedFallback`, `Reserved`). The mapping is: `SceneColorLDR`
  (`RGBA8_UNORM`) uses a direct LDR color blit; `SceneColorHDR`
  (`RGBA16_FLOAT`) uses a backend-local Reinhard tone-mapped color
  preview (no commitment to the postprocess chain); `SceneDepth` and
  other depth-class textures linearize via the existing `CameraUBO`
  near/far and emit a grayscale ramp; `SceneNormal` (`RGBA16_FLOAT`)
  uses world-space normal visualization (`* 0.5 + 0.5`);
  `EntityId`/`PrimitiveId` (`R32_UINT`) use a deterministic 32-bit
  integer hash to color, with `PrimitiveId` first decoded through the
  existing `EncodedSelectionId` helper so the high 4 bits
  (`SelectionPrimitiveDomain`) modulate hue and the low 28 bits drive
  the hash; `Albedo` (`RGBA8_UNORM`) uses direct color; `Material0`
  (`RGBA16_FLOAT`, scalar PBR channels per `surface_gbuffer.frag`:
  roughness in R, metallic in G, reserved in B/A) uses a scalar
  channel false-color visualization (roughness routed to the red
  channel, metallic routed to the green channel, blue zeroed) — it is
  **not** an integer slot-ID resource and never uses the integer-hash
  path. Adding a separate material-slot-ID render target later is a
  distinct follow-up that would introduce its own `R32_UINT` resource
  and integer-hash mapping; `ShadowAtlas` uses depth-aspect
  linearization to grayscale at a backend-fixed shadow projection.
  `DebugViewRGBA` is the `Pass.DebugView` color attachment and is
  **non-selectable** as a preview input — `DebugViewSystem::BuildInspectionTable()`
  already gates `Previewable` on
  `resource.Kind != FrameRecipeResourceKind::DebugViewRGBA`, so the
  visualization-mode mapping has no `DebugViewRGBA` entry and backends
  must not invent a `DebugViewRGBA` blit/self-preview mode that would
  bypass that aliasing gate. Future user-selectable overrides (e.g.,
  manual mode, channel masks, false-color ramps) are explicit
  follow-ups and must extend
  `DebugViewSettings`/`DebugViewPushConstants` through their own task
  with budget justification.
- **Descriptor binding ownership.** `Pass.DebugView` owns one
  pass-local descriptor set with exactly two bindings: (b0) a sampled
  image view of the resolved selection's texture/depth resource and
  (b1) a single linear-clamp sampler, mirroring the postprocess pattern
  of one push-constant block plus one pass-local descriptor set.
  Concrete `VkDescriptorSetLayout` definitions, per-aspect view
  creation (color view for `RGBA8_UNORM`/`RGBA16_FLOAT` resources,
  depth-aspect-only view for depth-class resources, integer-typed
  view for `R32_UINT` selection-ID resources `EntityId`/`PrimitiveId`),
  and sampler creation remain backend-local under `src/graphics/vulkan`
  and never leak through RHI or renderer module surfaces. `DebugViewRGBA` is the pass color attachment, owned
  by the framegraph through the frame-recipe resource map, and is not
  part of the descriptor set. Aliasing protection relies on the
  existing exclusion of `DebugViewRGBA` from the previewable inspection
  set, so no separate descriptor-side aliasing gate is added. No
  retained graphics-owned debug-view textures or buffers exist;
  backends never bypass `DebugViewSystem` to allocate debug-view state,
  and the CPU/null backend exercises the same seam without
  Vulkan-specific code so the default CPU correctness gate stays
  authoritative.
- **UI-name to `FrameRecipeIntrospection` mapping.** Runtime/editor
  code owns the dictionary that maps human-readable UI strings (for
  example "Scene Color (HDR)" or "Picking IDs") to canonical
  `FrameRecipeIntrospection::Resources[i].Name` keys (for example
  `"SceneColorHDR"`, `"EntityId"`). The runtime builds this dictionary
  from `DebugViewSystem::BuildInspectionTable(frameRecipe)` rows
  (`Name`/`Kind`/`ResourceClass`/`Enabled`/`Previewable` drive grouping,
  disabled-state UX, and previewable filtering) and writes the
  canonical name into `DebugViewSettings::RequestedResourceName` via
  `DebugViewSystem::SetSettings(...)`. Graphics never receives display
  strings, never imports ImGui or platform/window state, and never
  owns the dictionary. The default
  `RequestedResourceName = "FrameRecipe.PresentSource"` remains the
  graphics-side fallback when runtime has not yet selected anything.
  Editor persistence of the active selection across sessions is
  runtime-owned and out of scope here; the matching runtime adapter
  shape lands as part of `GRAPHICS-014Q` visualization-runtime work.
- **Buffer textual/statistical inspection.** Buffer resources (GPU
  scene tables, draw-bucket args/counts, `PostProcess.Histogram`,
  `Picking.Readback`, transient debug streams) remain listed in
  `BuildInspectionTable()` with `DebugViewResourceClass::Buffer` but
  stay deliberately non-previewable in `Pass.DebugView`. Textual and
  statistical inspection of buffers is **deferred** to a future
  runtime/editor visualization surface tracked under `GRAPHICS-014Q`,
  not added to the fullscreen preview path. That future surface
  consumes existing per-owner diagnostics rather than introducing a
  parallel buffer-readback API on `DebugViewSystem`:
  `PostProcessDiagnostics` for `PostProcess.Histogram`,
  `SelectionSystem` plus `Picking.Readback` drains for picking,
  `GpuWorld::Diagnostics` for scene-table buffers, and
  `SpatialDebugVisualizerDiagnostics` for transient debug streams.
  `DebugViewSystem` does not grow a buffer-inspection method or
  buffer-readback path under this clarification, and the CPU/null
  contract from `GRAPHICS-013B` is unchanged. If runtime work later
  surfaces a graphics-side buffer-inspection seam, it is promoted via
  a new follow-up task rather than re-opening `GRAPHICS-013B`.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/rendering-three-pass.md` (debug-view contract
  block), `docs/architecture/graphics.md` (`DebugViewSystem` ownership
  bullet under the GPU scene ownership block), and
  `src/graphics/renderer/README.md` (matching ownership-contract
  bullet next to the existing `DebugViewSystem` line). The rendering
  backlog README entry for `GRAPHICS-013BQ` is redirected to the
  `tasks/done/` location by the retirement commit.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify concrete backend and UX details that remain after the CPU/null `GRAPHICS-013B` debug-view and render-target inspection contracts.

## Non-goals
- No C++ behavior changes.
- No postprocess effect ownership.
- No ImGui overlay or present/finalization policy work.

## Context
- `GRAPHICS-013B` established `DebugViewSystem` resource inspection, deterministic resource selection/fallback diagnostics, `DebugViewPushConstants`, `Pass.DebugView` fullscreen command contracts, and frame-recipe `DebugViewRGBA` scheduling.
- Remaining questions affect shader-side visualization modes, descriptor binding, UI selection plumbing, and backend formatting and should not be mixed with CPU/null contracts.

## Required changes
- Clarify shader visualization modes for color, depth, integer ID, normal, material, and shadow atlas resources.
- Clarify descriptor binding ownership for the selected sampled resource and `DebugViewRGBA` output.
- Clarify how runtime/editor UI names map to `FrameRecipeIntrospection` resource names without adding platform/window ownership to graphics.
- Clarify whether unsupported buffer resources should later expose textual/statistical inspection outside the fullscreen preview path.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md`, renderer docs, and backend notes with selected debug-view backend policies.

## Acceptance criteria
- Backend/UI debug-view work can proceed without changing the CPU/null graphics contracts from `GRAPHICS-013B`.
- Graphics remains decoupled from platform/window ownership and ImGui policy.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Expanding into ImGui/present ownership.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

