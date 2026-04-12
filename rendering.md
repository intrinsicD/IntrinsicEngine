# IntrinsicEngine — Full Rendering Pipeline

---

## ⚠ Boundary Audit — Unclear Separation of Concerns

The following issues were identified by reading the source. Each is cross-referenced
to the section of this document where it is relevant. **These are the real boundary
problems, not aspirational ones.**

| # | Location | Problem |
|---|---|---|
| B1 | `RenderDriver` | Owns `LightEnvironmentPacket` (mutable scene state) + `GlobalRenderModeOverride` (view-mode intent), neither of which is a driver concept |
| B2 | `RenderDriver.cpp` | Registers a 700-line ImGui "Render Target Viewer" panel in its constructor — Graphics layer has a hard dep on `Interface` for UI |
| B3 | `RenderDriver::ProcessCompletedGpuWork` | Accepts `ECS::Scene&` and fires `GpuPickCompleted` via `dispatcher.enqueue()` — Graphics module couples directly into ECS |
| B4 | `Runtime.RenderExtraction.cppm` | `FrameContext` and `FrameContextRing` (GPU slot + timeline sync) live in the extraction module alongside pure data structs (`RenderWorld`, `RenderViewPacket`) |
| B5 | `RenderOrchestrator` vs `RenderDriver` | No documented contract for who owns what. You must read both constructors. `GeometryPool` and `MaterialRegistry` are *owned* by `RenderOrchestrator` but *borrowed* by `RenderDriver` as raw references — shared mutable state |
| B6 | `RenderOrchestrator::Impl::InitPipeline()` | Shader filenames and source paths are hardcoded in the Runtime layer — Runtime knows about `"shaders/surface.vert.spv"` etc. |
| B7 | `RenderDriver::BuildGraph()` | Combines three separate concerns: (a) frame resource setup pass, (b) GPUScene sync compute pass, (c) `DefaultPipeline::SetupFrame()`. The first two are hardcoded in the driver, not in the pipeline |
| B8 | `BuildGraphInput` + `RenderPassContext` | Dual-struct problem: `RenderPassContext` is constructed by copying all ~20 fields of `BuildGraphInput` and adding GPU plumbing. Every draw packet, snapshot, and debug span exists in both structs |
| B9 | `RenderOrchestrator::PrepareFrame()` | `GlobalRenderModeOverride` is *stored* in `RenderDriver`, *read* in `PrepareFrame`, used to *filter* `BuildGraphInput` spans — but the override itself is not visible in `BuildGraphInput`, so `BuildGraph` sees pre-filtered input with no indication of why draws are absent |
| B10 | `PrepareEditorOverlay()` timing | GUI frame start/end lifecycle straddles `BeginFrame`/`AcquireFrame`. `RenderDriver::AcquireFrame()` calls `GUI::EndFrame()` on acquire failure — an acquire-failure edge case forces `RenderDriver` to know about the GUI frame lifecycle |
| B11 | `LightEnvironmentPacket` definition | Defined in `Graphics.RenderPipeline.cppm` alongside render graph infra. It is a scene-state struct that has no business being co-located with `IRenderFeature`, `FrameRecipe`, and `RenderBlackboard` |
| B12 | `rendering.md §5` (old) | `ProcessCompletedGpuWork` was shown as part of `PrepareFrame`. In reality it is called from `ResourceMaintenanceService::ProcessCompletedReadbacks()` in the **Maintenance lane**, which runs *after* the Render lane — not before GPU acquire |
| B13 | Lane ordering | Old doc implied parallel lanes; the actual order in `RunFramePhasesStaged` is strictly sequential: `Streaming → Fixed → Render → Maintenance` |

---

## 1. Ownership Hierarchy

