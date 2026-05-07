# Rendering Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/rendering/`
queue. It complements the canonical inventory and points agents at the next
valid task without requiring them to read every file.

## Canonical sources

- [GRAPHICS-001 — Rendering parity inventory and task index](GRAPHICS-001-rendering-parity-inventory.md)
  is the canonical rendering backlog index. The prose ordering inside
  GRAPHICS-001 remains authoritative whenever this README and GRAPHICS-001
  disagree on intent; this README expresses the same ordering as a
  machine-readable DAG.
- `RORG-031B`, if encountered in older notes, is **historical/superseded
  planning** that has been retired to
  [`tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md`](../../done/RORG-031B-rendering-pipeline-backlog-seed.md).
  It is not the active implementation path and must not be selected as
  next-active work.

## Dependency DAG

The following list is the minimum dependency order for the rendering backlog.
Each entry lists the upstream tasks that must be complete (or explicitly
out-of-scope) before the entry is eligible for "in-progress" selection.

- [GRAPHICS-021 — Rendering backlog workflow cleanup](../../done/GRAPHICS-021-rendering-backlog-workflow-cleanup.md):
  completed cleanup precondition for further rendering task churn
  (task-format/validator/doc hygiene for the rest of the queue).
- [GRAPHICS-016 — Runtime extraction and graphics handoff](../../done/GRAPHICS-016-runtime-extraction-handoff.md):
  completed first implementation gate for runtime-owned extraction and graphics
  snapshot handoff. Downstream graphics pass work can proceed through
  GRAPHICS-002+ without introducing live ECS ownership into graphics.
- [GRAPHICS-002 — Render-world / frame-input snapshot contract](../../done/GRAPHICS-002-render-world-contract.md):
  completed immutable render-world/frame-input snapshot contract gate for
  downstream pass work.
- [GRAPHICS-003 — Frame recipe and default pipeline](../../done/GRAPHICS-003-frame-recipe-pipeline.md):
  completed reusable default frame recipe and canonical graph-construction gate.
- [GRAPHICS-004 — GPU-world allocation and lifetime](../../done/GRAPHICS-004-gpu-world-allocation-lifetime.md):
  completed retained-pool allocation/lifetime gate; depends on GRAPHICS-002.
- [GRAPHICS-005 — GPU-world compaction](../../done/GRAPHICS-005-gpu-world-compaction.md):
  completed managed-buffer fragmentation, opt-in compaction planning, and
  relocation reporting gate; depends on GRAPHICS-004.
- [GRAPHICS-006 — Material/shader/pipeline registry](../../done/GRAPHICS-006-material-shader-pipeline-registry.md):
  depends on GRAPHICS-002.
- [GRAPHICS-006Q — Material registry clarification backlog](../../done/GRAPHICS-006Q-material-registry-clarifications.md):
  completed clarification-only follow-up for GRAPHICS-006 material-slot,
  material-layout, and shader-asset identity decisions.
- [GRAPHICS-007 — Culling and draw buckets](../../done/GRAPHICS-007-culling-and-draw-buckets.md):
  depends on GRAPHICS-002 and GRAPHICS-004. Owns `CullingPass`
  (source module `Extrinsic.Graphics.Pass.Culling`) command contracts and the
  GPU draw-bucket contracts consumed by the surface, line, point, shadow, and
  selection passes.
- [GRAPHICS-007Q — Culling bucket clarification follow-ups](../../done/GRAPHICS-007Q-culling-bucket-clarifications.md):
  completed clarification-only follow-up for selection bucket specialization,
  `GpuInstanceStatic::VisibilityMask`/`Layer` policy, culling diagnostics
  ownership, and unsupported bucket combinations. Decisions also mirrored in
  `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-008 — Depth/surface/G-buffer passes](../../done/GRAPHICS-008-depth-surface-gbuffer-passes.md):
  depends on GRAPHICS-003, GRAPHICS-006, and GRAPHICS-007.
- [GRAPHICS-008Q — Surface pass clarification follow-ups](../../done/GRAPHICS-008Q-surface-pass-clarifications.md):
  completed clarification-only follow-up for alpha-mask depth/G-buffer bucket
  policy (reserved infrastructure until material alpha evaluation lands),
  descriptor-bind seam vs `SceneTableBDA`, renderpass attachment ownership
  across depth/surface/G-buffer paths (load/store, depth-write, and compare
  ops for prepass-on and prepass-off cases), and empty/invalid bucket
  diagnostics. Decisions also mirrored in
  `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-009 — Deferred lighting and shadows](../../done/GRAPHICS-009-deferred-lighting-and-shadows.md):
  depends on GRAPHICS-008.
