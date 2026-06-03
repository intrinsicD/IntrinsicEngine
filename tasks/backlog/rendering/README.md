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
  image layout tracking for the batched path, multi-mip / multi-layer /
  cubemap batching plus opt-in `gpu;vulkan` smoke landed with GRAPHICS-018T),
  sampler anisotropy (probed and enabled when supported,
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
- [GRAPHICS-018T — Vulkan texture upload batching](../../done/GRAPHICS-018T-texture-upload-batching.md):
  retired follow-up to `GRAPHICS-018Q`/`GRAPHICS-026` that landed multi-mip /
  multi-layer / cubemap `VkBufferImageCopy` batching plus opt-in `gpu;vulkan`
  smoke; CPU-only fail-closed correctness path remains via the guarded
  synchronous `WriteTexture()` baseline. Upstream gates GRAPHICS-006,
  GRAPHICS-015, GRAPHICS-018, GRAPHICS-018Q, and GRAPHICS-026 are retired in
  `tasks/done/`.
- [GRAPHICS-019 — Legacy graphics IO boundaries](../../done/GRAPHICS-019-legacy-graphics-io-boundaries.md):
  completed planning task that assigns legacy graphics import/export/model/texture
  IO to `assets`, `geometry`, `runtime`, and `graphics/assets` handoff owners;
  final graphics layers must not own file IO or parser/exporter registries.
- [GRAPHICS-020 — Legacy graphics retirement gates](../../done/GRAPHICS-020-legacy-graphics-retirement-gates.md):
  completed planning gate map for retained legacy graphics/RHI/runtime-rendering
  modules. It records promoted owners, blocker tasks, test evidence, and final
  deletion readiness checks; no legacy source deletion happens in this slice.
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
- [GRAPHICS-023 — Shader, material, and texture hot reload](../../done/GRAPHICS-023-shader-material-texture-hot-reload.md):
  retired at `CPUContracted`; promoted shader-path/generation invalidation,
  pipeline recompile diagnostics, material-layout compatibility decisions,
  texture fallback/reload retention, and runtime asset-generation observation
  are CPU-testable without legacy graphics dependencies. Depends on
  GRAPHICS-006 for registry/material/pipeline contracts and GRAPHICS-015 for GPU
  asset/texture residency.
- [GRAPHICS-023A — GpuSceneSlot asset generation tracking](../../done/GRAPHICS-023A-gpu-scene-slot-asset-generation-tracking.md):
  completed focused child task that lands the `GpuSceneSlot::SourceAsset` and
  `GpuSceneSlot::LastSeenAssetGeneration` metadata required by completed
  `GRAPHICS-028` for later runtime residency hot-reload comparisons. It does
  not implement runtime polling, file watching, shader reload, or asset ingest.
- [GRAPHICS-023B — GpuSceneSlot asset rebind decision](../../done/GRAPHICS-023B-gpu-scene-slot-asset-rebind-decision.md):
  completed focused child task that adds the CPU-only `GpuSceneSlot` decision helper
  for comparing runtime-observed asset generations without importing
  `Graphics.GpuAssetCache` into the renderer component value type.
- [GRAPHICS-023C — Runtime asset-generation observation bridge](../../done/GRAPHICS-023C-runtime-asset-generation-observation.md):
  completed focused child task that lets `Runtime.RenderExtraction` observe
  `AssetInstance::Source` against a supplied `Graphics.GpuAssetCache` view and
  classify pending/up-to-date/rebind-required `GpuSceneSlot` states without
  performing upload, file-watching, shader reload, or GPU geometry rebinding.
- [GRAPHICS-023D — Runtime asset-generation rebind acknowledgment](../../done/GRAPHICS-023D-runtime-asset-generation-rebind-acknowledgment.md):
  completed focused child task that added the runtime-owned
  `AcknowledgeRenderableAssetRebind` helper closing the
  observe → classify → acknowledge loop opened by `GRAPHICS-023C`. The default
  `ExtractAndSubmit` path still observes without acknowledging; uploads,
  file-watching, shader reload, and texture residency reload remain out of
  scope.
- [GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md):
  completed planning task; recorded the per-row owner matrix for legacy
  overlay/presentation/editor-adjacent behaviors and cross-linked
  GRAPHICS-010/011/014/017 done tasks plus this backlog index. Retirement
  gating in GRAPHICS-020 resolves overlay/presentation modules through
  the matrix in `../../../docs/migration/nonlegacy-parity-matrix.md`.
- [GRAPHICS-025 — Hybrid, transparent, and special-material forward path](../../done/GRAPHICS-025-hybrid-transparent-special-material-path.md):
  retired planning slice; records deferred opaque base + future forward hybrid
  surface overlay semantics, material classifications (`Opaque`, `AlphaMask`,
  `Transparent`, `Unlit`, `SpecialForwardOnly`), `SceneColorHDR`/`SceneDepth`
  resource ownership, optional velocity/history/OIT follow-up boundaries, and
  split points without expanding GRAPHICS-006/007/008/009/013A.
- [GRAPHICS-028 — ECS renderable-entity to GpuWorld residency bridge](../../done/GRAPHICS-028-ecs-renderable-residency-bridge.md):
  completed planning task that records the runtime-owned residency bridge
  between live ECS queries and `GpuWorld`. Residency state lives in a
  runtime-owned sidecar/cache keyed by stable entity ID and may store
  graphics-owned `Graphics::Components::GpuSceneSlot` values; no GPU-typed
  component is introduced under `src/ecs/Components/`. It records per-stream
  static-vs-dynamic split, dirty-tag drain order, hierarchy decomposition,
  per-domain packers (mesh / graph / point cloud), and primitive-instancing
  policy. Follow-up implementation remains separate from this docs-only slice.
- [GRAPHICS-029 — Reference scene bootstrap and minimal renderable extraction](../../done/GRAPHICS-029-runtime-reference-scene-bootstrap.md):
  retired planning slice that locks down module placement
  (`Extrinsic.Runtime.ReferenceScene` at `src/runtime/Runtime.ReferenceScene.cppm`),
  bootstrap-seam shape (`IReferenceSceneProvider` + `ReferenceSceneRegistry`,
  one default `TriangleProvider`), renderable composition (HARDEN-060 default
  components plus `Graphics::Components::RenderSurface` and a future
  `ECS::Components::ProceduralGeometryRef` from GRAPHICS-030-Impl-A), camera
  authorship (provider-seeded `CameraViewInput` written into
  `RenderFrameInput::Camera`, forward-compatible with GRAPHICS-017Q
  `CameraControllers`), unlit-only first milestone, deterministic content via
  `MetaData::EntityName` (not `entt::entity` IDs), once-per-init lifetime with
  double-install guard, `EngineConfig::ReferenceScene::{Enabled, Selector}`
  default-off plumbing, O(1) steady-state extraction overhead, and a layering
  audit confirming no new graphics/RHI/asset edges. Implementation children
  GRAPHICS-029-Impl-A/B/C are identified but not opened; Impl-B depends on
  GRAPHICS-030-Impl-A landing `ProceduralGeometryRef`. Sandbox stays
  policy-light; no GPU-typed ECS components.
- [GRAPHICS-030 — Procedural-source geometry residency bridge (planning)](../../done/GRAPHICS-030-runtime-geometry-residency-bridge.md):
  retired planning-only first concrete slice of the GRAPHICS-028 contract; locked
  the procedural-geometry descriptor shape (closed `ProceduralGeometryKind` enum
  + POD `ProceduralGeometryParams`), cache identity (`ProceduralGeometryKey =
  (Kind, ParamsHash)`), `Runtime::ProceduralGeometryCache` placement on
  `RenderExtractionCache`, refcount/deferred-retire semantics aligned with
  `Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)`, the
  procedural-sentinel rule (default `Assets::AssetId{}` so GRAPHICS-023C
  observation short-circuits via `HasSourceAsset()`), per-kind packer placement
  in `Extrinsic.Runtime.ProceduralGeometryPacker`, the
  detect/EnsureResident/AllocateInstance/refresh/SetInstanceGeometry/transform-
  drain ordering, fail-closed failure modes and counters
  (`ProceduralGeometryUploads`/`ReuseHits`/`Releases`/`FreeRetires`/
  `Failed*`/`MissingPacker`/`InvalidParams`/`RefCountSaturated`/
  `ProceduralAndAssetSourceConflict`), O(1) steady-state characteristics,
  contract-test seam (`FindRenderableSidecarForTest`), extensibility to
  Cube/Quad/Sphere/LineStrip behind enum + packer additions only, and a
  layering audit confirming `ecs → core` and no new graphics edges.
  Implementation children `GRAPHICS-030-Impl-A/B/C/D` are identified but not
  opened. Asset-backed mesh residency is deferred to GRAPHICS-034.
- [GRAPHICS-031 — Default debug surface material and missing-material fallback (planning)](../../done/GRAPHICS-031-default-debug-surface-material.md):
  retired planning-only first slice of the default-material contract; locked the
  reuse of `kDefaultMaterialSlotIndex = 0u` as the
  `"Material.DefaultDebugSurface"` slot with
  `MaterialTypeID = kMaterialTypeID_DefaultDebugSurface = 2u`,
  `MaterialFlags::Unlit`, and a deterministic non-black `BaseColorFactor`
  (`{0.55, 0.20, 0.85, 1.0}`); the shader pair location
  (`assets/shaders/forward/default_debug_surface.vert/frag`); vertex format
  (position `vec3` + optional packed RGBA8 `uint32`, matching
  GRAPHICS-030-Impl-A's `Triangle` packer); descriptor/push-constant reuse
  (canonical scene-table BDA + `MaterialBuffer` SSBO at `set 3, binding 0`,
  no per-material descriptor set); pipeline state (`CullMode = Back`,
  `DepthCompareOp = Less`/`Equal`, `BlendEnabled = false`,
  `PolygonMode = Fill`, `TriangleList`, 1× MSAA, dynamic
  `{Viewport, Scissor}`); graphics-owned snapshot-consumption substitution
  at the renderer span-copy step with three additive
  `MaterialSystemDiagnostics` counters (`MissingMaterialFallbackCount`,
  `InvalidMaterialSlotCount`, `DefaultDebugSurfaceUses`); fail-closed
  visibility guarantee tied to a pixel-readback test; performance bounds
  (≤ 32 vertex / ≤ 16 fragment SPIR-V instructions, one pipeline at init,
  no per-frame state churn); extensibility family
  (`Material.DefaultDebug<Variant>` /
  `kDefaultDebug<Variant>MaterialSlotIndex` for `Wireframe`, `Line`,
  `Point`, `Normals`, `UVs`, `Depth`, `InstanceId`); and a layering audit
  confirming zero new dependency edges. Implementation children
  `GRAPHICS-031-Impl-A` (shader sources + pipeline + slot-0
  repopulation), `GRAPHICS-031-Impl-B` (substitution wiring + diagnostics
  counters), and the optional `GRAPHICS-031-Impl-C` (one additional
  debug variant) are identified but not opened.
- [GRAPHICS-032 — Minimal surface and present command recording path (planning)](../../done/GRAPHICS-032-minimal-surface-present-command-path.md):
  retired planning-only definition of the smallest visible-path command
  recording bodies. It locks `FrameRecipe::MinimalDebugSurface` with label
  `recipe.minimal-debug-surface`, the two-pass order
  `Pass.Surface.MinimalDebug` → `Pass.Present.MinimalDebug`, `SceneColorHDR` +
  `SceneDepth` target ownership, fullscreen-triangle present finalization,
  compiler-inferred framegraph barriers, property-based CPU-mock command
  assertions, diagnostics (`MinimalSurfacePassExecutions`,
  `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount`),
  recipe-vs-default isolation, failure modes, performance bounds, and layering.
  Implementation child slices (`GRAPHICS-032-Impl-A/B/C/D`) are identified but
  not opened. Vulkan operational-readiness defers to GRAPHICS-033. The
  bootstrap implementation artifacts from this planning track were deleted by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md)
  after the default recipe became canonical.
- [GRAPHICS-033 — Vulkan operational readiness and runtime fallback diagnostics (planning)](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md):
  retired planning-only operational-gate definition for the promoted Vulkan
  backend; locks the ordered gate checklist, single-source
  `EvaluateVulkanOperationalStatus(...)` status seam, append-only status/reason
  enums, runtime reconciliation truth table, `VulkanRequestedButNotOperational`
  startup breadcrumb, Vulkan-owned operational diagnostics snapshot,
  validation-layer fail-closed policy, required-vs-optional capability split,
  queue-family ownership rules, swapchain/device-loss transition handling, test
  split, performance characteristics, extensibility, and layering. Implementation
  children `GRAPHICS-033A/B/C/D` and gate follow-ups `GRAPHICS-033E/F` are
  retired; fail-closed behavior is preserved.
- [GRAPHICS-033A — Vulkan operational-status evaluator surface (done)](../../done/GRAPHICS-033A-vulkan-operational-status-evaluator.md):
  first implementation child of GRAPHICS-033. Landed the
  `VulkanOperationalStatusCode` / `VulkanOperationalReason` /
  `VulkanOperationalInputs` / `VulkanOperationalStatus` types and
  `EvaluateVulkanOperationalStatus(...)` in `Extrinsic.Backends.Vulkan` as
  the single source of truth for operational state, with CPU
  `contract;graphics` coverage of every gate item and matrix row. The
  originally-planned `*-vulkan-operational-status-seam.md` backlog entry was
  superseded by the as-landed `*-evaluator.md` filename and has been
  retired. Upstream consumer of GRAPHICS-033B/C/D.
- [GRAPHICS-033B — Vulkan operational diagnostics snapshot and runtime breadcrumb (done)](../../done/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md):
  retired second implementation child. Added `VulkanOperationalDiagnosticsSnapshot`
  with the five process-monotonic counters
  (`VulkanFallbackToNullCount`, `VulkanInitFailureCount`,
  `VulkanValidationErrorCount`, `VulkanOperationalGateFailureCount`,
  `VulkanDeviceLostOperationalDropCount`) and a reason histogram, and
  wired the `VulkanRequestedButNotOperational` startup/transition warn
  breadcrumb in `Runtime.Engine`. CPU `contract;graphics` and
  `contract;runtime` tests cover row-by-row counter side effects and
  one-shot breadcrumb emission. Depends on GRAPHICS-033A.
- [GRAPHICS-033C — Vulkan command recording for the minimal-debug-surface recipe (done)](../../done/GRAPHICS-033C-vulkan-minimal-recipe-recording.md):
  retired third implementation child. Implemented `Pass.Surface.MinimalDebug` and
  `Pass.Present.MinimalDebug` Vulkan recording bodies through the
  GRAPHICS-018R `RebuildOperationalResources()` seam, asserting CPU-mock
  parity against the GRAPHICS-032 contract. The operational reason row was
  renamed to `DefaultRecipeRecordingMissing` by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md)
  when the bootstrap recipe retired; full `Operational` still
  required the follow-up gate items (barrier validation, public service
  reconciliation) before GRAPHICS-033E/F retired. Depends on GRAPHICS-033A,
  GRAPHICS-032 (done), GRAPHICS-031 (done), GRAPHICS-018R (done).
