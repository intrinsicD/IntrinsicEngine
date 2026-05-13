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
- The promoted Vulkan backend defines the same `IDevice` lifecycle/resource surface behind the RHI seam. Runtime selects it only when both gates are enabled: `RenderConfig::EnablePromotedVulkanDevice == true` and the build option `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` produces an `ExtrinsicBackendsVulkan` target. Otherwise `GraphicsBackend::Vulkan` still routes to the Null device fallback, preserving the default CPU/null correctness path. `VulkanDevice::Initialize()` now performs guarded Vulkan bootstrap/probing when given a native GLFW window: volk initialization, instance creation, surface creation, physical-device/queue-family/swapchain-support probing, required Vulkan 1.2/1.3 feature support and enablement for descriptor indexing, timeline semaphores, buffer device addresses, and dynamic rendering, logical-device creation, graphics/present/transfer queue acquisition, VMA allocator creation, per-frame command/sync resource allocation, backend-local swapchain image/image-view/`RHI::TextureHandle` registration, internal bindless heap / global pipeline-layout / transfer queue creation, and command-context rebinding. This remains backend-local and does not make Vulkan operational; `IsOperational()` is now backed by a conservative Vulkan-owned predicate that requires live logical-device/swapchain/per-frame/service state and still-false safety prerequisites for canonical renderer pass execution, synchronization/barrier validation, queue-family ownership handling where needed, and public service exposure. `GetVulkanBootstrapDiagnosticsSnapshot()` exposes backend-specific CPU diagnostics for the most recent bootstrap attempt, including required-feature support/enablement plus swapchain creation/image/view/handle counts and extent, without leaking Vulkan-native types through RHI or renderer code; `GetVulkanServiceDiagnosticsSnapshot()` similarly reports guarded internal service handoff, clean service-failure/skipped states, whether live operational prerequisites are present, whether safety prerequisites are cleared, and whether public services are exposed. Fail-closed and guarded direct `BeginFrame`, `EndFrame`, `Present`, and `Resize` attempts are observable through process-monotonic CPU diagnostics and `GetVulkanFrameLifecycleDiagnosticsSnapshot()`, which records backend-local structured lifecycle statuses populated from all real paths: fail-closed paths emit `SkippedNotOperational`/`SkippedNoSwapchain`/`SkippedNoSwapchainImages`; guarded direct paths emit `Acquired`/`Suboptimal`/`OutOfDate`/`FailedAcquire` for `BeginFrame`, `Submitted`/`FailedSubmit` for `EndFrame`, `Presented`/`Suboptimal`/`OutOfDate`/`FailedPresent` for `Present`, and `Recreated`/`FailedRecreate`/`RecordedPending*` for `Resize`; device-loss from any path sets `DeviceLost` and routes subsequent calls back through fail-closed skips. The snapshot also carries frame/image indices, requested resize extent, availability booleans, pending-resize/device-lost flags, and Vulkan result codes. Frame-loop fail-closed log breadcrumbs (`BeginFrame`, `EndFrame`, `Present`) are emitted only on the first fire to avoid log spam at 60 Hz; counters remain process-monotonic for diagnostic consumers regardless. After service-ready bootstrap, opt-in Vulkan smoke coverage can directly acquire a swapchain image, record an empty command buffer, submit it, present it, defer zero-extent resize, recreate the swapchain for a nonzero extent, create canonical scene-table/draw-bucket buffers with BDA, create sampled/depth/color attachment textures and samplers, upload a sampled texture subresource, submit 2D multi-mip, 2D-array, and six-face cubemap full-chain uploads through the live transfer queue, and create both compute and depth-only dynamic-rendering graphics pipelines while `IsOperational()` remains false; renderer/runtime code still must branch on `IDevice::IsOperational()`, not Vulkan-specific diagnostics. The live internal `VulkanBindlessHeap` can resolve backend-owned RHI texture/sampler handles into descriptor writes through a Vulkan-local resolver, preserving texture/material descriptor readiness without exposing `Vk*` handles or live ECS knowledge outside `src/graphics/vulkan`. `assets/shaders/depth_prepass.vert` is the promoted depth-prepass shader source for opt-in SPIR-V smoke coverage. `GetVulkanPipelineDiagnosticsSnapshot()` reports pre-bring-up skips, invalid descriptions, shader-read/module failures, pipeline creation failures, and successful direct backend pipeline creation without exposing Vulkan-native handles. The internal transfer queue returns invalid `RHI::TransferToken` values with logger diagnostics instead of aborting on command-buffer, submit, semaphore-query, range, or staging failures; device-lost results from lifecycle, recreate, one-shot upload, and resource/pipeline creation paths return the backend to fail-closed state; and unbound/not-begun Vulkan command-context recording calls skip with `GetFallbackCommandRecordingAttemptCount()` diagnostics instead of issuing commands against null state. Public `GetBindlessHeap()` remains routed through `IDevice::IsOperational()` and returns the fallback heap while non-operational. Public `GetTransferQueue()` now returns the live Vulkan transfer queue once guarded live prerequisites are ready, even while `IsOperational()` remains false, so opt-in upload smoke and streaming seams can exercise async transfer without special-casing Vulkan; pre-bootstrap/non-service-ready states still return the fail-closed fallback queue. Callers must not special-case Vulkan in renderer code. The operational-transition reset seam is present and CPU-tested. `GRAPHICS-018` has completed all guarded backend bring-up paths, structured lifecycle diagnostics for all enum variants, rate-limited frame-loop breadcrumbs, and operational boundary documentation; the remaining blocker before `IsOperational()` can become true is canonical renderer pass command execution beyond `CullingPass`/`DepthPrepass` routing and public service fallback reconciliation. Per `GRAPHICS-018Q`, the four remaining Vulkan integration follow-ups resolve as follows. Texture upload policy keeps the guarded synchronous staging-buffer one-subresource `WriteTexture()` path as the fail-closed correctness baseline; runtime/streaming uploads must use `RHI::ITransferQueue` (the canonical seam declared by `GRAPHICS-026`) rather than the blocking graphics-queue helper, per-subresource layout tracking stays whole-image for the batched path, and multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke landed with `GRAPHICS-018T` rather than this clarification. Sampler anisotropy stays expressed through the existing `RHI::SamplerDesc::MaxAnisotropy` float; the Vulkan backend probes `VkPhysicalDeviceFeatures::samplerAnisotropy` during physical-device selection alongside the existing required Vulkan 1.2 / 1.3 features, enables it on the logical device when supported, records support / enablement on `GetVulkanBootstrapDiagnosticsSnapshot()`, and at sampler-creation time silently disables anisotropy when the feature is unsupported or `MaxAnisotropy <= 1.0`, otherwise clamps to `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)` with one warn breadcrumb when clamping reduces the value; missing support never fails sampler creation and no new RHI-visible enum or cap is added. Fallback reason taxonomy keeps each fail-closed counter and its reason enum 1:1 to its path: future device-loss / extension / feature-negotiation reasons in the pipeline path are appended to the existing `FallbackPipelineReason` enum, and any second reason in another counter introduces a *new* path-local `FallbackXxxReason` enum named after that counter with a matching `LastXxxReason` field appended to `FallbackDiagnosticsSnapshot` after the existing eight fields, so consumers never have to demultiplex a combined enum and counter process-monotonicity stays independent of any reason field. Per-call warn breadcrumbs on bindless / transfer-queue / pipeline-creation fallback paths remain canonical for now (those callsites fire infrequently while non-operational and the visibility helps catch accidental loops); frame-loop counters keep the existing once-per-fail-closed-cycle rate-limited breadcrumb policy with `FallbackDiagnosticsSnapshot` carrying the precise diagnostic regardless of breadcrumb suppression, and `Resize` stays unrate-limited. Migration of any per-call counter to once-per-frame rate-limited breadcrumbs (with a cumulative-skipped count appended) is a separate semantic task scoped to that counter only when operational bring-up demonstrates many-per-frame fallback firing.