- [GRAPHICS-009Q — Lighting and shadow clarification follow-ups](../../done/GRAPHICS-009Q-lighting-shadow-clarifications.md):
  completed docs-only follow-up for shadow atlas sizing input, backend sampler binding (global set 0 / binding 1, `sampler2DShadow` with `VK_COMPARE_OP_LESS_OR_EQUAL`), runtime/shadow extraction ownership of texel-snapped cascade view-projection matrices and missing-caster diagnostics, and deferred-lighting push-constant scope (scene-table-only; debug visualization owned by `Pass.DebugView`). Decisions also mirrored in `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-010 — Lines, points, and debug primitives](../../done/GRAPHICS-010-lines-points-debug-primitives.md):
  depends on GRAPHICS-002, GRAPHICS-003, and GRAPHICS-007.
- [GRAPHICS-010Q — Transient debug backend clarification follow-ups](../../done/GRAPHICS-010Q-transient-debug-backend-clarifications.md):
  completed docs-only follow-up for transient debug packet upload/routing (per-frame host-visible buffers, not GpuWorld/cull buckets), debug-triangle dedicated debug-surface overlay routing, depth-tested vs overlay pipeline-variant policy, and `InvalidSnapshotRecordCount`-only diagnostics. Decisions also mirrored in `docs/architecture/rendering-three-pass.md` and `src/graphics/renderer/README.md`.
- [GRAPHICS-011 — Spatial debug visualizers](../../done/GRAPHICS-011-spatial-debug-visualizers.md):
  depends on GRAPHICS-010.
- [GRAPHICS-011Q — Spatial debug adapter clarification follow-ups](../../done/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md):
  completed docs-only follow-up for concrete BVH/KD-tree/octree/convex-hull
  adapter ownership (runtime extraction, planned umbrella module
  `Extrinsic.Runtime.SpatialDebugAdapters`), adapter pre-filter policy,
  diagnostics handoff (`SpatialDebugVisualizerOptions`/`Diagnostics` remain
  the single graphics-visible budget/diagnostics surfaces), and adapter test
  placement (runtime integration tests under `tests/integration/runtime/`).
  Decisions also mirrored in `docs/architecture/graphics.md` and
  `src/graphics/renderer/README.md`.
- [GRAPHICS-012 — Picking, selection, and outline](../../done/GRAPHICS-012-picking-selection-outline.md):
  depends on GRAPHICS-002, GRAPHICS-007, GRAPHICS-008, and GRAPHICS-010. Owns
  the logical `PickingPass` stage (split source modules
  `Pass.Selection.EntityId`/`FaceId`/`EdgeId`/`PointId`) and the
  `SelectionOutlinePass` (`Pass.Selection.Outline`); see the Pass module
  naming map in `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-012Q — Picking backend/runtime clarification follow-ups](../../done/GRAPHICS-012Q-picking-backend-runtime-clarifications.md):
  retired docs-only clarification for shader-side ID encoding, backend
  readback drain, runtime ECS selection resolution, and transparent /
  special forward picking eligibility. Decisions also mirrored in
  `docs/architecture/rendering-three-pass.md`,
  `docs/architecture/graphics.md`, and `src/graphics/renderer/README.md`.
- [GRAPHICS-013 — Postprocess/debug-view/ImGui/present umbrella index](../../done/GRAPHICS-013-postprocess-debugview-imgui-present.md):
  retired planning-only umbrella; superseded by the split children
  GRAPHICS-013A/B/C and their retired clarification follow-ups
  GRAPHICS-013AQ/BQ/CQ. Listed here only as a historical pointer; not an
  eligible next-active candidate.