- [GRAPHICS-033D — Opt-in Vulkan visible-triangle smoke fixture (done)](../../done/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md):
  retired fourth implementation child. Added
  `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp` under
  `gpu;vulkan;graphics` labels (excluded from the default CPU gate). On
  supported hosts the fixture drove one GRAPHICS-032 minimal-recipe
  frame through GLFW + real Vulkan device + surface + swapchain and
  asserted `IsOperational() == true` with no fallback counters or
  breadcrumbs. The fixture was removed by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md)
  after the default-recipe smoke/readback path replaced it. Depends on
  GRAPHICS-033A, GRAPHICS-033B, GRAPHICS-033C.
- [GRAPHICS-034 — Asset-backed mesh residency from AssetInstance::Source to GpuWorld (done)](../../done/GRAPHICS-034-asset-backed-mesh-residency-bridge.md):
  retired planning-only slice. Recorded the runtime-owned asset-source
  residency contract: `AssetInstance::Source` normalization, separate
  `Runtime::AssetGeometryCache`, `Assets::AssetId` key/refcount semantics,
  per-renderable state machine, ordering against `GpuAssetCache::Tick`,
  GRAPHICS-023A/B/C/D generation acknowledgment, sharing fairness,
  stuck-pending policy, visible failure fallback, diagnostics, performance,
  extensibility to non-mesh domains, and layering. Implementation child slices
  (`GRAPHICS-034-Impl-A/B/C/D/E`) are identified but not opened.

