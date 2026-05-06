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
- The promoted Vulkan backend defines the same `IDevice` lifecycle/resource surface behind the RHI seam. Runtime selects it only when both gates are enabled: `RenderConfig::EnablePromotedVulkanDevice == true` and the build option `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` produces an `ExtrinsicBackendsVulkan` target. Otherwise `GraphicsBackend::Vulkan` still routes to the Null device fallback, preserving the default CPU/null correctness path. `VulkanDevice::Initialize()` now performs guarded Vulkan bootstrap/probing when given a native GLFW window: volk initialization, instance creation, surface creation, physical-device/queue-family/swapchain-support probing, required Vulkan 1.2/1.3 feature support and enablement for descriptor indexing, timeline semaphores, buffer device addresses, and dynamic rendering, logical-device creation, graphics/present/transfer queue acquisition, VMA allocator creation, per-frame command/sync resource allocation, backend-local swapchain image/image-view/`RHI::TextureHandle` registration, internal bindless heap / global pipeline-layout / transfer queue creation, and command-context rebinding. This remains backend-local and does not make Vulkan operational; `IsOperational()` is now backed by a conservative Vulkan-owned predicate that requires live logical-device/swapchain/per-frame/service state and still-false safety prerequisites for canonical renderer pass execution, synchronization/barrier validation, queue-family ownership handling where needed, and public service exposure. `GetVulkanBootstrapDiagnosticsSnapshot()` exposes backend-specific CPU diagnostics for the most recent bootstrap attempt, including required-feature support/enablement plus swapchain creation/image/view/handle counts and extent, without leaking Vulkan-native types through RHI or renderer code; `GetVulkanServiceDiagnosticsSnapshot()` similarly reports guarded internal service handoff, clean service-failure/skipped states, whether live operational prerequisites are present, whether safety prerequisites are cleared, and whether public services are exposed. Fail-closed and guarded direct `BeginFrame`, `EndFrame`, `Present`, and `Resize` attempts are observable through process-monotonic CPU diagnostics and `GetVulkanFrameLifecycleDiagnosticsSnapshot()`, which records backend-local structured lifecycle statuses populated from all real paths: fail-closed paths emit `SkippedNotOperational`/`SkippedNoSwapchain`/`SkippedNoSwapchainImages`; guarded direct paths emit `Acquired`/`Suboptimal`/`OutOfDate`/`FailedAcquire` for `BeginFrame`, `Submitted`/`FailedSubmit` for `EndFrame`, `Presented`/`Suboptimal`/`OutOfDate`/`FailedPresent` for `Present`, and `Recreated`/`FailedRecreate`/`RecordedPending*` for `Resize`; device-loss from any path sets `DeviceLost` and routes subsequent calls back through fail-closed skips. The snapshot also carries frame/image indices, requested resize extent, availability booleans, pending-resize/device-lost flags, and Vulkan result codes. Frame-loop fail-closed log breadcrumbs (`BeginFrame`, `EndFrame`, `Present`) are emitted only on the first fire to avoid log spam at 60 Hz; counters remain process-monotonic for diagnostic consumers regardless. After service-ready bootstrap, opt-in Vulkan smoke coverage can directly acquire a swapchain image, record an empty command buffer, submit it, present it, defer zero-extent resize, recreate the swapchain for a nonzero extent, create canonical scene-table/draw-bucket buffers with BDA, create sampled/depth/color attachment textures and samplers, upload a sampled texture subresource, and create both compute and depth-only dynamic-rendering graphics pipelines while `IsOperational()` remains false; renderer/runtime code still must branch on `IDevice::IsOperational()`, not Vulkan-specific diagnostics. The live internal `VulkanBindlessHeap` can resolve backend-owned RHI texture/sampler handles into descriptor writes through a Vulkan-local resolver, preserving texture/material descriptor readiness without exposing `Vk*` handles or live ECS knowledge outside `src/graphics/vulkan`. `assets/shaders/depth_prepass.vert` is the promoted depth-prepass shader source for opt-in SPIR-V smoke coverage. `GetVulkanPipelineDiagnosticsSnapshot()` reports pre-bring-up skips, invalid descriptions, shader-read/module failures, pipeline creation failures, and successful direct backend pipeline creation without exposing Vulkan-native handles. The internal transfer queue returns invalid `RHI::TransferToken` values with logger diagnostics instead of aborting on command-buffer, submit, semaphore-query, range, or staging failures; device-lost results from lifecycle, recreate, one-shot upload, and resource/pipeline creation paths return the backend to fail-closed state; and unbound/not-begun Vulkan command-context recording calls skip with `GetFallbackCommandRecordingAttemptCount()` diagnostics instead of issuing commands against null state. Public bindless/transfer service accessors are routed through the same predicate as `IDevice::IsOperational()` and return no-op fallbacks while non-operational even when live internal services exist, so RHI managers/renderer code remain gated by the backend-neutral seam. Callers must not special-case Vulkan in renderer code. The operational-transition reset seam is present and CPU-tested. `GRAPHICS-018` has completed all guarded backend bring-up paths, structured lifecycle diagnostics for all enum variants, rate-limited frame-loop breadcrumbs, and operational boundary documentation; the remaining blocker before `IsOperational()` can become true is canonical renderer pass command execution beyond `CullingPass`/`DepthPrepass` routing and public service fallback reconciliation.

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
- `Extrinsic.Graphics.SelectionSystem` is a CPU-visible reporting-only seam
  for picking. Selection ID passes write `EntityId` (`R32_UINT`, the stable
  extracted entity ID surfaced through `RenderableInstance`, value `0`
  reserved for "no hit") and `PrimitiveId` (`R32_UINT`, packed via
  `EncodedSelectionId` as `(domain << 28) | (payload & 0x0FFFFFFFu)` with
  authoritative face/edge/point payloads from the corresponding cull
  buckets). The renderer copies the requested pixel(s) into the
  graphics-owned host-visible `Picking.Readback` buffer at frame-record
  time and drains it on the next `BeginFrame()` after the issuing frame's
  fences complete: valid samples invoke
  `SelectionSystem::PublishPickResult(...)` and `EntityId == 0` /
  invalidated requests / deterministic readback failures invoke
  `PublishNoHit()`. Pending-pick consumption stays inside the renderer's
  frame-record path so `SelectionSystemDiagnostics` counts remain
  comparable across backends, and the CPU/null backend simulates the same
  drain without Vulkan-specific code so it remains the correctness gate.
  Runtime owns `StableEntityId` -> live ECS resolution, ECS selection /
  hover mutation, editor selection policy, and the selection-outline
  input mask consumed by `SelectionOutlinePass`; graphics never reads or
  mutates ECS state. Until
  [`GRAPHICS-025`](../../tasks/backlog/rendering/GRAPHICS-025-hybrid-transparent-special-material-path.md)
  introduces selectable transparent / special-forward sub-buckets, only
  `Selectable` opaque renderables flow through `SelectionSurface` /
  `SelectionLines` / `SelectionPoints`, and runtime extraction routes
  transparent picks through CPU pick fallback if editor policy requires
  them.
