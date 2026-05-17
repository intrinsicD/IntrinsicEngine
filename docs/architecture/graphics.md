# Graphics Architecture

Graphics is organized into explicit sublayers:

- `graphics/rhi`: low-level rendering hardware abstraction.
- `graphics/vulkan`: Vulkan backend implementation.
- `graphics/framegraph`: transient render dependency graph and scheduling.
- `graphics/renderer`: high-level render orchestration using snapshots/views.

## Rules

- Graphics consumes immutable/snapshot data from higher-level systems.
- Graphics must not depend on live ECS ownership structures.
- Graphics-owned GPU handles, slots, leases, and backend resource IDs must not be stored in canonical `src/ecs` components.
- Runtime owns ECS-to-graphics extraction and any sidecar/cache mappings from ECS entities, asset IDs, or geometry source handles to graphics GPU handles.
- Runtime extraction is implemented by `Extrinsic.Runtime.RenderExtraction`, which queries live ECS, maintains entity-to-graphics sidecars outside canonical ECS components, and submits `RuntimeRenderSnapshotBatch` records through `IRenderer::SubmitRuntimeSnapshots()`.
- Backend code depends on RHI + allowed platform abstractions only.
- Vulkan backend implementation files emit diagnostics through `Core::Log::*`; they must not bypass the project logger with direct `stderr` writes.

## Renderer/RHI frame lifecycle

- `IRenderer::BeginFrame()` delegates acquisition to `RHI::IDevice::BeginFrame()` and clears renderer-owned per-frame snapshot storage before extraction.
- `IRenderer::ExecuteFrame()` imports the frame backbuffer from `RHI::IDevice::GetBackbufferHandle(frame)` when building the default frame recipe, then brackets render-graph barrier/command recording with `ICommandContext::Begin()` / `End()` on the frame graphics context.
- The default renderer records the canonical `CullingPass` command sequence through `CullingSystem` only when the injected device is operational and culling output is available. Stub/null devices still compile and execute the backend-independent graph but skip routed command bodies so the default CPU gate remains Vulkan-free.
- Culling and depth-prepass pipeline/resource failures are soft-fail conditions. They leave render-graph compile/execute intact, skip only the unavailable command body, and report `SkippedUnavailable` through `RenderGraphFrameStats.CommandRecords` rather than asserting in renderer code.
- `DepthPrepass` consumes the culling surface-opaque indirect bucket, so it records only after culling output is available and the cached depth-prepass pipeline handle is valid.
- `RenderGraphFrameStats` is split into `Compile`, `Execute`, and `CommandRecords` sub-structures. Per-pass command status is name-keyed (`CullingPass`, `DepthPrepass`, etc.) with recorded, skipped-non-operational, and skipped-unavailable outcomes. `Diagnostic` remains graph-scoped, while lifecycle failures before graph compilation use `LifecycleDiagnostic`.
- `IRenderer::EndFrame()` delegates to `RHI::IDevice::EndFrame()` and reports the device post-`EndFrame` global frame number for maintenance callers, not the completed `FrameHandle::FrameIndex`. Runtime remains the composition owner for presentation and calls `IDevice::Present(frame)` after the renderer frame has ended.
- `IRenderer::RebuildOperationalResources()` is the backend-neutral reset/rebind seam for devices that were initialized fail-closed and later become operational. Runtime owns transition detection: once `IDevice::IsOperational()` changes from false to true, runtime waits for device idle, calls the renderer hook, then marks the renderer operational for subsequent frames. The renderer rebuilds material GPU buffers, `GpuWorld` buffers/scene-table bindings, culling output resources, the depth-prepass pipeline, and the GRAPHICS-031A slot-0 default-debug-surface pipeline lease through RHI managers only; the `MinimalDebugSurface`/`MinimalDebugPresent` scaffold passes (GRAPHICS-032/033C) reuse that same slot-0 lease so their recording bodies become live in the same transition without separate pipeline creation. The seam must not import or branch on Vulkan backend types. CPU/null paths remain valid because non-operational devices continue to compile/execute the graph while command bodies report `SkippedNonOperational`.
- `RHI::SamplerDesc` owns backend-neutral filtering, addressing, LOD, comparison, anisotropy, and `SamplerBorderColor` state. The default border color is opaque black for compatibility with earlier sampler creation paths; backends translate it internally (for example Vulkan maps it to `VkBorderColor`) without exposing API-native types through RHI or renderer code.
- The promoted Vulkan backend defines the same `IDevice` lifecycle/resource surface behind the RHI seam, selected only when both `RenderConfig::EnablePromotedVulkanDevice == true` and `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` are set; otherwise `GraphicsBackend::Vulkan` falls back to the Null device. See [ADR-0004 — Vulkan backend bring-up and fail-closed fallback](../adr/0004-vulkan-backend-bringup-and-fallback.md) for the full bring-up sequence, `IsOperational()` predicate, diagnostics snapshots, fail-closed status taxonomy, rate-limited breadcrumb policy, and the `GRAPHICS-018Q` follow-up resolutions (texture upload, sampler anisotropy, fallback reason taxonomy, per-call vs frame-loop breadcrumbs).

## Vulkan operational readiness and runtime fallback