### Theme A — Triangle path implementation children (GRAPHICS-029A..033F, GRAPHICS-080)

These tasks open the previously-identified-but-not-opened implementation slices
under GRAPHICS-029..033 in dependency order so the sandbox can render its first
visible triangle through the promoted runtime/graphics path. Each leaf is small,
independently testable (CPU/null where possible), and gated as recorded.

- [GRAPHICS-029A — Reference scene skeleton (interface, registry, config field) (done)](../../done/GRAPHICS-029A-reference-scene-skeleton.md):
  landed; depended on GRAPHICS-029 (planning) and HARDEN-060 (done). Unblocks
  GRAPHICS-029B.
- [GRAPHICS-029B — TriangleProvider and reference camera substitution (done)](../../done/GRAPHICS-029B-triangle-provider-and-camera.md):
  landed; depended on GRAPHICS-029A (done) and GRAPHICS-030A (done) for the
  `ProceduralGeometryRef` type. Unblocks GRAPHICS-030B and GRAPHICS-031A on the
  Theme A triangle-path DAG.
- [GRAPHICS-030A — Procedural geometry descriptor, cache, and triangle packer (done)](../../done/GRAPHICS-030A-procedural-geometry-descriptor-cache.md):
  landed; ECS procedural component + runtime descriptor/cache/packer modules
  live in `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` and
  `src/runtime/Runtime.ProceduralGeometry{,Packer}.cppm`. Unblocks
  GRAPHICS-029B and GRAPHICS-030B.