- `Extrinsic.Graphics.SpatialDebugVisualizers` converts data-only spatial debug
  snapshots (bounds, hierarchy nodes, split planes, convex-hull wire edges, and
  point markers) into transient debug packets with deterministic limits and
  diagnostics. Geometry/runtime/editor adapters remain outside graphics and feed
  snapshot records instead of giving graphics live ownership of geometry trees or
  editor state. Concrete adapters that translate `Geometry::BVH`,
  `Geometry::KDTree`, `Geometry::Octree`, and convex-hull outputs into these
  data-only records live in **runtime extraction** (planned umbrella module
  name `Extrinsic.Runtime.SpatialDebugAdapters`), not in `src/geometry` and not
  in `src/graphics`: geometry stays at the `geometry -> core` layer rule and
  graphics never imports geometry tree implementations or editor state. Editor
  and app code may own user-facing toggles (enable/disable, color, leaf/internal
  filters, per-depth filters) but must funnel them into the runtime adapter as
  pre-filter inputs rather than calling
  `Extrinsic.Graphics.SpatialDebugVisualizers` with live geometry references.
  The `SpatialDebugVisualizerOptions::MaxLinePackets`/`MaxPointPackets`/
  `MaxDepth` budget remains the single place that enforces graphics-side
  truncation, and `SpatialDebugVisualizerDiagnostics` remains the only
  graphics-visible diagnostic surface; adapters apply CPU-side pre-filters
  (e.g. leaf-only, occupancy-only, capped depth) and report adapter-side
  invocation/filter statistics through runtime extraction stats rather than
  introducing a parallel graphics diagnostics struct. Adapter tests are
  runtime integration tests under `tests/integration/runtime/` (matching
  `Test.RuntimeRenderExtraction.cpp`); the data-only packet contract keeps
  its unit coverage under `tests/unit/graphics/`.
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
- `Extrinsic.Graphics.PostProcessSystem` owns the backend-agnostic
  HDR-to-LDR chain. `SceneColorHDR`, `SceneColorLDR`,
  `PostProcess.BloomScratch`, `PostProcess.Histogram`, and
  `PostProcess.AATemp` are frame-recipe transient resources owned by
  the framegraph; postprocess passes obtain their RHI handles through
  the frame-recipe resource map rather than through static descriptor
  sets. Per `GRAPHICS-013AQ`, `PostProcessSystem` is the sole owner of
  the small set of **retained** postprocess resources: the SMAA
  `AreaTex` (`R8G8_UNORM`, 160x560) and `SearchTex` (`R8_UNORM`,
  256x33) lookup textures and the exposure-adaptation history buffer
  (`{previous_average_log_lum, adaptation_velocity, frame_index}`).
  These retained resources are allocated once at
  `PostProcessSystem::Initialize()` through
  `RHI::TextureManager`/`RHI::BufferManager`, freed at `Shutdown()`,
  and imported by the bloom/histogram/SMAA passes as retained
  resources when the corresponding settings are enabled. Bloom uses a
  single progressive down-/up-sample pyramid built into the one
  frame-transient `PostProcess.BloomScratch` mip-chain texture (per-
  mip subviews, not one scratch texture per mip), the histogram stage
  uses a fixed 256-bin layout over a fixed log2-luminance range of
  `[-10, +10]` stops, and histogram diagnostics readback follows the
  same drain pattern as `Picking.Readback` (host-visible staging copy
  recorded at frame-record time, drained on the next `BeginFrame()`
  after the issuing frame's fences complete; the CPU/null backend
  simulates the same drain without Vulkan-specific code). FXAA and
  SMAA remain mutually exclusive per `PostProcessSettings::AntiAliasing`;
  SMAA edge and blend intermediates fold under the existing
  `PostProcess.AATemp` frame-recipe resource as two named
  subresources (`AATemp.Edges` `R8G8_UNORM`, `AATemp.Weights`
  `R8G8B8A8_UNORM`). Quality presets are encoded into the existing
  `PostProcessPushConstants::StageKind` packing space rather than
  expanding the push-constant struct, and concrete
  `VkDescriptorSetLayout` bindings remain backend-local under
  `src/graphics/vulkan` and never leak through RHI or renderer code.