See [ADR-0005 — Vulkan operational readiness gate and runtime reconciliation](../adr/0005-vulkan-operational-readiness-gate.md) for the 9-step ordered gate, the `EvaluateVulkanOperationalStatus(...)` single-source-of-truth evaluator, the `VulkanOperationalStatusCode`/`VulkanOperationalReason` taxonomy, the runtime reconciliation truth table, the `VulkanOperationalDiagnosticsSnapshot` surface, the validation-layer policy, the required-vs-optional capability split, and the rules for transient operational drops.

## GPU scene ownership

- Runtime composes the CPU render scene from ECS, assets, geometry, transforms, materials, lights, selection, camera, and debug inputs.
- Graphics receives immutable `RenderWorld` / `RenderFrameInput` snapshots plus runtime-submitted transform/light/visualization record batches and uploads them into graphics-owned GPU scene buffers.
- `RenderWorld` currently exposes renderer-owned spans of `RenderableSnapshot`
  and `LightSnapshot` values, sanitized transient debug line/point/triangle
  packet spans, data-only transform-gizmo render packet spans, data-only
  visualization packet spans/diagnostics, camera/view/frustum snapshots,
  defaulted optional packets for picking, selection, shadows,
  postprocess/readback, and invalid-record diagnostics. Runtime-submitted
  batches are copied by the renderer before these spans are exposed, so no live
  ECS storage is retained by graphics.
- `Extrinsic.Graphics.CameraSnapshots` validates camera matrices, extracts
  frustum planes, and derives pick rays from immutable pixel pick requests.
  Viewport dimensions are carried as the core-owned `Core::Extent2D` value type,
  not as a dependency on the live platform window port.
  Runtime/platform own camera motion, input polling, pick-request creation,
  gizmo hit testing, and transform application; graphics only consumes the
  resulting data snapshots. See [ADR-0006 — Camera, picking-request, and gizmo runtime handoff](../adr/0006-camera-picking-and-gizmo-runtime-handoff.md) for the runtime camera-controller umbrella, single-shot `PickPixelRequest` scheduling and coalescing, transform-gizmo hit-test ownership, interaction-state storage and lifetime, and transform-application/undo policy; legacy `Graphics.TransformGizmo` / `Graphics.Interaction` feature handoff stays tracked in [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md).
- `Extrinsic.Graphics.SelectionSystem` is a CPU-visible reporting-only seam for picking. The renderer copies the requested pixel(s) into the graphics-owned host-visible `Picking.Readback` buffer at frame-record time and drains it on the next `BeginFrame()` through `SelectionSystem::PublishPickResult(...)` / `PublishNoHit()`; runtime owns `StableEntityId` → live ECS resolution and the selection-outline input mask consumed by `SelectionOutlinePass`. See [ADR-0007 — Picking, selection, and outline reporting seam](../adr/0007-picking-selection-and-outline.md) for the `EntityId` / `PrimitiveId` `EncodedSelectionId` packing, the per-pass payload sources, the drain timing and diagnostic-counter invariants, and the transparent / special-material picking eligibility gate that holds until `GRAPHICS-025` lands.
- `Extrinsic.Graphics.SpatialDebugVisualizers` converts data-only spatial debug snapshots (bounds, hierarchy nodes, split planes, convex-hull wire edges, point markers) into transient debug packets with deterministic limits and diagnostics. Graphics never imports geometry tree implementations or editor state; concrete adapters live in runtime extraction. See [ADR-0008 — Spatial debug visualizer runtime adapters](../adr/0008-spatial-debug-visualizer-adapters.md) for the `Extrinsic.Runtime.SpatialDebugAdapters` umbrella, the `Build*SpatialDebugInputs(...)` naming convention, the frozen packet/diagnostics contract, the pre-filter vs graphics-side truncation budget split, and the `integration;runtime;graphics` adapter test placement.
- `Extrinsic.Graphics.VisualizationPackets` is the promoted data-only seam for scalar/color/vector attribute buffers, vector-field overlays, isoline overlays, UV-backed fragment-bake atlas descriptors, and Htex patch-preview/bake atlas descriptors. The packet layer validates domains, ranges, colormap IDs, buffer-address seams, missing texcoords/resources, and Htex recreation requests through `ValidateVisualizationPackets(...)` at snapshot extraction time; rejected records are dropped and counted in `VisualizationDiagnostics`. See [ADR-0009 — Visualization packets, validation, and overlay upload](../adr/0009-visualization-packets-and-overlay-upload.md) for the `Extrinsic.Runtime.VisualizationAdapters` runtime ownership, the "no runtime filtering" rule, the backend-local vector-field/isoline overlay upload helper that mirrors `GRAPHICS-007Q`/`GRAPHICS-010Q`/`GRAPHICS-013CQ`, the `ExistingTexcoords`/`ExistingHtex`/`RecreateHtex` fragment-bake mapping policy with `Extrinsic.Runtime.StreamingExecutor` async baking, and the two-pipeline-variant depth-test policy reused from `GRAPHICS-010Q`.
- `Extrinsic.Graphics.ColormapSystem` owns built-in colormap LUT residency. It
  allocates retained 256-sample RGBA8 LUT textures through RHI managers, submits
  the initial bytes through `RHI::ITransferQueue`, and exposes `IsReady()` as the
  first-frame readiness guard. Bindless colormap indices stay invalid until all
  LUT transfer tokens are valid and complete, keeping renderer/runtime callers on
  the async transfer seam instead of the blocking `IDevice::WriteTexture()`
  helper.