- [GRAPHICS-030B — Wire RenderExtraction to the procedural geometry residency bridge (done)](../../done/GRAPHICS-030B-extraction-procedural-geometry-binding.md):
  landed; depended on GRAPHICS-030A (done) and GRAPHICS-029B (done). First
  task that calls `GpuWorld::UploadGeometry()` and `SetInstanceGeometry()`
  from extraction. Unblocks GRAPHICS-030C and provided the residency input that
  the now-retired GRAPHICS-032B bootstrap pass body used before
  GRAPHICS-081 deleted that scaffold.
- [GRAPHICS-030C — Procedural geometry refcount/free retire ordering (done)](../../done/GRAPHICS-030C-procedural-geometry-retire-ordering.md):
  landed; deferred-retire window, refcount-cancellation resurrection, and
  `RenderExtractionCache::TickProceduralGeometry` maintenance hook are
  wired with `contract;runtime` coverage.
- [GRAPHICS-031A — Default debug surface shaders and pipeline (done)](../../done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md):
  depends on GRAPHICS-031 (planning) and BUILD-001 (done; Sandbox shader
  compile wiring). Landed by commit `886b197`.
- [GRAPHICS-031B — Default debug surface substitution and diagnostics counters (done)](../../done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md):
  depends on GRAPHICS-031A. Landed by commit `24ac0b7`.
- [GRAPHICS-032A — `FrameRecipe::MinimalDebugSurface` recipe and registration (done)](../../done/GRAPHICS-032A-minimal-debug-surface-recipe.md):
  depends on GRAPHICS-031A. Landed by commit `3931705`; artifacts retired by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md).
- [GRAPHICS-032B — `Pass.Surface.MinimalDebug` CPU-mock command body (done)](../../done/GRAPHICS-032B-minimal-debug-surface-pass-body.md):
  depends on GRAPHICS-032A and GRAPHICS-030B. Landed by commit `7fff8ca`;
  artifacts retired by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md).
- [GRAPHICS-032C — `Pass.Present.MinimalDebug` body and end-to-end CPU acceptance test (done)](../../done/GRAPHICS-032C-minimal-debug-present-pass-and-acceptance.md):
  depends on GRAPHICS-032B. Landed by commit `e50c593`; artifacts retired by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md).
- [GRAPHICS-033A — Vulkan operational-status evaluator surface (done)](../../done/GRAPHICS-033A-vulkan-operational-status-evaluator.md):
  depends on GRAPHICS-033 (planning). Landed by commit `7a5886d`.
- [GRAPHICS-033B — Vulkan operational diagnostics snapshot and runtime breadcrumb (done)](../../done/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md):
  depends on GRAPHICS-033A. Landed by commit `d736d9b`.
- [GRAPHICS-033C — Vulkan command-recording for `FrameRecipe::MinimalDebugSurface` (done)](../../done/GRAPHICS-033C-vulkan-minimal-recipe-recording.md):
  depends on GRAPHICS-032C, GRAPHICS-031B, GRAPHICS-033B, and GRAPHICS-018R
  (done) operational-transition seam; bootstrap recording artifacts retired by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md).
- [GRAPHICS-033D — Opt-in `gpu;vulkan` visible-triangle smoke fixture (done)](../../done/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md):
  depends on GRAPHICS-033C; originally owned the bootstrap pixel-readback
  driver harness, which is now superseded by the default-recipe smoke/readback
  fixture after GRAPHICS-081.
- [GRAPHICS-033E — Wire the `BarrierValidationClean` operational gate (done)](../../done/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md):
  done (slice 2 dropped the over-restrictive compile-time clause from the
  producer publish; CPU gate green). Depends on GRAPHICS-033C (recording
  bodies landed) and GRAPHICS-022 (rendergraph validation surface).
  Producer-side wiring from the renderer's post-`ValidateRecipeCompiledGraph(...)`
  outcome to a new CPU-public `IDevice::NoteRecipeGraphValidation(bool)` setter
  consumed by `VulkanDevice::BuildOperationalInputs()`. Planning-gap fill: the
  parent GRAPHICS-033 planning slice identified Impl-A/B/C/D but did not
  enumerate an explicit child for gates 7/8.
- [GRAPHICS-033F — Wire the `PublicServiceReconciled` operational gate (done)](../../done/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md):
  done. Depended on GRAPHICS-033E (per the evaluator's first-failing-gate
  ordering). Backend-internal: re-derive
  `VulkanDevice::HasOperationalSafetyPrerequisites()` from raw live-handle
  preconditions and feed it into `BuildOperationalInputs()` for the gate-8
  input. With both 033E and 033F landed,
  `EvaluateVulkanOperationalStatus(...)` can finally return `{Operational, None}`
  on a Vulkan-capable host. `GRAPHICS-033D`'s `gpu;vulkan` smoke verified
  the initial visible-triangle path, and `GRAPHICS-080` retired after the full
  default/ci-vulkan gate set passed. Planning-gap fill: the parent
  GRAPHICS-033 planning slice identified Impl-A/B/C/D but did not enumerate
  an explicit child for gates 7/8.