- [GRAPHICS-013A — Postprocess chain](../../done/GRAPHICS-013A-postprocess-chain.md):
  depends on GRAPHICS-003 and on GRAPHICS-008/GRAPHICS-009 wherever
  scene-color or HDR inputs are required.
- [GRAPHICS-013AQ — Postprocess backend clarification follow-ups](../../done/GRAPHICS-013AQ-postprocess-backend-clarifications.md):
  retired docs-only clarification for bloom backend shape (single
  half-resolution mip-chain `PostProcess.BloomScratch` with per-mip
  subviews, capped at six mips), histogram/exposure policy (fixed
  256-bin layout over `[-10, +10]` log2 stops, retained
  exposure-adaptation history buffer owned by `PostProcessSystem`,
  diagnostics readback via the `Picking.Readback` drain pattern), AA
  backend (FXAA single fullscreen draw with no LUT or intermediate;
  SMAA three-pass form with retained `AreaTex`/`SearchTex` lookups
  owned by `PostProcessSystem`, edge/blend intermediates folded into
  `PostProcess.AATemp` subresources), and descriptor/binding ownership
  (`SceneColorHDR`/`SceneColorLDR`/`PostProcess.*` are frame-recipe
  transient; only SMAA lookups + exposure history are retained
  graphics-owned). Decisions also mirrored in
  `docs/architecture/rendering-three-pass.md`,
  `docs/architecture/graphics.md`, and
  `src/graphics/renderer/README.md`.
- [GRAPHICS-013B — Debug view and render-target inspection](../../done/GRAPHICS-013B-debug-view-and-render-target-inspection.md):
  depends on GRAPHICS-013A.
- [GRAPHICS-013BQ — Debug-view backend clarification follow-ups](../../done/GRAPHICS-013BQ-debug-view-backend-clarifications.md):
  retired docs-only clarification for shader visualization modes
  (deterministic mapping from `FrameRecipeResourceKind` plus
  `DebugViewResourceClass` to LDR-color/Reinhard-tonemap/depth-grayscale/
  world-normal/integer-hash visualizations; no new
  `DebugViewSettings`/`DebugViewPushConstants` fields), descriptor
  binding ownership (one pass-local `Pass.DebugView` descriptor set
  with sampled image view + linear-clamp sampler; per-aspect
  `VkDescriptorSetLayout` and view creation remain backend-local under
  `src/graphics/vulkan`; `DebugViewRGBA` framegraph-owned), UI-name
  mapping (runtime/editor owns the human-readable -> canonical
  `FrameRecipeIntrospection::Resources[i].Name` dictionary built from
  `DebugViewSystem::BuildInspectionTable()` rows; graphics never
  receives display strings or imports ImGui/platform/window state),
  and buffer inspection (buffer-class resources stay non-previewable
  in `Pass.DebugView`; textual/statistical buffer inspection deferred
  to `GRAPHICS-014Q` runtime/editor surface that consumes existing
  per-owner diagnostics rather than adding a parallel buffer-readback
  API on `DebugViewSystem`). Decisions also mirrored in
  `docs/architecture/rendering-three-pass.md`,
  `docs/architecture/graphics.md`, and
  `src/graphics/renderer/README.md`.
- [GRAPHICS-013C — ImGui overlay and present/finalization](../../done/GRAPHICS-013C-imgui-overlay-and-present.md):
  depends on GRAPHICS-013B.