- `Extrinsic.Graphics.PostProcessSystem` owns the backend-agnostic HDR-to-LDR chain. `SceneColorHDR`, `SceneColorLDR`, `PostProcess.BloomScratch`, `PostProcess.Histogram`, and `PostProcess.AATemp` are frame-recipe transient resources owned by the framegraph; `PostProcessSystem` retains only the SMAA `AreaTex`/`SearchTex` lookup textures and the exposure-adaptation history buffer. See [ADR-0010 — Postprocess chain backend policy](../adr/0010-postprocess-chain-backend-policy.md) for the bloom mip-chain pyramid + filter taps, the 256-bin histogram + drain pattern, the FXAA/SMAA mutual-exclusion + named-subresource `AATemp` policy, the retained-vs-frame-transient ownership split, and the one-push-constant-block / one-pass-local-descriptor-set binding rule.
- `Extrinsic.Graphics.DebugViewSystem` owns the backend-agnostic render-target inspection and resource-selection seam. It builds a deterministic inspection table from `FrameRecipeIntrospection`, classifies resources (texture, depth texture, buffer, backbuffer, alias, unknown), and resolves the requested `DebugViewSettings::RequestedResourceName` with structured fallback diagnostics. See [ADR-0011 — Debug-view inspection table and visualization mode mapping](../adr/0011-debug-view-inspection-table.md) for the deterministic `(FrameRecipeResourceKind, DebugViewResourceClass)` → shader visualization-mode table, the one-pass-local-descriptor-set + per-aspect view rules, the runtime/editor-owned UI-name dictionary, and the buffer-class non-previewability + deferred textual/statistical inspection policy.
- `Extrinsic.Graphics.ImGuiOverlaySystem` and
  `Extrinsic.Graphics.Pass.Present` own the backend-agnostic ImGui
  overlay summary contract and the imported backbuffer finalization
  shim. Per `GRAPHICS-013CQ`, runtime/editor code (the runtime-side
  Dear ImGui platform/renderer adapter, **not** graphics) translates
  `ImDrawData` produced by `ImGui::Render()` into `ImGuiOverlayFrame`
  records and calls `ImGuiOverlaySystem::SubmitFrame(...)` once per
  frame after `ImGui::Render()` and before
  `IRenderer::PrepareFrame()`, alongside the
  `IRenderer::SubmitRuntimeSnapshots()` handoff; the renderer invokes
  the matching `ClearFrame()` at end-of-frame after `Pass.Present`
  finalizes the backbuffer. Graphics never imports `imgui.h`, never
  calls Dear ImGui platform/renderer backends, and never sees
  `ImDrawData` directly. Overlay vertex/index buffer upload mirrors
  the transient debug expansion pattern from
  `GRAPHICS-007Q`/`GRAPHICS-008Q`: per-frame host-visible (transient)
  GPU buffers owned by a backend-local upload helper under
  `src/graphics/vulkan` and recycled each frame, never retained on
  `GpuWorld` and never exposed through RHI or renderer module
  surfaces. The font atlas texture is graphics-owned retained
  (mirroring SMAA `AreaTex`/`SearchTex` ownership from
  `GRAPHICS-013AQ`): `ImGuiOverlaySystem::Initialize()` accepts the
  runtime-supplied font-atlas pixel buffer (`R8_UNORM` default,
  `R8G8B8A8_UNORM` for colored atlases) and allocates a single
  retained `RHI::TextureHandle` through `RHI::TextureManager`, freed
  at `Shutdown()`; DPI/font rebuilds re-run the `Shutdown()` /
  `Initialize()` cycle. User textures (image previews referenced by
  `ImTextureID` in editor panels) flow through the existing
  `RHI::Bindless` heap as bindless texture indices carried in a
  backend-local per-cmd parameter buffer; no new graphics-visible
  descriptor surface is added, and
  `ImGuiOverlayFrame::DrawLists[i].UsesUserTexture` remains the only
  graphics-visible diagnostics flag for user-texture presence.
  `ImGuiPass` owns one pipeline created by the backend at startup and
  bound through the existing `SetPipeline`/`RHI::PipelineHandle` seam;
  backend Vulkan pipeline state (dynamic rendering against the
  present-source attachment, premultiplied-alpha blend, no depth test,
  scissor enabled, viewport derived from `DisplayWidth`/`DisplayHeight`,
  vertex stride `sizeof(ImDrawVert)`) remains backend-local under
  `src/graphics/vulkan`. `Pass.Present` keeps the existing
  CPU-testable fullscreen-triangle finalization contract — backend-
  native swapchain copy/blit paths are rejected as the contract form
  because graphics cannot guarantee identical source/backbuffer
  formats or a `TRANSFER_DST_OPTIMAL` swapchain layout without owning
  swapchain state; a backend may opt into a copy/blit fast-path
  internally only when it can prove identical formats and a compatible
  source layout, and that decision never alters the `Pass.Present`
  command contract or the frame-recipe `Present` declaration. Platform
  (`src/platform/`) owns window creation/destroy, the window-event
  pump, and DPI/display reporting; backend (`src/graphics/vulkan`)
  owns surface/swapchain lifecycle and acquire/present timing through
  `IDevice::BeginFrame`/`Present`/`Resize` per `GRAPHICS-018`; runtime
  (`src/runtime/`) owns composition (event pump, `IRenderer::BeginFrame`/
  `EndFrame` bracketing, post-`EndFrame` `IDevice::Present(frame)`,
  resize forwarding); graphics owns only the backbuffer-import
  declaration, the `Pass.Present` command contract, and render-graph
  rejection of non-present writes to the imported backbuffer.