- [GRAPHICS-032D — Opt-in `gpu;vulkan` smoke for `FrameRecipe::MinimalDebugSurface` (done)](../../done/GRAPHICS-032D-gpu-vulkan-minimal-recipe-smoke.md):
  depended on GRAPHICS-033C and GRAPHICS-033D; landed as the
  `MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
  sibling sharing the GRAPHICS-033D bounded `engine.Run()` driver helper for
  recipe-selector coverage. Fixture and selector artifacts retired by
  [GRAPHICS-081](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md).
- [GRAPHICS-080 — Flip reference config + CI preset to enable promoted Vulkan (done)](../../done/GRAPHICS-080-enable-promoted-vulkan-by-default.md):
  depends on GRAPHICS-033C (done).
  Adds the `ci-vulkan` configure preset and flips
  `CreateReferenceEngineConfig()` so the reference sandbox requests promoted
  Vulkan by default; this is the flip-switch enabling the end-to-end visible
  triangle once the GRAPHICS-033 operational gate is fully satisfied. Until
  then runtime continues to fall back to Null per the GRAPHICS-033 truth table
  with the `VulkanRequestedButNotOperational` breadcrumb.

Cross-layer Theme A leaves outside `rendering/`:
- [`done/BUILD-001` — Wire shader compilation to the promoted Sandbox build](../../done/BUILD-001-sandbox-shader-compile-wiring.md).
- [`runtime/RUNTIME-070` — Bootstrap GpuAssetCache fallback texture in Engine::Initialize](../../done/RUNTIME-070-fallback-texture-bootstrap.md) (done).

### Theme B′ — Default-recipe pass operational wiring (GRAPHICS-070..079)

These tasks wire each default-recipe pass family from soft-skip
(`SkippedNonOperational` / `SkippedUnavailable`) to real command recording.
They depend on the Theme A triangle path at minimum through GRAPHICS-031A
(default-debug pipeline), and individually through their listed pass-family
gates. CPU/null testable; `gpu;vulkan` coverage opts in alongside
`GRAPHICS-033D`.

- [GRAPHICS-070 — Default-recipe `Pass.Forward.Surface` operational wiring (done)](../../done/GRAPHICS-070-default-recipe-forward-surface-pass-wiring.md):
  depended on GRAPHICS-031A (slot-0 pipeline) and GRAPHICS-030B (residency).
  Landed the `NullRenderer`-owned `ForwardSurfacePass` + pipeline lease,
  `"SurfacePass"` executor routing under the forward lighting path, and the
  default-recipe lighting-path flip so the surface bind/draw shape records
  on the operational CPU/null path. GRAPHICS-072 owns the deferred branch.
- [GRAPHICS-071 — Default-recipe `Pass.Forward.Line` and `Pass.Forward.Point` wiring (done)](../../done/GRAPHICS-071-default-recipe-forward-line-point-wiring.md):
  depended on GRAPHICS-070. Landed retained line/point `NullRenderer`
  ownership, pipeline leases, executor routing, CPU contract coverage, and the
  documented `point.vert` + `point_retained.frag` canonical retained point
  variant.
- [GRAPHICS-072 — Default-recipe deferred GBuffer + lighting pass wiring (done)](../../done/GRAPHICS-072-default-recipe-deferred-gbuffer-and-lighting-wiring.md):
  depends on GRAPHICS-070 (done), GRAPHICS-073 (done). Now also owns the
  shadow-atlas deferred-lighting binding (at `set 1, binding 1` per
  `assets/shaders/deferred_lighting.frag`, i.e. binding 1 of the same
  global descriptor set as the deferred-path CameraUBO per
  `GRAPHICS-009Q`; the forward path keeps using `set 0, binding 1`) and
  the `DepthAttachment → ShaderRead` cross-pass barrier-transition test
  absorbed from GRAPHICS-073 Slice C, since both can only be exercised
  once `Pass.Deferred.Lighting` is recording in the operational executor.
- [GRAPHICS-073 — Default-recipe `Pass.Shadows` wiring + shadow atlas allocation (done)](../../done/GRAPHICS-073-default-recipe-shadow-pass-wiring.md):
  depended on GRAPHICS-070. Slice A landed the depth-only shadow pipeline,
  `NullRenderer` ownership, and the `"ShadowPass"` executor branch
  (`SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy);
  Slice B promoted `ShadowSystem` to own the `D32_FLOAT` atlas + the
  `sampler2DShadow`-bindable sampler, added the `FrameRecipeShadowSizing`
  typed seam + optional `FrameRecipeImports::ShadowAtlas`, and surfaced the
  `ShadowDiagnostics::MissingCasterCount` empty-cull-bucket diagnostic.
  Slice C (deferred-lighting shadow-atlas binding + cross-pass
  barrier-transition + `Recorded` integration tests) was transferred to
  GRAPHICS-072 with the binding location corrected from the original
  task's `set 0, binding 1` shorthand to `set 1, binding 1` to match
  `assets/shaders/deferred_lighting.frag` (forward path keeps `set 0,
  binding 1`).
- [GRAPHICS-074 — Default-recipe selection ID passes, outline pass, and picking readback drain](../../done/GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md):
  done (retired 2026-05-21 at `Operational` on the CPU/null gate). Slice A
  landed the EntityId selection pipeline + `"PickingPass"` executor route +
  GpuScene-aware `selection/entity_id.{vert,frag}` shader pair (PR #890;
  commits `ad2e40d` + `08af46e`, merge `558a75d`). The recipe-side
  follow-up reordered `PickingPass` after `DepthPrepass`, added
  `Read(SceneDepth, DepthRead)`, gated picking + its `EntityId` /
  `PrimitiveId` / `Picking.Readback` resources on `pickingActive =
  EnablePicking && EnableDepthPrepass` (with `EntityId` surviving when
  `EnableSelectionOutline=true`), and flipped
  `BuildSelectionEntityIdPipelineDesc()` to depth-equal / `D32_FLOAT`
  (PR #891; commits `dac6f47` + `b78347d`, merge `5b5309d`). Slice B
  added the Face/Edge/Point pipelines + executor fan-out; Slice C added
  the outline pipeline + `"SelectionOutlinePass"` route; Slice D split
  into D.1 (renderer-owned host-visible `Picking.Readback` buffer
  lifecycle), D.2 (recipe-side `ImportBuffer` + per-frame copy + RHI
  region seam), D.3 (`BeginFrame()`-side drain + `PublishPickResult` /
  `PublishNoHit` routing), and D.4 (outline push constants sourced from
  `RenderWorld::Selection`; PR #900, slice commit `b47c459`, merge
  `7c29e10`). Outstanding portability follow-up: compact
  `SelectedIds[16]` to a UBO/bindless tail so the outline push block
  stays <= 128 bytes on hosts at the Vulkan-guaranteed minimum (tracked
  separately, intentionally out-of-scope here).
- [GRAPHICS-075 — Default-recipe postprocess chain wiring](../../done/GRAPHICS-075-default-recipe-postprocess-chain-wiring.md):
  depended on GRAPHICS-072 (HDR scene color producer). Retired 2026-05-22 at
  `Operational` on the CPU/null gate for all five postprocess stage families
  (ToneMap / Bloom / FXAA + SMAA edge-blend-resolve / Histogram including the
  readback drain). Last in-task merge: Slice E.2 via PR #916 (commits
  `1bcdfe5` + `62fa3d2`, merge `be9d916`). The Vulkan-backend `Operational`
  claim and the bloom per-mip subresource barriers are tracked as standing
  follow-ups via the opt-in `gpu;vulkan` smoke and the `ICommandContext::TextureBarrier`
  RHI extension respectively, intentionally out-of-scope of this task.
