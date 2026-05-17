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
- `Extrinsic.Graphics.ImGuiOverlaySystem` and `Extrinsic.Graphics.Pass.Present` own the backend-agnostic ImGui overlay summary contract and the imported backbuffer finalization shim. Graphics never imports `imgui.h` or platform/swapchain types; runtime/editor translates `ImDrawData` into `ImGuiOverlayFrame` and runtime composition owns frame bracketing + `IDevice::Present(frame)`. See [ADR-0012 — ImGui overlay submission and `Pass.Present` finalization](../adr/0012-imgui-overlay-and-present-finalization.md) for the `SubmitFrame`/`ClearFrame` timing, the transient overlay buffer ring + retained font atlas + bindless user textures + single-pipeline binding rule, the rejection of swapchain copy/blit as the contract finalization form (fullscreen-triangle stays the CPU-testable shape), and the platform/backend/runtime/graphics boundary table.
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

The bridge owner is `runtime`: `Extrinsic.Runtime.RenderExtraction` queries live ECS, maintains an entity-keyed residency sidecar that stores graphics-owned value types (`GpuSceneSlot`, `GpuInstanceHandle`, material instances, asset-binding metadata) outside canonical ECS, and submits per-frame transform/material/visualization records through `IRenderer::SubmitRuntimeSnapshots()`. Graphics render passes receive only renderer-submitted snapshots/views and must not query ECS or runtime sidecar storage directly. See [ADR-0013 — ECS renderable residency bridge](../adr/0013-ecs-renderable-residency-bridge.md) for the four-step asset-rebind observation/acknowledgment loop (GRAPHICS-023A/B/C/D), the static-vs-dynamic stream split, the dirty-tag CPU-only invariant, the hierarchy/primitive policy (only renderable leaves materialize `GpuInstanceHandle`s), and the remaining-implementation status.

## Procedural-source residency bridge

See [ADR-0014 — Procedural-source residency bridge](../adr/0014-procedural-source-residency-bridge.md) for the closed `Extrinsic.Runtime.ProceduralGeometry` descriptor surface, the `Runtime::ProceduralGeometryCache` shape (content-addressed `(Kind, Hash(Params))` key, `EnsureResident`/`Release` refcount, deferred-retire queue mirroring `GpuAssetCache::Tick`), the CPU-only `ECS::Components::ProceduralGeometryRef` link with `ecs → core` enforcement, the locked `RenderExtractionCache::ExtractAndSubmit()` ordering, the exhaustive fail-closed `RuntimeRenderExtractionStats` counter set, and the GRAPHICS-034 deferral for asset-backed mesh residency.

## Reference scene bootstrap

See [ADR-0015 — Runtime reference scene bootstrap](../adr/0015-reference-scene-bootstrap.md) for the runtime-owned, opt-in `Extrinsic.Runtime.ReferenceScene` module + `IReferenceSceneProvider` / `ReferenceSceneRegistry` shape, the `Engine::Initialize()` resolution of `EngineConfig::ReferenceScene::Selector` with the double-install guard, the `TriangleProvider` single-entity contract (no GPU-typed ECS state, no light, CPU-only `ProceduralGeometryRef`), the forward-compatible `CameraViewInput` seed for the future `CameraControllers` umbrella, the strict import allow-list, and the cross-link to [GRAPHICS-080](../../tasks/active/GRAPHICS-080-enable-promoted-vulkan-by-default.md) for the `Render.EnablePromotedVulkanDevice` flip.

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

See [ADR-0016 — Texture residency, fallback, and asset cache policy](../adr/0016-texture-residency-and-asset-cache-policy.md) for the GRAPHICS-015Q follow-ups: the explicitly non-evicting cache + future bounded-eviction constraints, the `RHI::TextureManager::Reupload()` streaming-mip preservation seam vs full-lease-replacement `RequestUpload`, the single 4x4 magenta-and-black fallback texture + per-channel shader-side neutrality contract, the `InitializeFallbackTexture()` failure behavior, the per-frame coalesced bindless descriptor write batch (mirroring `Picking.Readback` / histogram drain) + `RHI::SamplerManager` dedup + `BindlessDescriptorRewrites` counter, and the runtime ownership of fallback initialization, `Extrinsic.Runtime.AssetBridges.Texture` event subscription, and synchronous `RequestUpload`/`NotifyDestroyed` calls.

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