- The promoted GPU-driven path should use a canonical instance-slot space shared by renderable records, transform records, bounds/culling records, material references, picking IDs, and draw buckets.
- `GpuWorld` owns retained managed vertex/index buffer ranges for uploaded geometry.
  Managed-buffer compaction is explicit and opt-in: callers first request a
  `PlanManagedBufferCompaction()` result, then may pass that exact generation-
  checked plan to `ApplyManagedBufferCompaction()`. The relocation table reports
  old/new geometry byte offsets and shader-visible vertex/index units so runtime
  sidecars can refresh extracted caches without graphics importing runtime or
  live ECS ownership. Compaction is skipped when disabled or below threshold and
  is blocked by default while deferred frees are still pending for frames in
  flight.
- Heavy CPU scene data lives in the owning subsystem or runtime extraction pools; canonical ECS components keep source data/IDs, not graphics backend resources.

## ECS renderable residency bridge

`GRAPHICS-028` records the planning contract for turning live ECS renderable
entities into `GpuWorld` instance and geometry records. The bridge owner is
`runtime`: `Extrinsic.Runtime.RenderExtraction` is allowed to query live ECS,
read CPU-only `AssetInstance::Source`, `GeometrySources::*`, hierarchy, transform,
and dirty-tag components, and maintain an entity-keyed residency sidecar/cache.
That cache may store graphics-owned value types such as
`Graphics::Components::GpuSceneSlot`, `GpuInstanceHandle`, material instances, and
asset-binding metadata (`GpuSceneSlot::SourceAsset` and
`GpuSceneSlot::LastSeenAssetGeneration`), but those values remain outside
canonical ECS components and outside the live `entt::registry` as GPU-typed ECS
state. `GpuSceneSlot::EvaluateSourceAssetRebind()` compares the sidecar's stored
asset binding against an observed `(Assets::AssetId, generation)` supplied by
runtime; it does not import `Graphics.GpuAssetCache` or query live asset state.
`GRAPHICS-023C` adds the runtime-owned observation bridge: render extraction may
read `AssetInstance::Source`, query a supplied `Graphics.GpuAssetCache`, and
report whether a future rebind is required, but it does not mark newer
generations as last-seen until a later upload/rebind slice actually performs the
binding work. `GRAPHICS-023D` closes that loop on the runtime side only by
adding `Runtime::AcknowledgeRenderableAssetRebind(slot, observation)`, an
explicit caller-driven helper that advances `GpuSceneSlot::LastSeenAssetGeneration`
to the observed generation when the asset identity matches; it does not
auto-acknowledge inside `RenderExtractionCache::ExtractAndSubmit`, perform
uploads, watch files, recompile shaders, or reload texture residency. Graphics
render passes receive only renderer-submitted snapshots/views and must not
query ECS or runtime sidecar storage directly.

Static-vs-dynamic geometry residency is decided per stream. Immutable, shared
asset geometry flows from the CPU asset identifier on `AssetInstance::Source`
through runtime normalization to `Assets::AssetId`, then through
`Graphics.GpuAssetCache` and `GpuWorld::UploadGeometry()`. Dynamic per-entity
streams are owned by runtime residency policy and may use `GpuSceneSlot` named
buffers or a future `GpuWorld` successor for per-entity updates. Per-instance
state such as transforms, render flags, bounds, and material slots continues to
flow through runtime-submitted transform/material records and `GpuWorld` instance
SSBO updates.

Dirty tags in ECS remain CPU-only semantic markers. Editing systems may mark
`DirtyTransform`, `DirtyVertexPositions`, `DirtyVertexAttributes`, topology tags,
or the `GpuDirty` escape hatch, but they must not encode renderer buffer names,
`RHI::BufferHandle` values, bindless indices, or `GpuSceneSlot` references. The
runtime bridge consumes tags in dependency order, maps CPU property/channel
changes to packed upload streams or named dynamic buffers, and clears only the
tags it has consumed. Mesh, graph, point-cloud, and primitive domains share a
uniform `GpuWorld::GeometryUploadDesc` target so `GpuWorld` remains
domain-agnostic.

Hierarchy and primitive policy are also runtime-owned. Only renderable leaves
with geometry sources or asset sources materialize `GpuInstanceHandle` entries;
root/interior hierarchy nodes propagate transforms, visibility, and editor policy
to leaves before extraction. Authored primitive entities default to regular
`GpuWorld` instances that reference shared unit geometry so they participate in
culling, sorting, and picking. High-volume transient debug primitives may instead
be collected into per-frame instanced debug batches following the existing
transient debug packet pattern, without consuming retained `GpuWorld` instance
slots. Implementation of the full bridge remains a follow-up to the completed
planning task; the current promoted runtime extraction cache already owns the
partial sidecar for render hints, instance allocation/free, transform/light/
visualization submission, and consumed `DirtyTransform` clearing.

## Procedural-source residency bridge