- [GRAPHICS-076 — Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring](../../done/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md):
  depends on GRAPHICS-075 (done). Promoted to `tasks/active/` on 2026-05-23
  with a four-slice plan (canonical present → canonical debug-view →
  non-present-`Backbuffer`-write negative test → default-recipe
  `gpu;vulkan` visible-triangle smoke). Slices A–C landed via PRs
  #921 + #922 (Slice A follow-up), #923, and #924; Slice D graduated locally
  on 2026-05-28 with `DefaultRecipeSurfaceGpuSmoke` passing normally on a
  Vulkan-capable host after the BUG-012 command-stream fixes.
- [GRAPHICS-076E — Default-recipe pixel-readback parity harness](../../done/GRAPHICS-076E-default-recipe-pixel-readback.md):
  done 2026-05-29. Follow-up to GRAPHICS-076 Slice D. Adds default-recipe
  four-sample readback parity without reusing bootstrap-only diagnostics;
  the GRAPHICS-076F descriptor-slot fix made the opt-in Vulkan pixel smoke
  green.
- [GRAPHICS-077 — Backend transient-debug-primitive upload helper](../../done/GRAPHICS-077-transient-debug-primitive-upload-helper.md):
  depends on GRAPHICS-072 (uses `SceneColorHDR`/`SceneDepth` LOAD-store).
  Promoted to `tasks/active/` on 2026-05-23 with a four-slice plan
  (recipe/executor scaffold → triangle lane → line + point lanes →
  optional `gpu;vulkan` smoke). Slices A–C are landed/CPUContracted and Slice D
  command-stream smoke passed locally on 2026-05-28 with
  `TransientDebugSurfaceGpuSmoke` recording triangle/line/point lanes on an
  operational Vulkan frame.
- [GRAPHICS-077E — Transient-debug pixel-readback parity harness (done)](../../done/GRAPHICS-077E-transient-debug-pixel-readback.md):
  retired follow-up to GRAPHICS-077 Slice D. Added
  `SetTransientDebugBackbufferReadbackBuffer(...)` and
  `TransientDebugBackbufferReadbackCopyCount`, CPU/null fail-closed contract
  coverage, and an opt-in Vulkan readback/sample-color smoke for triangle,
  line, point, and clear pixels without reusing the canonical surface-readback
  diagnostics.
- [GRAPHICS-078 — Backend visualization-overlay upload helper](../../done/GRAPHICS-078-visualization-overlay-upload-helper.md):
  depends on GRAPHICS-077 (mirrors helper pattern) and GRAPHICS-072.
  Promoted to `tasks/active/` on 2026-05-24 with a four-slice plan
  (recipe/executor scaffold → vector-field lane → isoline lane →
  optional `gpu;vulkan` smoke), mirroring GRAPHICS-077 exactly. Slices A–C are
  landed/CPUContracted and Slice D command-stream smoke passed locally on
  2026-05-28 with `VisualizationOverlaySurfaceGpuSmoke` recording vector-field
  and isoline lanes on an operational Vulkan frame.
- [GRAPHICS-078E — Visualization-overlay pixel-readback parity harness (done)](../../done/GRAPHICS-078E-visualization-overlay-pixel-readback.md):
  retired follow-up to GRAPHICS-078 Slice D. Added
  `SetVisualizationOverlayBackbufferReadbackBuffer(...)` and
  `VisualizationOverlayBackbufferReadbackCopyCount`, CPU/null fail-closed
  contract coverage, deterministic placeholder lane geometry, and an opt-in
  Vulkan readback/sample-color smoke for vector-field, isoline, and clear
  pixels without reusing canonical or transient-debug readback diagnostics.
- [GRAPHICS-079 — Default-recipe `Pass.ImGui` wiring](../../done/GRAPHICS-079-default-recipe-imgui-pass-wiring.md)
  (done): depends on GRAPHICS-076 (PresentSource finalization) and
  `runtime/RUNTIME-090` (ImGui adapter producer). It wires the renderer
  `ImGuiPass` executor route + consumer handoff seam, retained font atlas,
  transient upload helper, `FrameRecipe.PresentSource` write topology, and
  per-command bindless user-texture sampling.
- [GRAPHICS-081 — Retire `FrameRecipe::MinimalDebugSurface` scaffold once default recipe is operational](../../done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md):
  done 2026-06-02. Deleted the bootstrap recipe, pass classes, executor
  branches, diagnostics counters, CMake entries, shaders, tests, and module
  inventory rows after GRAPHICS-076E/F made the default-recipe visible-triangle
  readback path canonical.

Cross-layer Theme B′ leaves outside `rendering/`:
- [`runtime/RUNTIME-080` — Texture asset bridge](../runtime/RUNTIME-080-asset-bridges-texture.md).
- [`RUNTIME-081` — Camera controllers](../../done/RUNTIME-081-camera-controllers.md).
- [`runtime/RUNTIME-082` — Spatial debug adapters](../../done/RUNTIME-082-spatial-debug-adapters.md) (done 2026-05-27).
- [`runtime/RUNTIME-083` — Visualization adapters](../../done/RUNTIME-083-visualization-adapters.md).
- [`runtime/RUNTIME-084` — Gizmo interaction](../runtime/RUNTIME-084-gizmo-interaction.md).
- [`runtime/RUNTIME-090` — Dear ImGui platform/renderer adapter](../../done/RUNTIME-090-imgui-platform-renderer-adapter.md) (retired 2026-06-02 at `CPUContracted`; Slice A standalone adapter module plus Slice B `Engine` frame-loop wiring landed).