## Vulkan operational readiness and runtime fallback

`GRAPHICS-033` records the operational-readiness contract that must be satisfied
before the promoted Vulkan backend can leave fail-closed mode. The ordered gate is
backend-owned and append-only: (1) build/runtime request reconciled, (2) instance,
surface, physical device, and queue families live, (3) logical device, required
features/extensions, allocator, and heap diagnostics live, (4) swapchain acquire /
present / resize policy satisfies the GRAPHICS-013CQ present contract, (5)
per-frame command/sync objects are live for the GRAPHICS-032 minimal recipe, (6)
minimal surface and present recording bodies match the CPU/null command contract,
(7) framegraph barrier/layout validation reports no GRAPHICS-022 hard errors, (8)
public bindless/transfer/pipeline/backbuffer/command-context services agree on the
same operational answer, and (9) validation-layer policy reports no gate-blocking
error.

The single source of truth is a planned Vulkan-public, non-native evaluator:
`EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) ->
VulkanOperationalStatus`. Status is reasoned rather than bool-only:
`VulkanOperationalStatusCode { NotCompiled, NotRequested,
RequestedButUnsupported, RequestedButFailedInit, RequestedButValidationFailed,
RequestedButIncompleteGate, Operational }`, with an append-only
`VulkanOperationalReason` for the first failing gate. `VulkanDevice::IsOperational()`
and runtime startup reconciliation consume that result; renderer code still gates
only on `RHI::IDevice::IsOperational()` and backend-neutral framegraph stats.