- `Extrinsic.Graphics.DebugViewSystem` owns the backend-agnostic
  render-target inspection and resource-selection seam. It builds a
  deterministic inspection table from `FrameRecipeIntrospection`,
  classifies resources (texture, depth texture, buffer, backbuffer,
  alias, unknown), and resolves the requested `DebugViewSettings::RequestedResourceName`
  with structured fallback diagnostics. Per `GRAPHICS-013BQ`, no
  retained graphics-owned debug-view textures or buffers exist;
  `DebugViewRGBA` is a frame-recipe transient owned by the framegraph,
  and the resolved sampled selection is bound through one pass-local
  `Pass.DebugView` descriptor set with exactly two bindings (sampled
  image view + linear-clamp sampler). Visualization mode is derived
  deterministically from `FrameRecipeResourceKind` plus
  `DebugViewResourceClass`, so `DebugViewSettings` does not gain a
  user-selectable visualization-mode field and
  `DebugViewPushConstants` keeps its existing four-`uint32` packing.
  Concrete `VkDescriptorSetLayout` definitions and per-aspect view
  creation (color view, depth-aspect-only view, integer-typed view for
  `R32_UINT` selection-ID and material-ID resources) remain
  backend-local under `src/graphics/vulkan` and never leak through RHI
  or renderer module surfaces. Runtime/editor code owns the dictionary
  that maps human-readable UI strings to canonical
  `FrameRecipeIntrospection::Resources[i].Name` keys using the rows
  exposed by `DebugViewSystem::BuildInspectionTable()`, then calls
  `DebugViewSystem::SetSettings(...)`; graphics never receives display
  strings and never imports ImGui or platform/window state. Buffer-class
  resources remain listed in the inspection table but stay
  non-previewable in `Pass.DebugView`; textual/statistical buffer
  inspection is deferred to a future runtime/editor visualization
  surface tracked under `GRAPHICS-014Q` that consumes existing
  per-owner diagnostics rather than adding a parallel buffer-readback
  API on `DebugViewSystem`.
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