Cross-layer Theme A leaves newly opened for the full working sandbox path:
- [`RUNTIME-085` — `GeometrySources` mesh residency bridge](../../done/RUNTIME-085-geometrysources-mesh-residency.md) (retired 2026-05-28 at `CPUContracted`; Slices A–C landed the mesh packer, extraction wiring, and dirty-domain reupload/retire ordering).
- [`runtime/RUNTIME-086` — `GeometrySources` graph residency bridge](../../done/RUNTIME-086-geometrysources-graph-residency.md) (retired 2026-05-30 at `CPUContracted`; Slice A graph packer plus Slices B + C extraction residency wiring landed).
- [`runtime/RUNTIME-087` — `GeometrySources` point-cloud residency bridge](../../done/RUNTIME-087-geometrysources-pointcloud-residency.md) (retired 2026-05-30 at `CPUContracted`).
- [`runtime/RUNTIME-088` — Mesh primitive view lifecycle](../../done/RUNTIME-088-mesh-primitive-view-lifecycle.md) (retired 2026-05-31 at `CPUContracted`; Slice A packers plus Slice B `RenderExtractionCache` sidecar residency wiring landed).
- [`runtime/RUNTIME-089` — Runtime selection controller and snapshot handoff](../../done/RUNTIME-089-selection-controller.md) (retired 2026-05-31 at `CPUContracted`; Slice A standalone `SelectionController` module, Slice B `Engine::RunFrame` + `RenderExtractionCache::ExtractAndSubmit` wiring).
- [`runtime/RUNTIME-092` — Runtime stable entity lookup sidecar](../../done/RUNTIME-092-stable-entity-lookup.md) (retired 2026-05-31 at `CPUContracted`; Slice A standalone `StableEntityLookup` module, Slice B `Engine::RunFrame` per-frame rebuild + `SelectionController` render-id seam routing).
- [`runtime/RUNTIME-093` — Primitive selection refinement](../../done/RUNTIME-093-primitive-selection-refinement.md) (done, 2026-06-01, `CPUContracted`).
- [`UI-001` — Sandbox editor shell and core panels](../../done/UI-001-sandbox-editor-shell-panels.md) (retired 2026-06-03 at `CPUContracted`).
- [`runtime/RUNTIME-095` — Working sandbox app acceptance path](../runtime/RUNTIME-095-working-sandbox-acceptance.md).

### Modernization roadmap (GRAPHICS-035..058)

The tasks below form the agreed phased path from the current 2025-era
foundation to a 2026+ feature set. The umbrella index is GRAPHICS-035; each
leaf task is a planning-only slice following the GRAPHICS-029..034 pattern
(implementation children identified but not opened). Phases are independently
testable; each phase derisks the next.

- [GRAPHICS-035 — Rendering modernization roadmap (umbrella planning index, done)](../../done/GRAPHICS-035-modernization-roadmap.md):
  records the agreed phased roadmap (Phase 1 frame structure, Phase 2 shader
  & material modernization, Phase 3 RT & GI, Phase 4 research differentiators,
  Phase 5 frontier). Parent of GRAPHICS-036..058. No implementation, no
  shader/pipeline changes, no CMake option growth.

#### Phase 1 — Modern frame structure

- [GRAPHICS-036 — Pipelined frames and double-buffered render world (done)](../../done/GRAPHICS-036-pipelined-frames-extraction-doublebuffer.md):
  locks the contract for sim-N / render-N-1 pipelining against an immutable
  double-buffered render world. Owner layers: `runtime` (pool + swap),
  `graphics/renderer` (consumer). Depends on GRAPHICS-002, GRAPHICS-016.
- [GRAPHICS-037 — Async compute and multi-queue scheduling in the frame graph (done)](../../done/GRAPHICS-037-async-compute-multi-queue-rendergraph.md):
  locks `QueueAffinity` enum, partitioning, cross-queue timeline-semaphore
  edges, ownership transfer, and CPU-testable null-RHI mocks. Depends on
  GRAPHICS-022, GRAPHICS-018T.
- [GRAPHICS-038 — HZB and two-phase occlusion culling extension to CullingPass (done)](../../done/GRAPHICS-038-hzb-two-phase-occlusion-culling.md):
  locks HZB resource shape + build pass + phase-1/phase-2 cull shader
  extension preserving the 8-bucket lane contract. Depends on GRAPHICS-007.
- [GRAPHICS-039 — Clustered light binning (done)](../../done/GRAPHICS-039-clustered-light-binning.md):
  locks froxel-grid cluster build, light-to-cluster assignment, surface-shader
  binding. Depends on GRAPHICS-009.
- [GRAPHICS-040 — TAA pass and reconstructor/upscaler interface seam (done)](../../done/GRAPHICS-040-taa-and-reconstructor-interface.md):
  locks sub-pixel jitter, motion-vector buffer, history color buffer, and the
  vendor-agnostic `IReconstructor` seam (DLSS/FSR/XeSS/MetalFX/NRD plug-in
  point). Depends on GRAPHICS-013A, GRAPHICS-036.

#### Phase 2 — Shader & material modernization

- [GRAPHICS-041 — Slang as canonical shading language with module compilation and hot reload (done)](../../done/GRAPHICS-041-slang-shader-pipeline-and-hot-reload.md):
  locks offline Slang compilation under `tools/`, module/generic system,
  hot-reload retire-deadline wiring, autodiff annotation policy. Depends on
  GRAPHICS-006, GRAPHICS-023.
- [GRAPHICS-042 — PBR feature completeness and IBL (done)](../../done/GRAPHICS-042-pbr-feature-completeness-and-ibl.md):
  locks GGX multi-scatter compensation, sheen, anisotropy, clear-coat, and
  split-sum IBL with prefiltered envmap + DFG LUT. Depends on GRAPHICS-006,
  GRAPHICS-009.
