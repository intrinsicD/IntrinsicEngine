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
- `IRenderer::RebuildOperationalResources()` is the backend-neutral reset/rebind seam for devices that were initialized fail-closed and later become operational. Runtime owns transition detection: once `IDevice::IsOperational()` changes from false to true, runtime waits for device idle, calls the renderer hook, then marks the renderer operational for subsequent frames. The renderer rebuilds material GPU buffers, `GpuWorld` buffers/scene-table bindings, culling output resources, and the depth-prepass pipeline through RHI managers only; it must not import or branch on Vulkan backend types. CPU/null paths remain valid because non-operational devices continue to compile/execute the graph while command bodies report `SkippedNonOperational`.
- `RHI::SamplerDesc` owns backend-neutral filtering, addressing, LOD, comparison, anisotropy, and `SamplerBorderColor` state. The default border color is opaque black for compatibility with earlier sampler creation paths; backends translate it internally (for example Vulkan maps it to `VkBorderColor`) without exposing API-native types through RHI or renderer code.
- The promoted Vulkan backend defines the same `IDevice` lifecycle/resource surface behind the RHI seam. Runtime selects it only when both gates are enabled: `RenderConfig::EnablePromotedVulkanDevice == true` and the build option `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` produces an `ExtrinsicBackendsVulkan` target. Otherwise `GraphicsBackend::Vulkan` still routes to the Null device fallback, preserving the default CPU/null correctness path. Until full Vulkan device/swapchain/resource bring-up lands, promoted Vulkan initialization fails closed by leaving the device non-operational and `BeginFrame()` returns `false`. Texture creation, synchronous texture upload, and sampler creation have guarded real Vulkan resource paths for future operational devices; pipeline creation still returns invalid handles, and bindless/transfer service accessors return no-op fallbacks instead of null state. Callers must not special-case Vulkan in renderer code. The operational-transition reset seam is now present and CPU-tested; a future Vulkan slice may only mark the backend operational after it also reconciles backend fallback bindless/transfer behavior and real pipeline/swapchain bring-up.

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
  Runtime/platform own camera motion, input polling, pick-request creation,
  gizmo hit testing, and transform application; graphics only consumes the
  resulting data snapshots.
- `Extrinsic.Graphics.SpatialDebugVisualizers` converts data-only spatial debug
  snapshots (bounds, hierarchy nodes, split planes, convex-hull wire edges, and
  point markers) into transient debug packets with deterministic limits and
  diagnostics. Geometry/runtime/editor adapters remain outside graphics and feed
  snapshot records instead of giving graphics live ownership of geometry trees or
  editor state.
- `Extrinsic.Graphics.VisualizationPackets` is the promoted data-only seam for
  scalar/color/vector attribute buffers, vector-field overlays, isoline overlays,
  UV-backed fragment-bake atlas descriptors, and Htex patch-preview/bake atlas
  descriptors. Existing mesh texcoords may be used for per-fragment bakes such
  as label/colormap visualization; Htex remains an always-recreatable alternate
  mapping for meshes without texcoords or when explicitly selected by the user.
  The packet layer validates domains, ranges, colormap IDs, buffer-address seams,
  missing texcoords/resources, and Htex recreation requests without importing
  geometry algorithms, ECS ownership, or texture residency. Htex/UV atlas texture
  allocation remains deferred to graphics asset/residency work.
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