`GRAPHICS-030` records the planning contract for the procedural-geometry first
slice of the GRAPHICS-028 residency bridge. The descriptor surface is closed:
`ProceduralGeometryKind` (initial enumerator `Triangle`) plus a small POD
`ProceduralGeometryParams` (vertex/index counts plus a fixed-size float
payload), exported from a planned module `Extrinsic.Runtime.ProceduralGeometry`
at `src/runtime/Runtime.ProceduralGeometry.cppm`. Two sources are the same
allocation iff `ProceduralGeometryKey = (Kind, Hash(Params))` is equal; debug
name, source entity ID, owning provider identity, frame index, and
`MetaData::EntityName` are explicitly excluded from the key so N entities
sharing the same key share one `Graphics::GpuGeometryHandle`.

The cache `Runtime::ProceduralGeometryCache` is a value type owned as a
member of `Runtime::RenderExtractionCache`, lifetime tied to the existing
extraction tick. `EnsureResident(key, params)` either inserts an entry and
runs the per-kind packer + `GpuWorld::UploadGeometry()` once, or hits an
existing entry and increments its `std::uint32_t` refcount. `Release(key)`
decrements; on transition to zero the entry is moved to a deferred-retire
list and `GpuWorld::FreeGeometry()` is issued only after `framesInFlight`
ticks have elapsed, mirroring the deferred-free contract that
`Graphics::GpuAssetCache::Tick(currentFrame, framesInFlight)` enforces for
asset leases. Per-kind packers live in a sibling module
`Extrinsic.Runtime.ProceduralGeometryPacker` whose `Pack(kind, params,
scratch) → std::optional<GeometryUploadDesc>` consumes a runtime-owned
scratch buffer (packed vertex bytes + surface/line index vectors) reused
across ticks. `GpuWorld` stays domain-agnostic and never imports
`Extrinsic.Runtime.ProceduralGeometry*`.

Procedural sources do **not** participate in `Graphics::GpuAssetCache`
generation tracking. `GpuSceneSlot::SourceAsset` is left at the default
`Assets::AssetId{}` (`HasSourceAsset()` returns `false`); the GRAPHICS-023C
`Runtime::ObserveRenderableAssetGeneration` helper already short-circuits in
that case and reports `SourceAssetCacheUnavailableCount` /
`SourceAssetViewUnavailableCount` instead of `RebindRequired`. No new
sentinel value or `IsProcedural` flag is added to `GpuSceneSlot`. The
procedural-source link on the entity is a CPU-only ECS component
`ECS::Components::ProceduralGeometryRef { Kind, Params }` at
`src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm`, importing
`Extrinsic.Core.*` only — no graphics, RHI, asset, or runtime imports — so
the GRAPHICS-028 prohibition on GPU-typed ECS components and the AGENTS.md
§2 `ecs → core` invariant both hold.

Lifecycle ordering inside `RenderExtractionCache::ExtractAndSubmit()` is
locked: detect candidates carrying `ProceduralGeometryRef`, `EnsureResident`
the key, `AllocateInstance` if no sidecar exists, refresh `GpuSceneSlot`,
`GpuWorld::SetInstanceGeometry()` once per `(instance, geometry)` pair, then
consume `DirtyTransform` → `SetInstanceTransform()` and clear the tag.
Failures are fail-closed and never throw: missing packer, upload failure,
allocate-instance failure, bind rejection, refcount saturation, invalid
params, and procedural+asset conflict each increment a dedicated counter on
`RuntimeRenderExtractionStats` (`ProceduralGeometryUploads`,
`ProceduralGeometryReuseHits`, `ProceduralGeometryReleases`,
`ProceduralGeometryFreeRetires`, `ProceduralGeometryFailedUploads`,
`ProceduralGeometryFailedInstanceAlloc`, `ProceduralGeometryFailedBinds`,
`ProceduralGeometryMissingPacker`, `ProceduralGeometryInvalidParams`,
`ProceduralGeometryRefCountSaturated`, `ProceduralAndAssetSourceConflict`)
and skip the entity for the tick. Steady-state cost is O(1) per renderable;
already-resident geometry incurs one `unordered_map` lookup and a refcount
increment with no scratch-buffer touch and no packer invocation. Adding a
new primitive (`Cube`, `Quad`, `Sphere`, `LineStrip`) requires only enum
extension + per-kind packer + a contract test, with no cache or extraction
lifecycle changes. Asset-backed mesh residency (the `AssetInstance::Source`
→ `GpuAssetCache` → `GpuWorld` path) and mixed-source entities remain
deferred to GRAPHICS-034. Implementation children GRAPHICS-030-Impl-A
(component + cache + Triangle packer), Impl-B (extraction wiring + bind
test), Impl-C (refcount/free + retire-queue test), and the optional Impl-D
(second packer) are identified but not opened.

## Reference scene bootstrap

`GRAPHICS-029` records the planning contract for the runtime-owned, opt-in
reference scene that produces at least one renderable candidate observable by
`Runtime.RenderExtraction`. The bootstrap owner is `runtime`: a planned module
`Extrinsic.Runtime.ReferenceScene` (source `src/runtime/Runtime.ReferenceScene.cppm`)
exposes an `IReferenceSceneProvider` interface plus a `ReferenceSceneRegistry`
container; `Engine::Initialize()` resolves a single
`EngineConfig::ReferenceScene::Selector` value (default
`ReferenceSceneSelector::Triangle`) and invokes the matching provider after the
scene registry is constructed but before the first frame. A
`bool m_ReferenceSceneInstalled` guard rejects double-install; teardown runs in
`Engine::Shutdown()` before scene tear-down. `Sandbox::main.cpp` does not flip the
flag — `CreateReferenceEngineConfig()` flips
`EngineConfig::ReferenceScene::Enabled = true` so sandbox stays policy-light per
the app/runtime boundary. The same helper also sets
`Render.EnablePromotedVulkanDevice = true` (GRAPHICS-080) so reference
sandbox launches request the promoted Vulkan backend; the resolved device
remains governed by the GRAPHICS-033 truth table in
`src/graphics/vulkan/README.md`.