```
Runtime::Engine                         (src/Runtime/Runtime.Engine.cppm)
 │
 ├── Runtime::GraphicsBackend           (Runtime.GraphicsBackend.cppm)
 │    │  Owns the Vulkan stack. Has no knowledge of scenes, passes, or draw packets.
 │    ├── RHI::VulkanContext
 │    ├── RHI::VulkanDevice (shared_ptr, exported to dependents)
 │    ├── RHI::VulkanSwapchain
 │    ├── RHI::SimpleRenderer           (frame sync, timeline semaphores, cmd buffers)
 │    ├── RHI::BindlessDescriptorSystem
 │    ├── RHI::DescriptorAllocator
 │    ├── RHI::BufferManager
 │    ├── RHI::TextureManager
 │    └── RHI::TransferManager
 │
 ├── Runtime::RenderOrchestrator        (Runtime.RenderOrchestrator.cppm)
 │    │  Owns everything needed to define *what* to render.
 │    │  Coordinates the ECS FrameGraph tick + the GPU frame.
 │    │  ⚠ See B5: also leaks GeometryPool/MaterialRegistry refs into RenderDriver.
 │    ├── Graphics::ShaderRegistry      (Graphics.ShaderRegistry.cppm)
 │    │    ⚠ See B6: populated with hardcoded shader paths in RenderOrchestrator::Impl.
 │    ├── Graphics::PipelineLibrary     (Graphics.PipelineLibrary.cppm)
 │    ├── Graphics::GeometryPool        (Graphics.Geometry.cppm)
 │    ├── Graphics::MaterialRegistry    (Graphics.MaterialRegistry.cppm)
 │    ├── Graphics::DebugDraw           (Graphics.DebugDraw.cppm)
 │    ├── Graphics::ShaderHotReloadSvc  (Graphics.ShaderHotReload.cppm)
 │    ├── Core::FrameGraph              (ECS system graph, not the render graph)
 │    ├── Core::Memory::LinearArena     (per-frame scratch, global)
 │    ├── Core::Memory::ScopeStack      (per-frame scratch, global)
 │    ├── Runtime::FrameContextRing     (Runtime.RenderExtraction.cppm)  ⚠ B4
 │    └── Graphics::RenderDriver        (Graphics.RenderDriver.cppm)
 │         │  Owns how to execute a GPU frame. Has no knowledge of ECS or scenes.
 │         │  ⚠ See B1: also holds LightEnvironmentPacket + GlobalRenderModeOverride.
 │         │  ⚠ See B3: calls ECS dispatcher in ProcessCompletedGpuWork.
 │         │  ⚠ See B2: registers ImGui panel in constructor.
 │         ├── Graphics::GlobalResources    (Graphics.GlobalResources.cppm)
 │         │    ├── CameraUBO (VulkanBuffer, dynamic offsets × FramesInFlight)
 │         │    ├── GlobalDescriptorSet (set=0: UBO + shadow atlas sampler)
 │         │    └── RHI::TransientAllocator
 │         ├── Graphics::PresentationSystem (Graphics.Presentation.cppm)
 │         │    ├── VulkanSwapchain (borrowed ref)
 │         │    └── Depth image (per swapchain image)
 │         ├── Graphics::InteractionSystem  (Graphics.Interaction.cppm)
 │         │    ├── Pick readback buffers (N slots)
 │         │    └── DebugViewState (selected resource, depth range, culling toggle)
 │         ├── Graphics::RenderGraph        (Graphics.RenderGraph.cppm)
 │         └── Graphics::RenderPipeline     (Graphics.Pipelines.cppm — DefaultPipeline)
 │              └── IRenderFeature × N (the pass list, see §4)
 │
 ├── Runtime::SceneManager              (Runtime.SceneManager.cppm)
 │    └── entt::registry (ECS)
 │
 └── Runtime::AssetPipeline / AssetIngestService / SelectionModule …
```

---

## 2. Frame Loop — Lane Execution Order

`Engine::Run()` drives a **sequential** loop each iteration (see `Runtime.FrameLoop.cppm`).
The three lanes run **in order**, not in parallel:

```
PlatformFrameCoordinator::BeginFrame()
 • glfwPollEvents, resize detection, idle-FPS throttle sleep
 │
 ▼
StreamingLane::BeginFrame()
 • ProcessAssetIngest (main-thread queue drain)
 • ProcessMainThreadQueue (RunOnMainThread() callbacks)
 • ProcessUploads (transfer fence GC)
 │
 ▼
[Fixed-step tick]
 • RunFixedSteps → OnFixedUpdate → RegisterFixedSystems → ECS FrameGraph execute
 │
 ▼
RenderLane::Run()
 • OnUpdate(dt)         — Sandbox variable logic
 • RegisterVariableSystems + ECS FrameGraph execute  — lifecycle, sync, BVH (see §3)
 • DispatchDeferredEvents()  — entt::dispatcher::update()
 • OnRender(alpha)      — DebugDraw calls
 • PrepareEditorOverlay()    ⚠ B10: GUI::BeginFrame + DrawGUI before GPU acquire
 • ExtractRenderWorld()
 • ExecutePreparedFrame()    → PrepareFrame + ExecuteFrame + EndFrame
 │
 ▼
MaintenanceLane::Run()                ⚠ B12/B13: runs AFTER Render, not during
 • CaptureGpuSyncState   — timeline query, memory budgets
 • ProcessCompletedReadbacks → RenderDriver::ProcessCompletedGpuWork  ⚠ B3
 • CollectGpuDeferredDestructions
 • GarbageCollectTransfers
 • ProcessTextureDeletions / ProcessMaterialDeletions
 • CaptureFrameTelemetry
 • BookkeepHotReloads
```

---

## 3. ECS FrameGraph Systems (run inside RenderLane, variable-tick)

These run in dependency order via `Core::DAGScheduler`. All are registered in
`RegisterEngineSystems()` (`Runtime.SystemBundles.cppm`).

| System | File | What it does |
|---|---|---|
| `PropertySetDirtySync` | `Systems/Graphics.Systems.PropertySetDirtySync` | Reads 6 `DirtyTag::*` components; escalates position/topology changes to `GpuDirty=true`; re-extracts cached color/radius vectors for attribute-only changes. Clears tags after. |
| `MeshRendererLifecycle` | `Systems/Graphics.Systems.MeshRendererLifecycle` | Phase 1: uploads vertex/index buffers to `GeometryPool` when `GpuDirty`. Phase 2: allocates GPUScene slot + bounding sphere. Phase 3: populates `ECS::Surface::Component`. |
| `MeshViewLifecycle` | `Systems/Graphics.Systems.MeshViewLifecycle` | Creates edge index buffer via `ReuseVertexBuffersFrom`. Populates `ECS::Line::Component` and `ECS::Point::Component` for wireframe/vertex vis. |
| `GraphLifecycle` | `Systems/Graphics.Systems.GraphLifecycle` | Uploads graph node positions/normals, creates edge index buffer. Populates `Line::Component` + `Point::Component`. Direct mode (host-visible) or Staged (device-local). |
| `PointCloudLifecycle` | `Systems/Graphics.Systems.PointCloudLifecycle` | Reads `Cloud::Positions()`/`Normals()` spans, uploads via Staged mode. Populates `Point::Component`. |
| `GPUSceneSync` | `Systems/Graphics.Systems.GPUSceneSync` | Transform-only updates: calls `GPUScene::QueueUpdate()` for changed transforms without re-uploading geometry. |
| `PrimitiveBVHBuild` | `Systems/Graphics.Systems.PrimitiveBVHBuild` | CPU BVH rebuild for changed geometry (used by CPU picking). |