Runtime reconciliation is fail-soft: if Vulkan is requested but not compiled,
unsupported, failed initialization, validation-failed, or gate-incomplete, runtime
returns the Null device, increments the Vulkan operational fallback diagnostics,
emits one `VulkanRequestedButNotOperational` startup breadcrumb, and continues.
Runtime never aborts solely because requested Vulkan falls back to Null. Runtime
does not call `vk*` symbols or inspect native handles; it reads a future
`VulkanOperationalDiagnosticsSnapshot` at startup, after device reset/recreate,
and around false→true / true→false operational transitions.

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
  resulting data snapshots. Per `GRAPHICS-017Q`, the camera/gizmo runtime
  follow-ups resolve as follows. Concrete camera controllers (orbit, fly,
  free-look, top-down) live as runtime modules under the planned umbrella
  module name `Extrinsic.Runtime.CameraControllers`, mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern from `GRAPHICS-011Q`,
  the `Extrinsic.Runtime.VisualizationAdapters` pattern from
  `GRAPHICS-014Q`, and the `Extrinsic.Runtime.AssetBridges.Texture`
  pattern from `GRAPHICS-015Q`; controllers read platform input deltas
  through the existing platform input port and translate them into
  runtime-owned camera state, then runtime extraction fills
  `CameraViewInput` and submits it through
  `IRenderer::SubmitRuntimeSnapshots()`. Graphics never imports
  `src/platform/`, never polls window events, and never owns
  active-controller selection or camera fan-out across multiple views.
  Pick-request scheduling is runtime-owned and single-shot: each input
  frame's accepted picks (mouse-down, hover-pick, programmatic editor
  pick, async asset-pipeline pick) are coalesced at runtime by
  `(viewport, pixel, request_kind)` key into the per-frame
  `PickPixelRequest` span on `RenderFrameInput`, and the renderer
  drains the resulting `Picking.Readback` on the next `BeginFrame()`
  mirroring the drain pattern from `GRAPHICS-012Q`; valid samples invoke
  `SelectionSystem::PublishPickResult(...)` and `EntityId == 0` /
  invalidated requests / deterministic readback failures invoke
  `SelectionSystem::PublishNoHit()`. There is no graphics-side
  persistent pending-pick queue across frames and no graphics-side
  request-kind taxonomy. Transform-gizmo hit testing is
  runtime/editor-owned under the planned umbrella module
  `Extrinsic.Runtime.GizmoInteraction` (mirroring the same adapter
  pattern); the hit-test path reads selection authoring transforms
  from runtime ECS/editor state, the same
  `CameraViewSnapshot::ViewProjection`/`PickRay` derivation that
  graphics produces from `CameraViewInput`, and raw pointer pixels
  from the platform input port. Graphics never receives raw pointer
  coordinates and never imports any gizmo hit-test code path. The
  `TransformGizmoRenderPacket` spans on `RenderWorld` carry only
  render-relevant data — world-space gizmo origin, camera-relative
  scale, active mode (translate / rotate / scale), highlighted axis or
  plane mask, and per-handle render flags — while active drag state,
  axis lock, screen-space drag origin, snap thresholds, modifier-key
  state, multi-select pivot policy (centroid / individual /
  last-selected), and orientation reference frame (local / world /
  view) stay runtime-side. Reusing `PickPixelRequest` for gizmo handle
  picking is permitted but not required: editor policy may either use
  direct CPU ray-vs-gizmo intersection in runtime/editor (preferred to
  avoid a frame of pick-readback latency) or route handle picks
  through `PickPixelRequest` with a runtime-side request-kind tag, and
  either choice is invisible to graphics. Transform application is
  runtime/editor-owned: at drag-tick time the gizmo interaction module
  writes the next authoring transform into runtime ECS/asset/prefab
  storage; at drag-commit time it pushes a single undoable command
  onto the editor undo stack capturing
  `(entity, before, after)`. Undo / redo lives entirely in the editor
  and graphics never mutates ECS, asset, or prefab state. Legacy
  `Graphics.TransformGizmo` and `Graphics.Interaction` features
  (orientation modes, snap modes, multi-select pivot policy,
  modifier-key behavior, numeric-input commit, per-axis constraint
  locks) are enumerated by the editor-handoff rows in
  `../migration/nonlegacy-parity-matrix.md` that cross-link
  `GRAPHICS-017Q`; concrete promoted-implementation task IDs are
  deliberately not allocated by this clarification because the matrix
  already cross-links them and `GRAPHICS-020` (legacy graphics
  retirement gates) is the gating task that consumes the matrix. This
  clarification adds no new graphics fields, no new graphics
  diagnostics, and no graphics acceptance criteria beyond the existing
  `GRAPHICS-017` contract.
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
  allocation remains deferred to graphics asset/residency work. Per
  `GRAPHICS-014Q`, runtime extraction (`Extrinsic.Runtime.RenderExtraction`) is
  the sole owner of translating PropertySet attributes, KMeans labels, isoline
  results, vector fields, and Htex metadata into the `RuntimeRenderSnapshotBatch`
  visualization packet spans; concrete producer adapters live under a planned
  `Extrinsic.Runtime.VisualizationAdapters` umbrella mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern from `GRAPHICS-011Q`, and
  graphics never imports geometry algorithm modules or live ECS ownership.
  Editor/app code provides user-facing surfaces (selected attribute, colormap,
  range, isoline parameters, vector-field scale/color, Htex regeneration
  request) but funnels them into the runtime adapter as pre-filter inputs.
  Runtime/extraction performs no packet filtering; validation is centralized
  in graphics through `ValidateVisualizationPackets(...)` called by the
  renderer at snapshot extraction time, rejected records are dropped from the
  consumed `RenderWorld::Visualization` snapshot and counted in
  `VisualizationDiagnostics` (`MissingAttributeCount`, `DomainMismatchCount`,
  `InvalidRangeCount`, `UnsupportedColormapCount`, `InvalidResourceCount`,
  `MissingTexcoordCount`, `HtexRecreateRequestCount`,
  `TextureResidencyDeferredCount`, `HasErrors`), and future backend upload
  stages do not re-validate. Vector-field glyphs and isoline polylines are NOT
  routed through retained `GpuRender_Line`/`GpuRender_Point` cull buckets and
  are NOT GPU-scene renderable instances; they are auxiliary draw resources
  owned by a backend-local upload helper under `src/graphics/vulkan` mirroring
  the transient debug expansion from `GRAPHICS-007Q`/`GRAPHICS-010Q` and the
  ImGui overlay upload from `GRAPHICS-013CQ` (per-frame host-visible transient
  GPU buffers, recycled each frame, never retained on `GpuWorld`, never exposed
  through RHI or renderer module surfaces). The backend-local helper expands
  `VectorFieldOverlayPacket` and `IsolineOverlayPacket` into per-frame
  transient vertex/index buffers consumed by dedicated visualization-overlay
  passes that LOAD `SceneColorHDR`/`SceneDepth` next to
  `Pass.Forward.Line`/`Pass.Forward.Point`, expressing depth-tested vs
  always-on-top behavior as the same two-pipeline-variant policy resolved for
  transient debug primitives in `GRAPHICS-010Q`. Auxiliary GPU resources
  referenced through packet BDAs and the Htex/UV bake atlas textures are
  uploaded by the existing `Graphics.GpuAssetCache`/`RHI::BufferManager`/
  `RHI::TextureManager` paths once `GRAPHICS-015` texture/buffer residency
  lands. Bake mapping selection is runtime/editor-owned: editor UI maps
  directly to `VisualizationFragmentBakeMapping`
  (`ExistingTexcoords`/`ExistingHtex`/`RecreateHtex`), and `RecreateHtex` is an
  explicit user-driven request scheduled by runtime/geometry on a background
  task through `Extrinsic.Runtime.StreamingExecutor` (async visualization
  baking remains CPU/runtime-only). Graphics increments
  `HtexRecreateRequestCount` and accepts the descriptor without owning the
  Htex regeneration algorithm; once regeneration completes the next extraction
  frame submits the `FragmentBakeAtlasPacket` with `Mapping = ExistingHtex`.
  UV-backed bakes require `MeshHasTexcoords = true` and a non-zero
  `TexcoordBufferBDA`; missing texcoords are rejected from the snapshot and
  counted in `MissingTexcoordCount`.