- [GRAPHICS-013CQ — ImGui/present backend clarification follow-ups](../../done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md):
  retired docs-only clarification for `ImDrawData` -> `ImGuiOverlayFrame`
  translation and submission timing (runtime-side Dear ImGui adapter
  builds the records and calls `SubmitFrame()` after `ImGui::Render()`
  and before `IRenderer::PrepareFrame()`; graphics never imports
  `imgui.h` or sees `ImDrawData`), overlay vertex/index upload + font/
  user texture descriptor policy + backend pipeline state (per-frame
  transient host-visible buffers under `src/graphics/vulkan` mirroring
  the `GRAPHICS-007Q`/`GRAPHICS-008Q` debug pattern; font atlas
  graphics-owned retained mirroring the SMAA `AreaTex`/`SearchTex`
  pattern from `GRAPHICS-013AQ`; user textures via existing
  `RHI::Bindless`; one backend-local pipeline bound through the
  existing `SetPipeline` seam), present finalization strategy
  (`Pass.Present` keeps the CPU-testable fullscreen-triangle form;
  backend-native `vkCmdCopyImage`/`vkCmdBlitImage` rejected as the
  contract finalization form because graphics cannot guarantee
  identical formats or `TRANSFER_DST_OPTIMAL` swapchain layout), and
  platform/backend boundaries (platform owns window/event-pump/DPI;
  backend owns surface/swapchain/acquire/present through `IDevice`;
  runtime owns composition; graphics owns only the backbuffer-import
  declaration, the `Pass.Present` command contract, and render-graph
  rejection of non-present writes to the imported backbuffer).
  Decisions also mirrored in `docs/architecture/rendering-three-pass.md`,
  `docs/architecture/graphics.md`, and `src/graphics/renderer/README.md`.
- [GRAPHICS-014 — Visualization attributes and overlays](../../done/GRAPHICS-014-visualization-attributes-overlays.md):
  depends on GRAPHICS-002 and GRAPHICS-010, and on GRAPHICS-015 wherever
  texture/atlas resources are required.
- [GRAPHICS-014Q — Visualization runtime/backend clarification follow-ups](../../done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md):
  retired docs-only clarification for runtime visualization packet producer
  ownership (`Extrinsic.Runtime.RenderExtraction` is sole owner; concrete
  adapters under planned `Extrinsic.Runtime.VisualizationAdapters` umbrella,
  mirroring the `Extrinsic.Runtime.SpatialDebugAdapters` pattern from
  `GRAPHICS-011Q`; graphics never imports geometry algorithm modules),
  invalid-packet handling (no runtime/extraction filtering; centralized
  validation through `ValidateVisualizationPackets()` at snapshot extraction
  time, rejected records dropped from `RenderWorld::Visualization` and counted
  in `VisualizationDiagnostics`; future backend upload stages do not
  re-validate, mirroring the `InvalidSnapshotRecordCount` drain pattern from
  `GRAPHICS-002`/`GRAPHICS-010Q`), overlay upload strategy (vector-field
  glyphs and isoline polylines are auxiliary draw resources expanded by a
  backend-local upload helper under `src/graphics/vulkan` into per-frame
  transient vertex/index buffers, consumed by dedicated visualization-overlay
  passes that LOAD `SceneColorHDR`/`SceneDepth` next to
  `Pass.Forward.Line`/`Pass.Forward.Point` with the `GRAPHICS-010Q`
  two-pipeline-variant policy; not routed through retained line/point cull
  buckets), and UV-vs-Htex bake selection policy (runtime/editor-owned;
  editor UI maps directly to `VisualizationFragmentBakeMapping`;
  `RecreateHtex` is an explicit user-driven request scheduled by
  runtime/geometry on a background task through
  `Extrinsic.Runtime.StreamingExecutor`; graphics increments
  `HtexRecreateRequestCount` and never owns the regeneration algorithm).
  Decisions also mirrored in `docs/architecture/rendering-three-pass.md`,
  `docs/architecture/graphics.md`, and `src/graphics/renderer/README.md`.
- [GRAPHICS-015 — GPU assets, textures, and residency](../../done/GRAPHICS-015-gpu-assets-textures-residency.md):
  depends on GRAPHICS-006 wherever material texture references are involved.