The first provider, `TriangleProvider`, creates exactly one entity through the
HARDEN-060 `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")` API. The
entity carries `MetaData{ EntityName = "ReferenceTriangle" }`,
`Transform::Component{}` (identity), `Transform::WorldMatrix{}` (identity),
`Hierarchy::Component{}` (no parent/children), exactly one
`Graphics::Components::RenderSurface{ Domain = SourceDomain::Vertex }` render
hint, and — once GRAPHICS-030-Impl-A lands — the CPU-only
`ECS::Components::ProceduralGeometryRef{ Kind = ProceduralGeometryKind::Triangle }`
linking it to the procedural-source residency bridge. No GPU-typed value type
(`GpuSceneSlot`, `GpuInstanceHandle`, `RHI::*Handle`, bindless index, material
lease) is attached to the entity; residency state stays in the runtime extraction
sidecar per GRAPHICS-028. No light is created; the GRAPHICS-031 default debug
surface material is unlit by contract, so the first milestone composes through
the null renderer without lighting work.

Camera authorship is forward-compatible with GRAPHICS-017Q `CameraControllers`.
The provider returns an optional `Graphics::CameraViewInput` value (default
position `(0, 0, 3)` looking at the origin, 45° vertical FoV, near 0.1, far
100, aspect from `RenderFrameInput::Viewport`); `Engine::BuildRenderFrameInput()`
substitutes that value into `RenderFrameInput::Camera` while no controller is
wired. When `CameraControllers` opens, the seed value becomes its starting
state with no contract change. Determinism for tests is achieved at the
content layer through `MetaData::EntityName` and the provider-returned entity
span, not through `entt::entity` IDs (entt does not promise allocator
determinism across registries).

The reference scene module imports only ECS scene/component modules,
`Graphics.Component.RenderGeometry` (CPU-only render-hint value types already
imported by `Runtime.RenderExtraction`), `Graphics.CameraSnapshots` for
`CameraViewInput`, and `Core.Config.Engine`. It does not import
`Extrinsic.Graphics.GpuWorld`, `Extrinsic.Graphics.Renderer`,
`Extrinsic.Graphics.GpuAssetCache`, any `Extrinsic.RHI.*`,
`Extrinsic.Asset.*`, or `Extrinsic.Platform.*`, so the AGENTS.md §2 layering
invariants and the GRAPHICS-028 residency-bridge contract both hold.
Implementation children GRAPHICS-029-Impl-A (skeleton + config plumbing),
Impl-B (`TriangleProvider` + camera seed + contract test), and the optional
Impl-C (additional providers gated on GRAPHICS-030/034) are identified but
not opened.

## Graphics asset residency

- `src/graphics/assets/Graphics.GpuAssetCache.cppm` maps promoted
  `Assets::AssetId` values to graphics-owned GPU buffer/texture leases. It uses
  only Asset Registry identity types plus RHI managers; runtime remains
  responsible for translating asset events into `Reserve`, `RequestUpload`,
  `NotifyFailed`, `NotifyReloaded`, and `NotifyDestroyed` calls.
- Texture uploads use `GpuTextureRequest` (`AssetId`, CPU bytes, `TextureDesc`,
  and either an externally owned sampler handle or a sampler descriptor owned
  through `RHI::SamplerManager`). Ready texture views expose texture handle,
  bindless index, sampler handle, generation, and kind.
- Missing, pending, or failed texture assets resolve deterministically through
  `InitializeFallbackTexture()` plus `GetViewOrFallback()`. The resolved view
  records whether fallback was used and why (`Missing`, `Pending`, or `Failed`),
  keeping material/pass code CPU-testable without Vulkan.
- The current cache is explicitly non-evicting. Hot reloads keep old leases alive
  through a frame-anchored retire queue so immutable renderer snapshots can keep
  using old views for at least `framesInFlight` frames. Future capacity/eviction
  policy must be a separate semantic task.
- `GpuAssetCacheDiagnostics` reports upload requests, texture upload requests,
  texture/sampler allocation failures, fallback hits/misses, tracked assets,
  pending retire records, fallback readiness, and the non-eviction policy.