- `Extrinsic.Graphics.ColormapSystem` owns built-in colormap LUT residency. It
  allocates retained 256-sample RGBA8 LUT textures through RHI managers, submits
  the initial bytes through `RHI::ITransferQueue`, and exposes `IsReady()` as the
  first-frame readiness guard. Bindless colormap indices stay invalid until all
  LUT transfer tokens are valid and complete, keeping renderer/runtime callers on
  the async transfer seam instead of the blocking `IDevice::WriteTexture()`
  helper.
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
  creation (color view for `RGBA8_UNORM`/`RGBA16_FLOAT` resources,
  depth-aspect-only view for depth-class resources, integer-typed view
  for the `R32_UINT` selection-ID resources `EntityId`/`PrimitiveId`)
  remain backend-local under `src/graphics/vulkan` and never leak
  through RHI or renderer module surfaces. `Material0` is
  `RGBA16_FLOAT` carrying scalar PBR channels (roughness in R,
  metallic in G, reserved in B/A) and is visualized as a scalar
  channel false-color preview rather than as an integer slot-ID
  hash; `DebugViewRGBA` is the pass color attachment and is
  deliberately non-selectable as a preview input by
  `DebugViewSystem::BuildInspectionTable()` to prevent self-sampling. Runtime/editor code owns the dictionary
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
the app/runtime boundary.

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