- [GRAPHICS-015Q — Texture residency backend clarification follow-ups](../../done/GRAPHICS-015Q-texture-residency-backend-clarifications.md):
  retired docs-only clarification for cache capacity/eviction (stays
  non-evicting in this slice with future eviction routed through the
  frame-anchored retire queue and refusing to evict the fallback lease),
  streaming mips (partial reupload via `RHI::TextureManager::Reupload()`
  preserving bindless index; full lease replacement reserved for
  format/extent/mip-count/usage changes), fallback texture content (one
  4x4 magenta-checker fallback covers all sampled `MaterialParams` slots;
  per-channel neutrality enforced by material shader code observing
  `UsedFallback`; visualization atlases drop deferred-residency
  descriptors per `GRAPHICS-014Q` rather than using the magenta
  fallback), bindless descriptor flush cadence (per-frame coalesced
  drain at the next `BeginFrame()` mirroring the `Picking.Readback`
  pattern; samplers deduplicated through `RHI::SamplerManager`;
  `MaterialSystem::ResolveTextureAssetBindings()` writes resolved
  bindless indices without forcing a flush; stale-bindless hazards on
  hot reload prevented by the existing `framesInFlight` retire queue),
  and runtime upload scheduling policy (runtime owns
  `InitializeFallbackTexture()` and the texture-typed asset bridges
  under planned `Extrinsic.Runtime.AssetBridges.Texture`; graphics never
  imports `AssetService`/`AssetEventBus`). Decisions also mirrored in
  `docs/architecture/graphics.md`,
  `docs/architecture/rendering-three-pass.md`,
  `src/graphics/renderer/README.md`, and
  `src/graphics/assets/README.md`.
- [GRAPHICS-017 — Camera, interaction, and gizmo boundaries](../../done/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md):
  depends on GRAPHICS-012 for picking handoff. Camera packet contracts may be
  defined earlier without blocking.