Per `GRAPHICS-015Q`, the texture residency backend follow-ups resolve as
follows. The cache stays explicitly non-evicting in the `GRAPHICS-015`
contract slice; capacity introspection happens through the existing
`GpuAssetCacheDiagnostics` fields (`TrackedAssets`, `PendingRetireRecords`,
`NonEvictingCache = true`), and any future bounded-eviction work is a
separate semantic task that must extend the existing diagnostics, route
evicted leases through the same frame-anchored retire queue with
`retireDeadline = currentFrame + framesInFlight` (mirroring the existing
`NotifyReloaded` retirement so renderer snapshots remain valid for at
least `framesInFlight` frames after eviction), refuse to evict the
fallback texture lease, and prefer a priority + LRU pair over pure LRU
so runtime/editor can pin critical material textures. Streaming mip /
reupload behavior reuses `RHI::TextureManager::Reupload()` to preserve
the existing `RHI::TextureHandle`, bindless index, and sampler binding
of the lease for partial-mip updates whose destination `TextureDesc`
(extent, format, mip count, usage) is unchanged; full lease replacement
through `RequestUpload(GpuTextureRequest)` is reserved for format /
extent / mip-count / usage changes and hot-reload swaps to a distinct
asset version, and a future `RequestStreamingReupload(AssetId,
MipRange, std::span<const std::byte>)` seam will validate the lease is
`Ready` and forward to `TextureManager::Reupload()` while incrementing
a `StreamingMipUploads` counter on `GpuAssetCacheDiagnostics`. A
single deterministic 4x4 magenta-and-black checkerboard fallback
texture (RGBA8_UNORM, alpha 0xFF, nearest filter, clamp-to-edge
addressing) covers every sampled material texture slot (`Albedo`,
`Normal`, `MetallicRoughness`, `Emissive`); per-channel "neutral"
interpretation is enforced by material shader code that observes the
resolved `UsedFallback` bit (`Normal` reverts to a flat `(0.5, 0.5,
1.0)` tangent normal, `MetallicRoughness` reverts to per-material
`MetallicFactor`/`RoughnessFactor` scalars treated as `metallic = 0`,
`roughness = 1` when factors are absent, and `Emissive` is multiplied
by per-material `EmissiveFactor` defaulting to `0.0` so unbound
emissive assets do not silently glow). Visualization and Htex/UV bake
atlas references do not use the magenta fallback: per `GRAPHICS-014Q`,
visualization atlas descriptors with deferred residency are dropped
from `RenderWorld::Visualization` and counted in
`VisualizationDiagnostics::TextureResidencyDeferredCount`. If
`InitializeFallbackTexture()` itself fails, the cache reports
`FallbackTextureReady = false` and `GetViewOrFallback()` returns
`GpuAssetFallbackReason::Unavailable` so material code can fall back
to factor-only shading deterministically. Bindless texture descriptor
writes are coalesced per frame: the backend records all bindless slot
writes produced during the frame's `IRenderer::PrepareFrame`/`Record`
window and drains them as a single descriptor batch at the start of
the next frame's `BeginFrame()`, mirroring the `Picking.Readback`
drain pattern from `GRAPHICS-012Q` and the histogram readback drain
from `GRAPHICS-013AQ`. Sampler creation is deduplicated through
`RHI::SamplerManager`; sampler changes detected through a different
`SamplerDesc` on the next `RequestUpload` trigger a coalesced
bindless re-write of the lease's descriptor in the same per-frame
batch and increment a `BindlessDescriptorRewrites` counter on
`GpuAssetCacheDiagnostics`. Material slot updates that swap an
`AssetId` flow through `MaterialSystem::ResolveTextureAssetBindings()`
and write the resolved `BindlessIndex` into `MaterialParams` without
forcing a separate descriptor flush, because bindless indices are
retained-stable per lease for the lease's `Ready` lifetime;
stale-bindless-index hazards on hot reload are prevented by the
existing frame-anchored retire queue holding the descriptor live for
`framesInFlight` frames after retirement. Concrete `VkDescriptorSet`
layout and heap write batching remain backend-local under
`src/graphics/vulkan`. Runtime owns both fallback initialization and
upload scheduling: `Runtime.Engine` (or a runtime-side graphics-
bootstrap step) calls `cache.InitializeFallbackTexture(fallbackDesc)`
exactly once after the cache is constructed and before any runtime
asset bridge issues `RequestUpload(GpuTextureRequest)`, sourcing the
fallback bytes from a baked engine resource owned by the runtime
layer (compiled-in byte array or runtime-loaded engine asset, not a
graphics-layer file read); the cache only consumes the
`std::span<const std::byte>`. Texture-typed asset bridges (planned
umbrella module `Extrinsic.Runtime.AssetBridges.Texture`, mirroring
the `Extrinsic.Runtime.SpatialDebugAdapters` pattern from
`GRAPHICS-011Q` and the `Extrinsic.Runtime.VisualizationAdapters`
pattern from `GRAPHICS-014Q`) subscribe to texture-typed
`AssetEvent::Ready` events on `AssetService::SubscribeAll`, read the
decoded CPU payload, construct a `GpuTextureRequest`, and call
`cache.RequestUpload(req)` synchronously from the asset-event handler
thread; heavy CPU decoding may be queued through
`Extrinsic.Runtime.StreamingExecutor`, but the final `RequestUpload`
call is always synchronous from runtime, and graphics never imports
`AssetService` or `AssetEventBus`. `AssetEvent::Destroyed` flows to
`cache.NotifyDestroyed(id)` which queues live leases for retirement;
graphics never schedules CPU work, never reads priority data, and
never owns asset event subscription.

## Pipeline and shader registry contract

- `Extrinsic.RHI.PipelineRegistry` is the promoted CPU-testable cache layer for
  deterministic shader/pipeline identities. It builds `PipelineKey` values from
  shader paths, shader generations, and the RHI `PipelineDesc` render state;
  matching keys return the same graphics-owned pipeline handle without requiring
  Vulkan shader compilation in the default CPU gate.