- [GRAPHICS-043 — Visibility buffer recipe and deferred materialization (done)](../../done/GRAPHICS-043-visibility-buffer-deferred-materialization.md):
  locks vis-buffer encoding, tile classification, per-material compute
  materialization kernels with bindless sampling. Depends on GRAPHICS-008,
  GRAPHICS-041, GRAPHICS-044.
- [GRAPHICS-044 — Meshlet geometry representation in GpuGeometryRecord (done)](../../done/GRAPHICS-044-meshlet-geometry-representation.md):
  locks meshlet table extension on `GpuGeometryRecord` (≤ 64 vertices, ≤ 124
  primitives, bounding sphere + normal cone), authoring-time meshletization
  in `assets/`. Depends on GRAPHICS-004.

#### Phase 3 — Ray tracing and global illumination

- [GRAPHICS-045 — Ray tracing RHI extension (IRayTracingDevice) (done)](../../done/GRAPHICS-045-ray-tracing-rhi-extension.md):
  locks the optional `IRayTracingDevice` capability surface (BLAS/TLAS,
  inline RT in compute, ray pipelines + SBT) and the `GRAPHICS-033`
  operational-gate extension policy. Depends on GRAPHICS-033.
- [GRAPHICS-046 — Hybrid GI: ReSTIR DI/GI hardware path and software fallback (planning)](GRAPHICS-046-hybrid-gi-restir-and-fallback.md):
  locks `GiPathKind` recipe selection, ReSTIR DI/GI passes + reservoir
  buffers (HW), DDGI probe volume + screen-space probes (SW). Depends on
  GRAPHICS-039, GRAPHICS-045.
- [GRAPHICS-047 — Virtual Shadow Maps to replace cascade atlas (planning)](GRAPHICS-047-virtual-shadow-maps.md):
  locks 16K virtual address space, 128² page allocation, page-table
  encoding, and meshlet-cluster caster culling. Depends on GRAPHICS-009,
  GRAPHICS-038, GRAPHICS-044.

#### Phase 4 — Research differentiators

- [GRAPHICS-048 — 3D Gaussian Splatting rasterizer pass over the PointCloud primitive (planning)](GRAPHICS-048-gaussian-splatting-rasterizer.md):
  locks extended point record (anisotropic covariance + opacity + SH coeffs),
  tile-based sort + composite, `.gsplat` shipping format. Depends on
  GRAPHICS-014, GRAPHICS-030.
- [GRAPHICS-049 — Neural radiance cache slot in the GI path (planning)](GRAPHICS-049-neural-radiance-cache-slot.md):
  locks small-MLP shape, online training pass, cache invalidation, GI
  consumer seam. Depends on GRAPHICS-041, GRAPHICS-046.
- [GRAPHICS-050 — Neural texture compression with random-access decode (planning)](GRAPHICS-050-neural-texture-compression.md):
  locks `.ntc` shipping format + per-material decoder Slang module + BCn
  fallback. Depends on GRAPHICS-041, GRAPHICS-042.
- [GRAPHICS-051 — Differentiable rendering mode (planning)](GRAPHICS-051-differentiable-rendering-mode.md):
  locks forward+backward render-graph compile, adjoint buffer lifetime,
  `Pass.Loss` + gradient sink, build-time gating to keep production unchanged.
  Depends on GRAPHICS-041.
- [GRAPHICS-052 — Deltaful GPU-resident scene (planning)](GRAPHICS-052-deltaful-gpu-resident-scene.md):
  locks per-change delta records, persistent GPU scene buffer, apply-deltas
  pass, full-extract fallback. Promoted from `GRAPHICS-004Q`. Depends on
  GRAPHICS-002, GRAPHICS-004, GRAPHICS-016, GRAPHICS-036.

#### Phase 5 — Frontier

- [GRAPHICS-053 — Mesh shaders RHI extension (IMeshShaderDevice) (planning)](GRAPHICS-053-mesh-shaders-rhi-extension.md):
  locks the optional `IMeshShaderDevice` capability surface (task→mesh
  pipeline, indirect dispatch, payload limits) and recipe-selection
  fallback to `MeshletViaCompute`. Depends on GRAPHICS-033, GRAPHICS-044.
- [GRAPHICS-054 — Work graphs RHI extension (IWorkGraphDevice) (planning, long-horizon)](GRAPHICS-054-work-graphs-rhi-extension.md):
  locks the optional `IWorkGraphDevice` capability surface and recipe-slot
  reservation. Explicitly long-horizon: no implementation children opened
  until backend support and at least one consumer exist. Depends on
  GRAPHICS-053.
- [GRAPHICS-055 — Streaming Virtual Textures (planning)](GRAPHICS-055-streaming-virtual-textures.md):
  locks 128² virtual page table, feedback pass, runtime page resolver,
  KTX2/Basis Universal UASTC shipping format. Depends on GRAPHICS-018T,
  GRAPHICS-026, GRAPHICS-041.
- [GRAPHICS-056 — Virtualized meshes with cluster DAG and continuous LOD (planning, bounded scope)](GRAPHICS-056-virtualized-meshes-cluster-lod.md):
  locks DAG record shape + LOD selector pass + HZB integration. Bounded
  scope: explicitly NOT Nanite parity (no SW raster, no cluster-page
  streaming). Depends on GRAPHICS-038, GRAPHICS-044, GRAPHICS-053.
- [GRAPHICS-057 — DirectStorage-analog GPU decompression hookpoint on the transfer queue (planning)](GRAPHICS-057-directstorage-gpu-decompression.md):
  locks `IGpuDecompressionTransferQueue` capability surface, GDeflate/Zstd
  payload formats, CPU-fallback rule. Depends on GRAPHICS-018T, GRAPHICS-026.
- [GRAPHICS-058 — Frame generation pass (planning)](GRAPHICS-058-frame-generation-pass.md):
  locks `IFrameGenerator` interface (interpolation/extrapolation), reference
  motion-blend interpolator, presentation pacing in `runtime/`. Depends on
  GRAPHICS-013C, GRAPHICS-040.

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