- [GRAPHICS-017Q — Camera/gizmo runtime clarification follow-ups](../../done/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md):
  retired docs-only clarification for runtime camera controller
  ownership (concrete controllers under planned umbrella module
  `Extrinsic.Runtime.CameraControllers`, mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters`/`VisualizationAdapters`/
  `AssetBridges.Texture` patterns from `GRAPHICS-011Q`/`014Q`/`015Q`;
  graphics never imports `src/platform/` or polls window events),
  pick-request scheduling (per-frame queue coalesced at runtime by
  `(viewport, pixel, request_kind)` key, single-shot `PickPixelRequest`
  span on `RenderFrameInput`, drained on the next `BeginFrame()`
  mirroring the `Picking.Readback` drain from `GRAPHICS-012Q`),
  transform-gizmo hit testing (runtime/editor-owned under planned
  umbrella `Extrinsic.Runtime.GizmoInteraction`, reading
  `CameraViewSnapshot::ViewProjection`/`PickRay` plus platform pointer
  pixels; `TransformGizmoRenderPacket` carries only render-relevant
  fields), interaction state storage (runtime/editor-owned per-frame
  state with mode/axis/delta/origin/snap/pivot/orientation; never
  enters graphics), and transform application/undo (runtime/editor
  applies authoring transforms and pushes undo commands; legacy
  orientation/snap/pivot/modifier-key/numeric-commit behaviors are
  enumerated through existing
  `docs/migration/nonlegacy-parity-matrix.md` editor-handoff rows that
  already cross-link `GRAPHICS-017Q`, with concrete promoted-task IDs
  gated by `GRAPHICS-020`).
- [GRAPHICS-018 — Vulkan renderer integration](../../done/GRAPHICS-018-vulkan-renderer-integration.md):
  depends on stable CPU/null contracts from GRAPHICS-002, GRAPHICS-003,
  GRAPHICS-004, GRAPHICS-006, GRAPHICS-007, GRAPHICS-008, GRAPHICS-009, and
  GRAPHICS-013C.
- [GRAPHICS-018Q — Vulkan integration clarification follow-ups](../../done/GRAPHICS-018Q-vulkan-integration-clarifications.md):
  retired; nonblocking docs-only follow-up resolving the four remaining
  Vulkan integration decisions —
  texture upload policy (guarded synchronous `WriteTexture()` stays the
  fail-closed baseline, runtime/streaming uses `RHI::ITransferQueue`, whole-
  image layout tracking until multi-subresource batching, multi-mip / multi-
  layer / cubemap batching plus opt-in `gpu;vulkan` smoke owned by
  GRAPHICS-018T), sampler anisotropy (probed and enabled when supported,
  silently disabled on missing support, clamped to physical-device max with
  one warn breadcrumb when reduced, no new RHI surface), `FallbackPipelineReason`
  extension policy (each counter and its reason enum stay 1:1 to its path;
  pipeline reasons append to the existing enum, future second reasons in
  other counters introduce a new path-local enum and append a matching
  `LastXxxReason` field to `FallbackDiagnosticsSnapshot`), and per-call
  breadcrumb logging (per-call canonical for bindless/transfer/pipeline
  paths during pre-bring-up, frame-loop counters keep the existing once-
  per-fail-closed-cycle rate limiting, migration to per-counter rate-limited
  breadcrumbs is a separate semantic task only when operational bring-up
  demonstrates many-per-frame firing). It must not be mixed with C++
  behavior work.
- [GRAPHICS-019 — Legacy graphics IO boundaries](GRAPHICS-019-legacy-graphics-io-boundaries.md):
  may run as planning in parallel with the implementation tasks above, but
  must not put IO ownership into graphics.
- [GRAPHICS-020 — Legacy graphics retirement gates](GRAPHICS-020-legacy-graphics-retirement-gates.md):
  final retirement gating after parity tasks complete.
- [GRAPHICS-022 — Rendergraph diagnostics and validation](../../done/GRAPHICS-022-rendergraph-diagnostics-validation.md):
  completed follow-on infrastructure hardening task; canonical ordering matches
  GRAPHICS-001 and it depends on GRAPHICS-003 for frame-recipe context while
  remaining CPU/null testable (no Vulkan requirement).
- [GRAPHICS-027 — Remove rendergraph compile diagnostic string shim](../../done/GRAPHICS-027-remove-rendergraph-diagnostic-shim.md):
  completed follow-up to GRAPHICS-022 that removed the
  `GetLastCompileDiagnostic()` string compatibility shim and the dead
  `CompiledRenderGraph::Diagnostic` field once downstream callers had
  migrated to structured `RenderGraphValidationResult` findings.
  CPU-only, no Vulkan requirement.
- [GRAPHICS-023 — Shader, material, and texture hot reload](GRAPHICS-023-shader-material-texture-hot-reload.md):
  depends on GRAPHICS-006 for registry/material/pipeline contracts and
  GRAPHICS-015 for GPU asset/texture residency.
- [GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md):
  completed planning task; recorded the per-row owner matrix for legacy
  overlay/presentation/editor-adjacent behaviors and cross-linked
  GRAPHICS-010/011/014/017 done tasks plus this backlog index. Retirement
  gating in GRAPHICS-020 resolves overlay/presentation modules through
  the matrix in `../../../docs/migration/nonlegacy-parity-matrix.md`.
- [GRAPHICS-025 — Hybrid, transparent, and special-material forward path](GRAPHICS-025-hybrid-transparent-special-material-path.md):
  future-facing follow-up after GRAPHICS-006/007/008/009 and GRAPHICS-013A
  establish material, bucket, opaque, lighting, and postprocess contracts.

## Agent selection rules

When picking the next rendering task to promote to active:

- **Prefer the earliest unblocked task** in the DAG above. "Unblocked" means
  every upstream dependency is either marked done in `tasks/done/` or has been
  explicitly recorded as not-in-scope for the candidate task.
- **Do not start pass implementation before GRAPHICS-016 extraction ownership
  is resolved.** Until live-ECS ownership is removed from promoted graphics
  APIs, downstream pass and pipeline tasks must not be promoted to active.
- **Do not mix docs-only cleanup with C++ behavior changes** in a single PR.
  Split the work into separate tasks/PRs rather than batching.
- **Do not copy legacy code into promoted graphics layers.** `src/legacy` is a
  behavioral reference only; promoted code must be reimplemented against the
  snapshot/view contracts.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md) —
  graphics layer architecture and ownership boundaries.
- [`docs/architecture/rendering-three-pass.md`](../../../docs/architecture/rendering-three-pass.md) —
  canonical rendering pass architecture reference.
- [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) —
  legacy-to-promoted parity tracking.