---

## 4. Render Extraction (`ExtractRenderWorld`)

File: `Runtime.RenderExtraction.cppm` + `Runtime.RenderOrchestrator.cpp`

### 4.1 — What "extraction" means

Extraction runs on the main thread at the end of the variable tick. It reads the
current ECS world (via `WorldSnapshot`) and produces an **immutable** `RenderWorld`
that the GPU frame consumes. No ECS mutation occurs after this point in the frame.

### 4.2 — Inputs to extraction

```
RenderFrameInput {
  Alpha         double
  View          RenderViewPacket     ← camera matrices, viewport, FOV
  World         WorldSnapshot        ← entt::registry snapshot (borrowed read)
}
```

`RenderOrchestrator::ExtractRenderWorld()` calls the free function
`Runtime::ExtractRenderWorld(input)` (which reads `WorldSnapshot`) then enriches
the result with:
- **Lighting** — reads `RenderDriver::GetLightEnvironment()` ⚠ B1
- **Shadow cascade matrices** — computed from camera + light direction
- **DebugDraw snapshots** — frozen copies from `DebugDraw` accumulator
- **InteractionSystem snapshots** — `PendingPick`, `DebugViewState` (via `RenderDriver::GetInteraction()`)
- **GpuSceneSnapshot** — active instance count from `GPUScene`

### 4.3 — Output: `RenderWorld`

```
RenderWorld {
  Alpha                     double
  View                      RenderViewPacket
  World                     WorldSnapshot
  Lighting                  LightEnvironmentPacket
  HasSelectionWork          bool
  SelectionOutline          SelectionOutlinePacket
  SurfacePicking[]          ← per-mesh picking draw data
  LinePicking[]
  PointPicking[]
  SurfaceDraws[]            ← per-mesh rendering draw data
  LineDraws[]
  PointDraws[]
  HtexPatchPreview?
  EditorOverlay             EditorOverlayPacket (HasDrawData flag)
  DebugDrawLines[]          ← frozen DebugDraw accumulator
  DebugDrawOverlayLines[]
  DebugDrawPoints[]
  DebugDrawTriangles[]
  PickRequest               PickRequestSnapshot
  DebugView                 DebugViewSnapshot
  GpuScene                  GpuSceneSnapshot
}
```

---

## 5. Frame Execution (`ExecutePreparedFrame`)

`ExecutePreparedFrame(renderWorld)` calls the following in order, all on the main thread:

```
PrepareEditorOverlay()                       ⚠ B10
  └─ GUI::BeginFrame() + GUI::DrawGUI()
       → EditorOverlayPacket{HasDrawData=true}
  [ImGui draw data is alive from here until EndFrame]
          │
          ▼
FrameContextRing::BeginFrame()
  └─ select next slot by ring index
  └─ wait GPU timeline if slot was previously submitted
  └─ FlushDeferredDeletions for this slot
  └─ push cached GPU profiling sample to telemetry
          │
          ▼
RenderDriver::BeginFrame()
  └─ GeometryPool::ProcessDeletions
  └─ GarbageCollectRetiredPipelines
  └─ BindlessSystem::FlushPending
          │
          ▼
PresentationSystem::BeginFrame() ← vkAcquireNextImageKHR
  [If acquire fails: GUI::EndFrame() is called here]  ⚠ B10
          │
          ▼
  [RenderDriver::RebindFrameAllocators — per-slot LinearArena/ScopeStack]
          │
          ▼
RenderDriver::UpdateGlobals()
  └─ GlobalResources::BeginFrame(frameIndex)  ← reset transient allocator
  └─ GlobalResources::Update()
       ├─ upload CameraBufferObject (view, proj, light dir/color, ambient)
       └─ ApplyPendingPipelineSwap (if a new RenderPipeline was requested)
          │
          ▼
MaterialRegistry::SyncGpuBuffer()            ← upload GpuMaterialData[] SSBO
          │
          ▼
[GlobalRenderModeOverride filtering]          ⚠ B9
  └─ reads RenderDriver::GetGlobalRenderModeOverride()
  └─ filters SurfaceDraws / LineDraws / PointDraws spans
          │
          ▼
ResolveDrawPacketBounds()
  └─ fills LocalBoundingSphere in Line/Point packets from GeometryPool
          │
          ▼
CullDrawPackets()
  └─ CPU frustum cull → CulledDrawList{VisibleLineIndices, VisiblePointIndices}
  [Surface packets excluded: GPU-driven via GPUScene compute]
          │
          ▼
RenderDriver::BuildGraph(BuildGraphInput)     ⚠ B7, B8
  │
  ├─ RenderGraph::Reset(frameIndex)
  ├─ AddPass "FrameSetup"  ← imports Backbuffer + depth; creates frame-transient resources ⚠ B7
  ├─ AddPass "SceneUpdate" ← dispatches scene_update.comp (GPUScene scatter)              ⚠ B7
  └─ DefaultPipeline::SetupFrame(RenderPassContext)  ← registers all render passes (see §6)
          │
          ▼
RenderDriver::ExecuteGraph()
  ├─ RenderGraph::Compile(frameIndex)          ← barrier calc, aliasing, packetize
  ├─ BuildDebugPassList / BuildDebugImageList  ← for UI + telemetry
  ├─ ValidateCompiledGraph()                   ← structured diagnostics
  ├─ DefaultPipeline::PostCompile()            ← update descriptor sets with final image views
  ├─ GlobalResources::UpdateShadowAtlasBinding ← bind compiled atlas view to global set
  └─ RenderGraph::Execute(primaryCmd)          ← record + submit secondary cmd bufs
          │
          ▼
RenderDriver::EndFrame()
  └─ PresentationSystem::EndFrame()
       ├─ vkQueueSubmit (graphics queue, timeline semaphore signal)
       └─ vkQueuePresentKHR
```