- Shader reload is represented as explicit invalidation by shader path. The
  registry drops affected cached leases and reports reload invalidation counts;
  callers request a new key with an updated shader generation to recreate the
  pipeline through `RHI::PipelineManager`.
- Missing shader IDs, key/descriptor mismatches, and backend pipeline creation
  failures are deterministic diagnostics. Backend-specific shader compilation
  remains behind RHI/backend integration and stays opt-in for GPU/Vulkan tests.

## Material registry and slot contract

- `Extrinsic.Graphics.MaterialSystem` owns promoted material-slot allocation in
  the renderer layer. Runtime extraction may maintain sidecar mappings from ECS
  entities or material asset IDs to `MaterialSystem` leases/slots, but canonical
  ECS components must not store graphics-owned material-slot indices.
- Slot `0` is the immutable fallback/default material slot
  (`kDefaultMaterialSlotIndex`). Stale or invalid material handles resolve to
  that fallback and increment deterministic CPU-visible diagnostics.
- Per
  [`GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md),
  slot 0 is registered as `"Material.DefaultDebugSurface"` with
  `MaterialTypeID = kMaterialTypeID_DefaultDebugSurface = 2u`,
  `MaterialFlags::Unlit`, and a deterministic non-black `BaseColorFactor`
  (`{0.55, 0.20, 0.85, 1.0}`); the slot is pre-populated by
  `MaterialSystem::Initialize()` and republished byte-identical by
  `MaterialSystem::RebuildGpuResources()`. The shader pair lives at
  `assets/shaders/forward/default_debug_surface.vert/frag` and consumes the
  canonical `GpuScenePushConstants` scene-table BDA + the existing
  `MaterialBuffer` SSBO at `set = 3, binding = 0`; no per-material descriptor
  set is added. The vertex format is position `vec3` + optional packed RGBA8
  vertex color `uint32`, matching the `Triangle` packer planned in
  GRAPHICS-030-Impl-A. The forward graphics pipeline is created once at
  renderer init with `CullMode = Back`, `DepthCompareOp = Less` (or `Equal`
  when running after `Pass.DepthPrepass`), `BlendEnabled = false`,
  `PolygonMode = Fill`, `PrimitiveTopology = TriangleList`,
  `MSAA samples = 1`, and dynamic state `{Viewport, Scissor}`. The default
  debug surface lane consumes the existing `SurfaceOpaque` cull bucket; no
  new bucket, no new pass, no new descriptor set is introduced.
- Per
  [`GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md),
  the missing-material fallback policy is graphics-owned at snapshot
  consumption time: when the renderer copies a runtime-submitted snapshot
  record whose resolved material slot is unset or out-of-range, it
  substitutes `kDefaultMaterialSlotIndex` and increments one of three
  additive `MaterialSystemDiagnostics` counters —
  `MissingMaterialFallbackCount` (sentinel-unset authoring),
  `InvalidMaterialSlotCount` (out-of-range/stale slot integers), and
  `DefaultDebugSurfaceUses` (total per-frame uses of slot 0 after
  substitution). The substitution lives at the same renderer span-copy step
  that already drains `InvalidSnapshotRecordCount`, so runtime stays
  agnostic of graphics-side slot identity. Runtime authoring may also
  request the default explicitly through a sentinel "unset material"
  description; the renderer-side substitution and counter increments are
  identical. There are no silent-skip fallback paths, and
  `MaterialSystemDiagnostics::FallbackSlotResolveCount` continues to track
  the separate stale-handle resolution path inside `GetMaterialSlot()`.
- Follow-up debug-material variants (`Wireframe`, `Line`, `Point`, `Normals`,
  `UVs`, `Depth`, `InstanceId`) attach as additional `MaterialTypeDesc`
  registrations and additional well-known slot constants under the naming
  family `Material.DefaultDebug<Variant>` /
  `kDefaultDebug<Variant>MaterialSlotIndex`, sharing the same descriptor
  layout family; they are identified but not opened by GRAPHICS-031.
- The canonical material SSBO layout is versioned by
  `kMaterialLayoutVersion == 1` and described by
  `GetCanonicalMaterialLayoutContract()`: one 128-byte `RHI::GpuMaterialSlot`
  per material, four custom `vec4` slots, and four texture/bindless references
  (`Albedo`, `Normal`, `MetallicRoughness`, `Emissive`).
- Texture references remain `RHI::BindlessIndex` values in `MaterialParams` for
  this contract. `MaterialTextureAssetBindings` carries data-only `AssetId`
  texture slots (`Albedo`, `Normal`, `MetallicRoughness`, `Emissive`), and
  `MaterialSystem::ResolveTextureAssetBindings()` resolves them through
  `GpuAssetCache` into bindless material params. Missing, pending, or failed
  texture assets can resolve to the cache fallback texture; unavailable
  fallbacks are deterministic diagnostics. Runtime owns the asset-event and
  material-authoring sidecars, not renderer passes or live asset-service traffic.
- Material type registration rejects duplicate names and incompatible layouts
  (for example more custom parameters than the four shader-visible custom data
  slots). Dirty material updates are coalesced before upload and reported through
  `MaterialSystemDiagnostics` so CPU-only tests can cover fallback, layout,
  texture asset binding, and update behavior without Vulkan.

## Related references

- Frame graph details: [frame-graph.md](frame-graph.md).
- Historical migration docs: `legacy-rendering-architecture-migration.md`, `gpu-driven-modular-rendering-pipeline-plan.md`.