Note: `ProcessCompletedGpuWork` (GPU pick readback + `GpuPickCompleted` dispatch) runs in
the **Maintenance lane** (after this frame's render lane completes). See §2 and B12.

---

## 6. Pass Order — `DefaultPipeline::SetupFrame`

File: `Graphics.Pipelines.cpp`

Each pass is an `IRenderFeature` that calls `AddPasses(RenderPassContext&)`, which
registers lambdas into the `RenderGraph` via `AddPass<Data>()`.

```
Pass 1  — PickingPass            (Graphics.Passes.Picking.cppm)
Pass 2  — ShadowPass             (Graphics.Passes.Shadow.cppm)      [disabled by default]
Pass 3  — SurfacePass            (Graphics.Passes.Surface.cppm)
Pass 4  — CompositionPass        (Graphics.Passes.Composition.cppm) [deferred path only]
Pass 5  — LinePass               (Graphics.Passes.Line.cppm)
Pass 6  — PointPass              (Graphics.Passes.Point.cppm)
Pass 7  — PostProcessPass        (Graphics.Passes.PostProcess.cppm)
Pass 8  — SelectionOutlinePass   (Graphics.Passes.SelectionOutline.cppm)
Pass 9  — DebugViewPass          (Graphics.Passes.DebugView.cppm)   [optional]
Pass 10 — ImGuiPass              (Graphics.Passes.ImGui.cppm)
```

Each pass is feature-gated via `Core::FeatureRegistry` (e.g., `"SurfacePass"_id`).
Missing the feature key = pass skipped entirely.

`FrameSetup` and `SceneUpdate` are hardcoded in `RenderDriver::BuildGraph()` and
run before any pipeline pass. ⚠ B7: these should logically be registered by the
pipeline itself (e.g., as a `FramePrologueFeature : IRenderFeature`), not hardcoded
in the driver.

---

## 7. Per-Pass Detail

### Pass 1 — PickingPass
**Shaders:** `pick_mesh.vert/frag`, `pick_line.vert/frag`, `pick_point.vert/frag`  
**Writes:** `EntityId` (R32_UINT), `PrimitiveId` (R32_UINT), `SceneDepth`  
**When:** Only when a pick request is pending (`PickRequest.Pending = true`).  
**What:** Dual-channel MRT. `EntityId` = entt entity handle. `PrimitiveId` = high 2 bits = domain (`00`=surface, `01`=line, `10`=point), low 30 bits = face/edge/point index. After this pass a copy-pass blits both buffers to CPU-readable readback buffers.

The readback result is consumed the next maintenance lane via
`InteractionSystem::ProcessReadbacks()` → `TryConsumePickResult()` →
`GpuPickCompleted` dispatcher event. ⚠ B3: the dispatch crosses into ECS inside
`RenderDriver::ProcessCompletedGpuWork`.

---

### Pass 2 — ShadowPass *(currently disabled)*
**Shader:** `shadow_depth.vert`  
**Writes:** `ShadowAtlas` (2048×N depth atlas, N cascades up to 4)  
**What:** Renders scene depth from each cascade's orthographic LVP matrix into a packed depth atlas.

---

### Pass 3 — SurfacePass
**Shaders:** `surface.vert/frag`, `surface_gbuffer.frag`, `debug_surface.vert/frag`  
**Writes:** `SceneColorHDR` (forward) or `SceneNormal`/`Albedo`/`Material0` (deferred), `SceneDepth`

Three sub-stages of increasing GPU-drivenness:

#### Depth Prepass (optional, `FrameRecipe::DepthPrepass = true`)
Shader: `surface.vert` (depth-only). Writes `SceneDepth` with `CLEAR`; main raster uses `LOAD`.

#### Stage 2 — CPU-driven forward/G-buffer
CPU builds `GpuInstanceData[]` per visible entity → `m_InstanceBuffer[frame]`.  
CPU builds `VkDrawIndexedIndirectCommand[]` → `m_Stage2IndirectIndexedBuffer[frame]`.  
Draw: `vkCmdDrawIndexedIndirect`.

#### Stage 3 — GPU-driven (`m_EnableGpuCulling = true`)
1. CPU writes `GpuInstanceData[]` to `m_InstanceBuffer[frame]`.
2. Compute `instance_cull_multigeo.comp`: reads GPUScene bounds, camera frustum → writes indirect commands + visibility remap + draw count.
3. Draw: `vkCmdDrawIndexedIndirectCount`.

**Descriptor sets:**
| Set | Binding | Content |
|---|---|---|
| 0 | 0 | `CameraBufferObject` UBO (view/proj + lighting) |
| 1 | 0 | Bindless texture array |
| 2 | 0 | `GpuInstanceData[]` SSBO |
| 3 | 0 | `GpuMaterialData[]` SSBO |

**Push constants (MeshPushConstants, 120 bytes):**
`Model (mat4)` · `PtrPositions (BDA)` · `PtrNormals (BDA)` · `PtrFaceAttr (BDA)` · `PtrVertexAttr (BDA)` · `PtrIndices (BDA)` · `PtrCentroids (BDA)` · `VisibilityBase (u32)` · `MaterialSlot (u32)`

**Transient debug triangles:** `AddDebugTrianglePass` uploads `DebugDraw::Triangle()` accumulator via host-visible BDA buffers and renders with `kPipeline_DebugSurface` (alpha blend, no depth write).

---

### Pass 4 — CompositionPass *(deferred path only)*
**Shader:** `deferred_lighting.frag`  
**Reads:** `SceneNormal`, `Albedo`, `Material0`, `SceneDepth`  
**Writes:** `SceneColorHDR`  
**What:** Fullscreen triangle lighting pass. Reconstructs world position from depth, evaluates directional light + ambient from push constants mirroring the CameraBufferObject layout.

---

### Pass 5 — LinePass
**Shaders:** `line.vert/frag`  
**Reads:** `SceneDepth` (depth-tested), or none (overlay)  
**Writes:** `SceneColorHDR`

Two sub-pipelines: `m_Pipeline` (depth on) and `m_OverlayPipeline` (depth off).

**Retained draws** (from `[]LineDrawPacket`, filtered by `CulledDrawList::VisibleLineIndices`):
- `BDA PtrPositions` from shared vertex buffer
- `BDA PtrEdges` from edge index buffer
- `BDA PtrEdgeAttr` from persistent per-entity `m_EdgeAttrBuffers` (packed ABGR per edge)

**Transient draws** (from `DebugDrawLines` / `DebugDrawOverlayLines`):
- Per-frame flat `[start0,end0,start1,end1,…]` vec3 array → `m_TransientPosBuffer[frame]`
- Identity edge pair buffer (pre-allocated)
- Optional per-segment color buffer

**Push constants (104 bytes):** `Model (mat4)` · `PtrPositions` · `PtrEdges` · `PtrEdgeAttr` · `LineWidth` · `Color`

---

### Pass 6 — PointPass
**Shaders:** `point_flatdisc.vert/frag` (mode 0), `point_surfel.vert/frag` (mode 1), `point_sphere.vert/frag` (mode 3)  
**Writes:** `SceneColorHDR`, depth-tests against `SceneDepth`  
**Depth bias:** `(-2.0, -2.0)` to prevent z-fighting with surface.

Four per-mode pipelines. Each vertex expands a position into a camera-facing quad (or surfel-oriented quad). Surfel/EWA mode uses the normal for orientation; sphere mode adds analytical per-fragment normal.

**Push constants (120 bytes):** `Model (mat4)` · `PtrPositions` · `PtrNormals` · `PtrAttr` · `PointSize` · `SizeMultiplier` · `Viewport` · `Color` · `Flags`

---

### Pass 7 — PostProcessPass
File: `Graphics.Passes.PostProcess.cppm`

Five sub-passes in sequence:

```
SceneColorHDR
    │
    ▼
BloomSubPass           post_bloom_downsample.frag × N mips
    │                  post_bloom_upsample.frag × N mips
    ▼
ToneMapSubPass         post_tonemap.frag (ACES filmic or Reinhard)
    │                  reads SceneColorHDR + bloom upsampled chain
    │                  writes SceneColorLDR
    ▼
FXAASubPass / SMAASubPass
    │   FXAA:  post_fxaa.frag
    │   SMAA:  post_smaa_edge → post_smaa_blend → post_smaa_resolve
    ▼
HistogramSubPass       post_histogram.comp (compute, 128-bucket luminance)
                       → async readback for auto-exposure / UI display
```

---

### Pass 8 — SelectionOutlinePass
**Shader:** `selection_outline.frag` (fullscreen triangle)  
**Reads:** `EntityId` (R32_UINT sampled)  
**Writes:** Presentation target (`SceneColorLDR` or `Backbuffer`)  
**What:** Sobel-style edge detect on EntityId. Pixels whose 8-neighbourhood contains a selected PickID get the outline color. Hovered entity gets a distinct color. Thickness and colors configurable via `SelectionOutlineSettings`.

---

### Pass 9 — DebugViewPass *(optional)*
**Shader:** `debug_view.frag` / `debug_view.comp`  
**Reads:** Selected render graph image  
**Writes:** Presentation target  
**What:** Fullscreen blit with per-format decode (UINT→hue, depth→near/far remap, normal packed as RGB). Preview panel renders a scaled-down version via `m_PreviewImages[]`.

---

### Pass 10 — ImGuiPass
**What:** Calls `Interface::GUI::Render(cmd)` which calls `ImGui::Render()` + `ImGui_ImplVulkan_RenderDrawData()`. Skipped when `EditorOverlay.HasDrawData = false`. Renders onto the presentation target with `LOAD`.

---

## 8. RenderGraph Compile + Execute

File: `Graphics.RenderGraph.cppm`

### Compile (`RenderGraph::Compile(frameIndex)`)
1. **`ResolveTransientResources()`** — allocate or reuse images/buffers from pool, keyed by format+size+usage. Resources live within `[StartPass, EndPass]` → memory aliasing.
2. **`BuildSchedulerGraph()`** — DAG: pass A → pass B when B reads what A writes. `Core::DAGScheduler` topological sort → execution layers.
3. **`CalculateBarriers()`** — per-pass `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` deltas (srcStage/srcAccess/oldLayout → dstStage/dstAccess/newLayout). Stored per-pass in arena.
4. **`Packetize()`** — merge consecutive same-layer passes sharing the same attachments into `ExecutionPacket`s (→ fewer `vkBeginCommandBuffer` calls).
5. **PostCompile callbacks** — SelectionOutlinePass, DebugViewPass, CompositionPass, PostProcessPass update their descriptor sets to the newly resolved `VkImageView` handles.

### Execute (`RenderGraph::Execute(cmd)`)
For each execution layer:
- For each `ExecutionPacket`:
    - Record into a secondary command buffer
    - For each pass: emit barriers → `vkCmdBeginRendering` → `ExecuteFn` → `vkCmdEndRendering`
    - `vkEndCommandBuffer` (secondary) → `vkCmdExecuteCommands` on primary

---

## 9. Render Resources (Blackboard)

All passes communicate via `RenderBlackboard` (StringID → `RGResourceHandle`).

| Resource Name | Format | Lifetime | Producer | Consumer(s) |
|---|---|---|---|---|
| `Backbuffer` | swapchain | Imported | PresentationSystem | ImGui, SelectionOutline |
| `SceneDepth` | D32_SFLOAT | Imported | PresentationSystem | Picking, Surface, Line, Point |
| `EntityId` | R32_UINT | Transient | PickingPass | SelectionOutline, DebugView, readback copy |
| `PrimitiveId` | R32_UINT | Transient | PickingPass | readback copy |
| `SceneNormal` | R16G16B16A16_SFLOAT | Transient | SurfacePass (GBuffer) | CompositionPass |
| `Albedo` | R8G8B8A8_UNORM | Transient | SurfacePass (GBuffer) | CompositionPass |
| `Material0` | R16G16B16A16_SFLOAT | Transient | SurfacePass (GBuffer) | CompositionPass |
| `SceneColorHDR` | R16G16B16A16_SFLOAT | Transient | SurfacePass / CompositionPass | Line, Point, PostProcess |
| `SceneColorLDR` | swapchain | Transient | ToneMapSubPass | FXAA/SMAA, SelectionOutline, DebugView, ImGui |
| `ShadowAtlas` | D16_UNORM | Transient | ShadowPass | SurfacePass (sampler, bound to global set) |

---

## 10. Key GPU Buffers

| Buffer | Owned by | Updated by | Consumed by |
|---|---|---|---|
| Camera UBO (dynamic, 3 slots) | `GlobalResources` | `GlobalResources::Update()` | All passes (set=0, binding=0) |
| GPUScene instances SSBO | `GPUScene` | `GPUScene::Sync()` via `scene_update.comp` | `instance_cull*.comp`, `surface.vert` |
| GPUScene bounds SSBO | `GPUScene` | same | `instance_cull*.comp` |
| Instance SSBO (per-frame) | `SurfacePass` | CPU each frame | `surface.vert` (set=2) |
| Material SSBO | `MaterialRegistry` | `SyncGpuBuffer()` | `surface.frag` (set=3) |
| Indirect draw buffer | `SurfacePass` | Stage 2: CPU / Stage 3: `instance_cull_multigeo.comp` | `vkCmdDrawIndexedIndirectCount` |
| Edge attr buffer (per entity) | `LinePass` | on first use / on dirty | `line.vert` (BDA PtrEdgeAttr) |
| Point attr / radii buffer | `PointPass` | on first use / on dirty | `point_*.vert` (BDA PtrAttr) |
| Face / vertex attr buffer | `SurfacePass` | on first use / on dirty | `surface.vert` (BDA PtrFaceAttr/PtrVertexAttr) |

---

## 11. Shader → Pipeline Map

| Shader files | Pipeline ID | Owner | Stage |
|---|---|---|---|
| `surface.vert/frag` | `Pipeline.Surface` | PipelineLibrary | SurfacePass forward |
| `surface.vert` + `surface_gbuffer.frag` | `Pipeline.SurfaceGBuffer` | PipelineLibrary | SurfacePass G-buffer |
| `surface.vert` (depth only) | `Pipeline.DepthPrepass` | PipelineLibrary | SurfacePass depth prepass |
| `debug_surface.vert/frag` | `Pipeline.DebugSurface` | PipelineLibrary | SurfacePass transient triangles |
| `pick_mesh.vert/frag` | `Pipeline.PickMesh` | PipelineLibrary | PickingPass |
| `pick_line.vert/frag` | `Pipeline.PickLine` | PipelineLibrary | PickingPass |
| `pick_point.vert/frag` | `Pipeline.PickPoint` | PipelineLibrary | PickingPass |
| `shadow_depth.vert` | `Pipeline.ShadowDepth` | PipelineLibrary | ShadowPass |
| `line.vert/frag` | (built by LinePass) | LinePass | LinePass |
| `point_flatdisc.vert/frag` | (built by PointPass) | PointPass | PointPass mode 0 |
| `point_surfel.vert/frag` | (built by PointPass) | PointPass | PointPass mode 1 |
| `point_sphere.vert/frag` | (built by PointPass) | PointPass | PointPass mode 3 |
| `scene_update.comp` | (compute) | PipelineLibrary | SceneUpdate pass (in BuildGraph ⚠ B7) |
| `instance_cull_multigeo.comp` | (compute) | PipelineLibrary | SurfacePass Stage 3 |
| `deferred_lighting.frag` | (built by CompositionPass) | CompositionPass | CompositionPass |
| `post_tonemap.frag` | (built by ToneMapSubPass) | ToneMapSubPass | PostProcessPass |
| `post_bloom_*.frag` | (built by BloomSubPass) | BloomSubPass | PostProcessPass |
| `post_fxaa.frag` | (built by FXAASubPass) | FXAASubPass | PostProcessPass |
| `post_smaa_*.frag` | (built by SMAASubPass) | SMAASubPass | PostProcessPass |
| `post_histogram.comp` | (compute) | HistogramSubPass | PostProcessPass |
| `selection_outline.frag` | (built by SelectionOutlinePass) | SelectionOutlinePass | SelectionOutlinePass |
| `debug_view.frag` | (built by DebugViewPass) | DebugViewPass | DebugViewPass |

---

## 12. Full Frame Data-Flow Diagram

```
[GLFW input] ──────────────────────────────────────────────────────── ActivityTracker
                                                                            │
ECS Scene (entt::registry)                                                  │
    │                                                                       │
    ▼  ECS FrameGraph (variable-tick):                                      │
    │  PropertySetDirtySync → MeshRendererLifecycle → MeshViewLifecycle     │
    │  → GraphLifecycle → PointCloudLifecycle → GPUSceneSync                │
    │  → PrimitiveBVHBuild                                                  │
    │                                                                       │
    ▼  DispatchDeferredEvents (entt::dispatcher::update)                    │
    │                                                                       │
    ▼  OnRender(alpha)  ──► DebugDraw accumulator                           │
    │                                                                       ▼
    ▼  PrepareEditorOverlay()  ──► ImGui::NewFrame + DrawGUI         FramePacing
    │     → EditorOverlayPacket{HasDrawData=true}                    (idle sleep)
    │
    ▼  ExtractRenderWorld()  → immutable RenderWorld snapshot
    │     (enriched with Lighting from RenderDriver ⚠ B1,
    │      InteractionSystem snapshots, DebugDraw frozen copies)
    │
    ▼  FrameContextRing::BeginFrame()  (GPU timeline wait, deferred deletions)
    │
    ▼  PresentationSystem::BeginFrame()  ← vkAcquireNextImageKHR
    │     [on failure: GUI::EndFrame() called here ⚠ B10]
    │
    ▼  RenderDriver::UpdateGlobals()
    │     └─ GlobalResources::Update() (CameraBufferObject upload)
    │     └─ ApplyPendingPipelineSwap (if requested)
    │
    ▼  MaterialRegistry::SyncGpuBuffer()
    │
    ▼  GlobalRenderModeOverride filtering  ⚠ B9
    │     (reads from RenderDriver, filters draw spans in-place)
    │
    ▼  ResolveDrawPacketBounds + CullDrawPackets  (CPU frustum cull lines/points)
    │
    ▼  RenderDriver::BuildGraph(BuildGraphInput)  ⚠ B7, B8
    │     ├─ RenderGraph::Reset
    │     ├─ AddPass "FrameSetup" (resource import/create)  ⚠ B7
    │     ├─ AddPass "SceneUpdate" (GPUScene sync dispatch)  ⚠ B7
    │     └─ DefaultPipeline::SetupFrame(RenderPassContext)  ← all 10 passes
    │
    ▼  RenderDriver::ExecuteGraph()
    │     ├─ RenderGraph::Compile (barriers, aliasing, packetize)
    │     ├─ ValidateCompiledGraph
    │     ├─ DefaultPipeline::PostCompile (update descriptor sets)
    │     ├─ GlobalResources::UpdateShadowAtlasBinding
    │     └─ RenderGraph::Execute(primaryCmd)
    │          ├─ [Layer 0] PickingPass (EntityId + PrimitiveId MRT)
    │          ├─ [Layer 1] ShadowPass (optional)
    │          ├─ [Layer 2] SurfacePass
    │          │    ├─ DepthPrepass (optional)
    │          │    ├─ instance_cull_multigeo.comp (GPU cull → indirect draws)
    │          │    ├─ surface.vert/frag or surface_gbuffer.frag
    │          │    └─ debug_surface (transient triangles, alpha blend)
    │          ├─ [Layer 3] CompositionPass (deferred only)
    │          ├─ [Layer 4] LinePass
    │          ├─ [Layer 5] PointPass
    │          ├─ [Layer 6] PostProcessPass (Bloom → ToneMap → FXAA/SMAA → Histogram)
    │          ├─ [Layer 7] SelectionOutlinePass
    │          ├─ [Layer 8] DebugViewPass (optional)
    │          └─ [Layer 9] ImGuiPass
    │
    ▼  vkQueueSubmit (graphics queue, timeline semaphore signal)
    │
    ▼  vkQueuePresentKHR
    │
    ▼  [--- Maintenance lane runs AFTER this frame's render lane ---]  ⚠ B12/B13
         CaptureGpuSyncState
         ProcessCompletedReadbacks
           └─ InteractionSystem::ProcessReadbacks
           └─ TryConsumePickResult → GpuPickCompleted event  ⚠ B3
         CollectGpuDeferredDestructions
         GarbageCollectTransfers
         ProcessTextureDeletions / ProcessMaterialDeletions
         CaptureFrameTelemetry
         BookkeepHotReloads
```

---

## 13. Recommended Refactoring Targets (Priority Order)

These are concrete changes that would eliminate the boundaries listed in §⚠.

### High priority

**B3 — Decouple `RenderDriver` from ECS**  
`RenderDriver::ProcessCompletedGpuWork(ECS::Scene&, uint64_t)` should return
`std::optional<InteractionSystem::PickResultGpu>` and nothing more. The dispatch
`scene.GetDispatcher().enqueue<GpuPickCompleted>()` belongs in
`ResourceMaintenanceService::ProcessCompletedReadbacks()` at the Runtime layer,
which already owns both `SceneManager` and `RenderOrchestrator`.

**B1 — Move `LightEnvironmentPacket` out of `RenderDriver`**  
Scene lighting is not a driver concept. It should live in `SceneManager` or be
a first-class field on `RenderWorld`. `RenderDriver::GetLightEnvironment()` /
`SetLightEnvironment()` is a mutating accessor that UI panels use directly —
this is a scene-state side channel that bypasses the extraction contract.

**B7 — Promote `FrameSetup` and `SceneUpdate` to pipeline passes**  
Create a `FramePrologueFeature : IRenderFeature` that owns `AddFrameSetupPass()`
and `AddSceneUpdatePass()`. Register it first in `DefaultPipeline`. Remove the
two hardcoded `AddPass` calls from `RenderDriver::BuildGraph()`. The driver
becomes a pure graph executor: it calls `m_ActivePipeline->SetupFrame()` and
nothing more beyond `RenderGraph::Reset()`.

### Medium priority

**B9 — Make `GlobalRenderModeOverride` explicit in `BuildGraphInput`**  
Add a `GlobalRenderModeOverride RenderMode` field to `BuildGraphInput`. Do the
span filtering after populating `BuildGraphInput` and before calling
`RenderDriver::BuildGraph`. This makes it visible and testable.

**B8 — Eliminate `RenderPassContext` as a duplicate of `BuildGraphInput`**  
`RenderPassContext` = `BuildGraphInput` + GPU plumbing (UBO, descriptor sets,
renderer, bindless). Construct the plumbing fields inline at
`DefaultPipeline::SetupFrame()`; passes can receive them as a smaller
`GpuFrameContext` struct. The large draw-packet spans do not need to live on
`RenderPassContext` at all — passes query what they need.

**B4 — Move `FrameContext`/`FrameContextRing` to a dedicated module**  
Create `Runtime.FrameContext.cppm`. `Runtime.RenderExtraction.cppm` should
contain only the extraction data types (`RenderWorld`, `RenderViewPacket`,
`RenderFrameInput`, `ExtractRenderWorld()`).

**B2 — Extract `RenderDriver`'s ImGui panel to a dedicated file**  
Create `Graphics.RenderTargetViewerPanel.cppm` (or place it in `EditorUI`).
The panel registers itself via `GUI::RegisterPanel` during app startup, not
inside `RenderDriver`'s constructor. `RenderDriver` exposes a thin read-only
query interface (`DumpRenderGraphToString`, `GetLastDebugImages`, etc.) that
the panel consumes.

### Low priority

**B6 — Move shader registration to a catalog file**  
Create `Graphics.ShaderCatalog.cppm` with a constexpr table of
`{StringID, spv_path, glsl_source}` entries. `RenderOrchestrator` calls
`ShaderCatalog::RegisterAll(shaderRegistry, shaderSourceDir)` instead of
inlining 40 registrations.

**B10 — Document GUI frame lifecycle explicitly**  
The `PrepareEditorOverlay → BeginFrame → AcquireFrame → [EndFrame on failure]`
contract should be in a code comment on `AcquireFrame()`. Consider moving the
`IsFrameActive()` guard to `PrepareEditorOverlay`'s caller rather than burying
it in `AcquireFrame`.

**B11 — Move `LightEnvironmentPacket` definition**  
It lives in `Graphics.RenderPipeline.cppm` among render graph infrastructure.
Move it to `Graphics.Camera.cppm` (already holds `CameraComponent`) or to a
new `Graphics.SceneState.cppm`.


### My Goal Architecture

