# IntrinsicEngine — Full Rendering Pipeline

---

## 1. Ownership Hierarchy

```
Runtime::Engine                         (src/Runtime/Runtime.Engine.cppm)
 ├── Runtime::GraphicsBackend           (Runtime.GraphicsBackend.cppm)
 │    ├── RHI::VulkanDevice             (RHI.Device.cppm)
 │    ├── RHI::VulkanSwapchain          (RHI.Swapchain.cppm)
 │    ├── RHI::SimpleRenderer           (RHI.Renderer.cppm)
 │    ├── RHI::BindlessDescriptorSystem (RHI.Bindless.cppm)
 │    ├── RHI::DescriptorAllocator      (RHI.Descriptors.cppm)
 │    ├── RHI::BufferManager            (RHI.Buffer.cppm)
 │    ├── RHI::TextureManager           (RHI.TextureManager.cppm)
 │    └── RHI::TransferManager          (RHI.Transfer.cppm)
 │
 ├── Runtime::RenderOrchestrator        (Runtime.RenderOrchestrator.cppm)
 │    ├── Graphics::ShaderRegistry      (Graphics.ShaderRegistry.cppm)
 │    ├── Graphics::PipelineLibrary     (Graphics.PipelineLibrary.cppm)
 │    ├── Graphics::GPUScene            (Graphics.GPUScene.cppm)
 │    ├── Graphics::MaterialRegistry    (Graphics.MaterialRegistry.cppm)
 │    ├── Graphics::GeometryPool        (Graphics.Geometry.cppm)
 │    ├── Graphics::DebugDraw           (Graphics.DebugDraw.cppm)
 │    ├── Graphics::ShaderHotReloadSvc  (Graphics.ShaderHotReload.cppm)
 │    ├── Core::Memory::LinearArena     (per-frame scratch, per FrameContext)
 │    ├── Core::Memory::ScopeStack      (per-frame scratch, per FrameContext)
 │    └── Graphics::RenderDriver        (Graphics.RenderDriver.cppm)
 │         ├── Graphics::GlobalResources    (Graphics.GlobalResources.cppm)
 │         │    ├── CameraUBO (VulkanBuffer, dynamic offsets × FramesInFlight)
 │         │    ├── GlobalDescriptorSet (set=0: UBO + shadow atlas sampler)
 │         │    └── RHI::TransientAllocator (transient image/buffer pool)
 │         ├── Graphics::PresentationSystem (Graphics.Presentation.cppm)
 │         │    ├── VulkanSwapchain (borrowed)
 │         │    └── Depth images (per swapchain image)
 │         ├── Graphics::InteractionSystem  (Graphics.Interaction.cppm)
 │         │    └── Pick readback buffer + GPU pick result
 │         └── Graphics::DefaultPipeline   (Graphics.Pipelines.cppm)
 │              └── IRenderFeature × N (the pass list, see §4)
 │
 ├── Runtime::SceneManager              (Runtime.SceneManager.cppm)
 │    └── entt::registry (ECS)
 │
 └── Runtime::AssetPipeline / AssetIngestService / SelectionModule …
```

---

## 2. Frame Loop

`Engine::Run()` drives three **lanes** every iteration (see `Runtime.FrameLoop.cppm`):

```
┌─────────────────────────────────────────────────────────────────┐
│  PlatformFrameCoordinator::BeginFrame()                         │
│   • glfwPollEvents, resize detection, idle-FPS throttle sleep   │
└──────────────────────────┬──────────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
   StreamingLane    MaintenanceLane    RenderLane
   ─────────────    ───────────────    ──────────
   ProcessIngest    GPU readbacks      ECS FrameGraph
   MainThreadQ      Deferred destruct  Fixed-step tick
   Upload fence GC  Transfer GC        Variable-step tick
                    Texture/mat GC     ExtractRenderWorld
                    Telemetry          ExecutePreparedFrame
                    ShaderHotReload
```

**`RenderLaneCoordinator::Run()`** does (in order):
1. `OnUpdate(dt)` — Sandbox logic
2. Register engine ECS systems into `Core::FrameGraph`
3. `OnRegisterSystems()` — Sandbox systems
4. `DispatchDeferredEvents()` — `entt::dispatcher::update()`
5. `FrameGraphExecutor::Execute()` — compile + run ECS FrameGraph (lifecycle, sync, BVH)
6. `OnRender(alpha)` — Sandbox render hook (debug draw calls live here)
7. `ExtractRenderWorld()` → `ExecutePreparedFrame(renderWorld)`

---

## 3. ECS FrameGraph Systems (run inside step 5 above)

These run in dependency order via `Core::DAGScheduler`. All are registered in `RegisterEngineSystems()` (`Runtime.SystemBundles.cppm`).

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

Produces an immutable `RenderWorld` struct:

```
RenderWorld {
  RenderViewPacket View          ← camera matrices, viewport, FOV
  LightEnvironmentPacket Lighting ← dir light, ambient, shadow params+cascades
  []SurfaceDrawPacket             ← per-mesh: geo handle, world matrix, face/vertex colors
  []LineDrawPacket                ← per-edge-set: geo+edge handles, color, width, bounding sphere
  []PointDrawPacket               ← per-point-set: geo handle, color, size, mode, bounding sphere
  []PickingSurfacePacket          ← surface picking: geo + triangle→face ID sidecar
  []PickingLinePacket             ← line picking
  []PickingPointPacket            ← point picking
  SelectionOutlinePacket          ← selected+hovered PickIDs
  []DebugDraw::LineSegment        ← depth-tested debug lines
  []DebugDraw::LineSegment        ← overlay (no-depth) debug lines
  []DebugDraw::PointMarker        ← debug points
  []DebugDraw::TriangleVertex     ← debug triangles (face selection fill, etc.)
  EditorOverlayPacket             ← HasDrawData flag for ImGuiPass
  PickRequestSnapshot             ← pending pixel pick (x, y)
  DebugViewSnapshot               ← which buffer to visualize + depth range
  GpuSceneSnapshot                ← active instance count for cull dispatch
}
```

---

## 5. Frame Preparation (`PrepareFrame`)

File: `Runtime.RenderOrchestrator.cpp`

```
PrepareEditorOverlay()              ← GUI::BeginFrame() + DrawGUI() → EditorOverlayPacket
     ↓
FrameContextRing::BeginFrame()      ← wait GPU timeline (vkWaitSemaphores), flush deferred deletions
     ↓
RenderDriver::BeginFrame()
  └─ PresentationSystem::BeginFrame()   ← vkAcquireNextImageKHR
     ↓
RenderDriver::ProcessCompletedGpuWork() ← GPU pick readback, resolve GPU profiling timestamps
     ↓
RenderDriver::UpdateGlobals()
  └─ GlobalResources::Update()
       ├─ upload CameraBufferObject (view, proj, light dir/color/intensity, ambient)
       └─ GlobalResources::BeginFrame() — reset TransientAllocator
     ↓
GPUScene::Sync(cmd, frameIndex)         ← dispatch scene_update.comp (scatter pending QueueUpdate()s)
     ↓
MaterialRegistry::SyncGpuBuffer()       ← upload GpuMaterialData[] to material SSBO
     ↓
ResolveDrawPacketBounds()               ← fill LocalBoundingSphere in Line/Point draw packets from GeometryPool
     ↓
CullDrawPackets()                       ← CPU frustum cull → CulledDrawList{VisibleLineIndices, VisiblePointIndices}
     ↓
RenderDriver::BuildGraph(BuildGraphInput)
  └─ DefaultPipeline::SetupFrame(RenderPassContext)   ← adds all passes to RenderGraph (see §6)
  └─ RenderGraph::Compile(frameIndex)                 ← barrier calculation + packetization (see §7)
  └─ DefaultPipeline::PostCompile()                   ← update descriptor sets after image handles are known
```

---

## 6. Pass Order — `DefaultPipeline::SetupFrame`

File: `Graphics.Pipelines.cpp`

Passes call `IRenderFeature::AddPasses(RenderPassContext&)` which registers lambdas via `RenderGraph::AddPass<Data>()`.

```
Pass 1  — PickingPass            (Graphics.Passes.Picking.cppm)
Pass 2  — ShadowPass             (Graphics.Passes.Shadow.cppm)        [optional, off by default]
Pass 3  — SurfacePass            (Graphics.Passes.Surface.cppm)
Pass 4  — CompositionPass        (Graphics.Passes.Composition.cppm)   [deferred path only]
Pass 5  — LinePass               (Graphics.Passes.Line.cppm)
Pass 6  — PointPass              (Graphics.Passes.Point.cppm)
Pass 7  — PostProcessPass        (Graphics.Passes.PostProcess.cppm)
Pass 8  — SelectionOutlinePass   (Graphics.Passes.SelectionOutline.cppm)
Pass 9  — DebugViewPass          (Graphics.Passes.DebugView.cppm)     [optional]
Pass 10 — ImGuiPass              (Graphics.Passes.ImGui.cppm)
```

Each pass is feature-gated via `Core::FeatureRegistry` (e.g., `"SurfacePass"_id`). Missing the feature key = pass skipped entirely.

---

## 7. Per-Pass Detail

### Pass 1 — PickingPass
**Shaders:** `pick_mesh.vert/frag`, `pick_line.vert/frag`, `pick_point.vert/frag`  
**Writes:** `EntityId` (R32_UINT), `PrimitiveId` (R32_UINT), `SceneDepth`  
**When:** Only when a pick request is pending (`PickRequest.Pending = true`).  
**What:** Dual-channel MRT. `EntityId` = entt entity handle. `PrimitiveId` = high 2 bits = domain (`00`=surface, `01`=line, `10`=point), low 30 bits = face/edge/point index. After this pass a copy-pass blits both buffers to CPU-readable readback buffers.

---

### Pass 2 — ShadowPass *(currently disabled)*
**Shader:** `shadow_depth.vert`  
**Writes:** `ShadowAtlas` (2048×N depth atlas, N cascades up to 4)  
**What:** Renders scene depth from each cascade's orthographic LVP matrix into a packed depth atlas. Uses `instance_cull.comp` or a CPU-built indirect buffer (not wired yet).

---

### Pass 3 — SurfacePass
**Shaders:** `surface.vert/frag`, `surface_gbuffer.frag`, `debug_surface.vert/frag`  
**Writes:** `SceneColorHDR` (forward) or `SceneNormal`/`Albedo`/`Material0` (deferred), `SceneDepth`

The surface pass has three sub-stages of increasing GPU-drivenness:

#### Depth Prepass (optional, `FrameRecipe::DepthPrepass = true`)
- Shader: `surface.vert` (depth-only, no fragment output)
- Writes `SceneDepth` with `CLEAR`, so the main raster pass uses `LOAD`.

#### Stage 1/2 — CPU-driven forward/G-buffer
CPU builds `GpuInstanceData[]` per visible entity → writes into `m_InstanceBuffer[frame]`.  
CPU builds `VkDrawIndexedIndirectCommand[]` → writes into `m_Stage2IndirectIndexedBuffer[frame]`.  
Draw: `vkCmdDrawIndexedIndirect`.

#### Stage 3 — GPU-driven (active when `m_EnableGpuCulling = true`)
1. CPU writes `GpuInstanceData[]` to `m_InstanceBuffer[frame]`.
2. Compute dispatch `instance_cull_multigeo.comp` (workgroup 64):
    - Reads: GPUScene bounds buffer, camera frustum, per-geometry index counts.
    - Writes: `VkDrawIndexedIndirectCommand[]` into `m_Stage3IndirectPacked[frame]`, visibility remap into `m_Stage3VisibilityPacked[frame]`, draw count into `m_Stage3DrawCountsPacked[frame]`.
3. Draw: `vkCmdDrawIndexedIndirectCount`.

**Descriptor sets (surface.vert/frag):**
| Set | Binding | Content |
|---|---|---|
| 0 | 0 | `CameraBufferObject` UBO (view/proj + lighting) |
| 1 | 0 | Bindless texture array (all loaded textures) |
| 2 | 0 | `GpuInstanceData[]` SSBO |
| 3 | 0 | `GpuMaterialData[]` SSBO |

**Push constants (MeshPushConstants, 120 bytes):**
`Model (mat4)` · `PtrPositions (BDA)` · `PtrNormals (BDA)` · `PtrFaceAttr (BDA)` · `PtrVertexAttr (BDA)` · `PtrIndices (BDA)` · `PtrCentroids (BDA)` · `VisibilityBase (u32)` · `MaterialSlot (u32)`

**Transient debug triangles:** A separate `AddDebugTrianglePass` uploads the `DebugDraw::Triangle()` accumulator via host-visible BDA buffers and renders with `kPipeline_DebugSurface` (alpha blend, no depth write).

---

### Pass 4 — CompositionPass *(deferred path only)*
**Shader:** `deferred_lighting.frag`  
**Reads:** `SceneNormal`, `Albedo`, `Material0`, `SceneDepth`  
**Writes:** `SceneColorHDR`  
**What:** Fullscreen triangle lighting pass. Reconstructs world position from depth, reads G-buffer, evaluates the directional light + ambient from push constants that mirror the CameraBufferObject layout.

---

### Pass 5 — LinePass
**Shaders:** `line.vert/frag`  
**Reads:** `SceneDepth` (depth-tested variant), no depth (overlay variant)  
**Writes:** `SceneColorHDR`

Two sub-pipelines: `m_Pipeline` (depth on) and `m_OverlayPipeline` (depth off, for editor overlays like contact normals).

**Retained draws** (from `[]LineDrawPacket`, filtered by `CulledDrawList::VisibleLineIndices`):
- `BDA PtrPositions` from shared vertex buffer
- `BDA PtrEdges` from edge index buffer
- `BDA PtrEdgeAttr` from persistent per-entity `m_EdgeAttrBuffers` (packed ABGR per edge)

**Transient draws** (from `DebugDrawLines`/`DebugDrawOverlayLines`):
- Each frame uploads flat `[start0,end0,start1,end1,…]` vec3 array to `m_TransientPosBuffer[frame]`
- Identity edge pair buffer (pre-allocated `{0,1},{2,3},…`)
- Optional per-segment color buffer `m_TransientColorBuffer[frame]`

**Push constants (104 bytes):** `Model (mat4)` · `PtrPositions (BDA)` · `PtrEdges (BDA)` · `PtrEdgeAttr (BDA)` · `LineWidth (f32)` · `Color (vec4)`

---

### Pass 6 — PointPass
**Shaders:** `point_flatdisc.vert/frag` (mode 0), `point_surfel.vert/frag` (mode 1/EWA), `point_sphere.vert/frag` (mode 3)  
**Writes:** `SceneColorHDR`, depth-tests against `SceneDepth`  
**Depth bias:** `(-2.0, -2.0)` to prevent z-fighting with surface.

Four per-mode pipelines. Each vertex expands a single position into a camera-facing quad (or surfel-oriented quad). Surfel/EWA mode uses the normal for orientation and EWA covariance for anisotropic splatting. Sphere mode adds analytical per-fragment normal.

**Push constants (120 bytes):** `Model (mat4)` · `PtrPositions (BDA)` · `PtrNormals (BDA)` · `PtrAttr (BDA)` · `PointSize (f32)` · `SizeMultiplier (f32)` · `Viewport (vec2)` · `Color (vec4)` · `Flags (u32)`

Retained and transient paths same pattern as LinePass.

---

### Pass 7 — PostProcessPass
File: `Graphics.Passes.PostProcess.cppm`

Orchestrates five **sub-passes** in sequence:

```
SceneColorHDR
    │
    ▼
BloomSubPass           post_bloom_downsample.frag × N mips
    │                  post_bloom_upsample.frag × N mips
    ▼
ToneMapSubPass         post_tonemap.frag (ACES filmic, or Reinhard)
    │                  ← reads SceneColorHDR + bloom upsampled chain
    ▼                  → writes SceneColorLDR
FXAASubPass            post_fxaa.frag (reads SceneColorLDR)
  or
SMAASubPass            post_smaa_edge.frag → post_smaa_blend.frag → post_smaa_resolve.frag
    │
    ▼
HistogramSubPass       post_histogram.comp (compute, 128-bucket luminance histogram)
                       → async readback for auto-exposure/UI display
```

All read `SceneColorHDR` from the render blackboard. Final output lands in `SceneColorLDR` (swapchain format).

---

### Pass 8 — SelectionOutlinePass
**Shader:** `selection_outline.frag` (fullscreen triangle)  
**Reads:** `EntityId` (R32_UINT sampled image)  
**Writes:** Presentation target (`SceneColorLDR` or `Backbuffer`)  
**What:** Jump-flood or Sobel-style edge detect on the EntityId image. Pixels whose 8-neighbourhood contains a selected PickID get colored with the outline color. Hovered entity gets a distinct color. Thickness and colors are configurable via `SelectionOutlineSettings`.

---

### Pass 9 — DebugViewPass *(optional)*
**Shader:** `debug_view.frag` / `debug_view.comp`  
**Reads:** Selected render graph image (any of `EntityId`, `PrimitiveId`, `SceneNormal`, `Albedo`, `SceneDepth`, etc.)  
**Writes:** Presentation target  
**What:** Fullscreen blit with per-format decode (UINT visualized as hue, depth remapped by near/far, normal packed as RGB). An ImGui preview panel also renders a scaled-down version via a separate `m_PreviewImages[]` array.

---

### Pass 10 — ImGuiPass
**What:** Calls `Interface::GUI::Render(cmd)` which calls `ImGui::Render()` + `ImGui_ImplVulkan_RenderDrawData()`. Skipped entirely if `EditorOverlay.HasDrawData = false`. Renders onto the presentation target with `LOAD` (preserves previous passes).

---

## 8. RenderGraph Compile + Execute

File: `Graphics.RenderGraph.cppm`

### Compile phase (`RenderGraph::Compile(frameIndex)`)
1. **`ResolveTransientResources()`** — for each non-imported resource, find or allocate from `m_ImagePool`/`m_BufferPool` using format+size+usage as the key. Resources live only within their `[StartPass, EndPass]` interval → memory aliasing.
2. **`BuildSchedulerGraph()`** — builds a DAG: pass A → pass B when B reads a resource written by A. Feeds `Core::DAGScheduler` for topological sort → execution layers.
3. **`CalculateBarriers()`** — walks each pass's access list in execution order. Computes `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` deltas (srcStage/srcAccess/oldLayout → dstStage/dstAccess/newLayout). Stored as `span<>` in arena per pass.
4. **`Packetize()`** — merges consecutive passes in the same execution layer into `ExecutionPacket`s if they share the same render attachments (or are both non-raster). Packets map 1:1 to secondary command buffers → fewer `vkBeginCommandBuffer` calls.
5. **PostCompile callbacks** — `SelectionOutlinePass`, `DebugViewPass`, `CompositionPass`, `PostProcessPass` update their descriptor sets to point at the newly resolved `VkImageView` handles.

### Execute phase (`RenderGraph::Execute(cmd)`)
For each execution layer (can be parallelized in future):
- For each `ExecutionPacket` in the layer:
    - Record into a secondary command buffer (inherited rendering state for raster packets)
    - For each pass in the packet:
        1. Emit barriers (`vkCmdPipelineBarrier2`)
        2. If raster: `vkCmdBeginRendering` (dynamic rendering, Vulkan 1.3)
        3. Call `pass.ExecuteFn(userData, registry, cmd)` — the lambda registered via `AddPass<>()`
        4. If raster: `vkCmdEndRendering`
    - `vkEndCommandBuffer` (secondary)
    - `vkCmdExecuteCommands` on primary

---

## 9. Render Resources (Blackboard)

All passes communicate via `RenderBlackboard` (name→`RGResourceHandle` map).

| Resource Name | Format | Lifetime | Producer | Consumer(s) |
|---|---|---|---|---|
| `Backbuffer` | swapchain | Imported | PresentationSystem | ImGui, SelectionOutline, Present.LDR |
| `SceneDepth` | D32_SFLOAT | Imported | PresentationSystem | Picking, Surface, Line, Point |
| `EntityId` | R32_UINT | Transient | PickingPass | SelectionOutline, DebugView |
| `PrimitiveId` | R32_UINT | Transient | PickingPass | (readback copy pass) |
| `SceneNormal` | R16G16B16A16_SFLOAT | Transient | SurfacePass (GBuffer) | CompositionPass |
| `Albedo` | R8G8B8A8_UNORM | Transient | SurfacePass (GBuffer) | CompositionPass |
| `Material0` | R16G16B16A16_SFLOAT | Transient | SurfacePass (GBuffer) | CompositionPass |
| `SceneColorHDR` | R16G16B16A16_SFLOAT | Transient | SurfacePass / CompositionPass | Line, Point, PostProcess |
| `SceneColorLDR` | swapchain | Transient | ToneMapSubPass | FXAA/SMAA, SelectionOutline, DebugView, ImGui |
| `SelectionMask` | R8_UNORM | Transient | (future) | SelectionOutlinePass |
| `ShadowAtlas` | D16_UNORM | Transient | ShadowPass | SurfacePass (sampler) |

---

## 10. Key GPU Buffers

| Buffer | Owned by | Updated by | Consumed by |
|---|---|---|---|
| Camera UBO (dynamic, 3 slots) | `GlobalResources` | `GlobalResources::Update()` | All passes (set=0, binding=0) |
| GPUScene instances SSBO | `GPUScene` | `GPUScene::Sync()` via `scene_update.comp` | `instance_cull*.comp`, `surface.vert` |
| GPUScene bounds SSBO | `GPUScene` | same | `instance_cull*.comp` |
| Instance SSBO (per-frame) | `SurfacePass` | CPU each frame | `surface.vert` (set=2) |
| Material SSBO | `MaterialRegistry` | `SyncGpuBuffer()` | `surface.frag` (set=3) |
| Indirect draw buffer | `SurfacePass` | Stage2: CPU / Stage3: `instance_cull_multigeo.comp` | `vkCmdDrawIndexedIndirectCount` |
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
| `line.vert/frag` | (built lazily) | LinePass | LinePass |
| `point_flatdisc.vert/frag` | (built lazily) | PointPass | PointPass mode 0 |
| `point_surfel.vert/frag` | (built lazily) | PointPass | PointPass mode 1/EWA |
| `point_sphere.vert/frag` | (built lazily) | PointPass | PointPass mode 3 |
| `scene_update.comp` | (compute) | PipelineLibrary | GPUScene::Sync |
| `instance_cull_multigeo.comp` | (compute) | PipelineLibrary | SurfacePass Stage 3 |
| `deferred_lighting.frag` | (built lazily) | CompositionPass | CompositionPass |
| `post_tonemap.frag` | (built lazily) | ToneMapSubPass | PostProcessPass |
| `post_bloom_*.frag` | (built lazily) | BloomSubPass | PostProcessPass |
| `post_fxaa.frag` | (built lazily) | FXAASubPass | PostProcessPass |
| `post_smaa_*.frag` | (built lazily) | SMAASubPass | PostProcessPass |
| `post_histogram.comp` | (compute, built lazily) | HistogramSubPass | PostProcessPass |
| `selection_outline.frag` | (built lazily) | SelectionOutlinePass | SelectionOutlinePass |
| `debug_view.frag` | (built lazily) | DebugViewPass | DebugViewPass |

---

## 12. Full Frame Data-Flow Diagram

```
[GLFW input] ──────────────────────────────────────────────────────────────── ActivityTracker
                                                                                    │
ECS Scene (entt::registry)                                                          │
    │                                                                               │
    ▼  PropertySetDirtySync → MeshRendererLifecycle                                 │
    │  MeshViewLifecycle → GraphLifecycle → PointCloudLifecycle                     │
    │  GPUSceneSync                                                                 │
    │  PrimitiveBVHBuild                                                            │
    │                                                                               │
    ▼  DispatchDeferredEvents (entt::dispatcher)                                    │
    │                                                                               │
    ▼  OnRender(alpha)  ──► DebugDraw accumulator (lines, points, triangles)        │
    │                                                                               │
    ▼  PrepareEditorOverlay                                                         │
    │    └─ ImGui::NewFrame → all panels + gizmo → EditorOverlayPacket              │
    │                                                                               ▼
    ▼  ExtractRenderWorld (snapshot, immutable)                                FramePacing
    │    └─ RenderWorld {View, Lighting, Surface/Line/Point packets,           (idle sleep)
    │        DebugDraw snapshots, Pick request, DebugView, GpuScene}
    │
    ▼  BeginFrame (GPU timeline wait, deferred deletion flush)
    │
    ▼  vkAcquireNextImageKHR
    │
    ▼  ProcessCompletedGpuWork (pick readback, GPU profiling)
    │
    ▼  GlobalResources::Update (upload CameraBufferObject)
    │
    ▼  GPUScene::Sync → scene_update.comp (scatter instance updates)
    │
    ▼  MaterialRegistry::SyncGpuBuffer (upload material SSBO)
    │
    ▼  ResolveDrawPacketBounds + CullDrawPackets (CPU frustum cull lines/points)
    │
    ▼  RenderGraph::AddPass<> × N  (DefaultPipeline::SetupFrame)
    │
    ▼  RenderGraph::Compile
    │    ├─ ResolveTransientResources (allocate/pool images+buffers)
    │    ├─ BuildSchedulerGraph (DAG topology sort)
    │    ├─ CalculateBarriers (Vulkan Sync2 barriers)
    │    ├─ Packetize (merge into secondary cmd buf groups)
    │    └─ PostCompile (update descriptor sets with final image views)
    │
    ▼  RenderGraph::Execute(primaryCmd)
    │    ├─ [Layer 0] PickingPass (EntityId + PrimitiveId MRT) + Copy to readback
    │    ├─ [Layer 1] ShadowPass (optional)
    │    ├─ [Layer 2] SurfacePass
    │    │    ├─ DepthPrepass (optional)
    │    │    ├─ instance_cull_multigeo.comp (GPU culling → indirect draws)
    │    │    ├─ surface.vert/frag  (forward) or surface_gbuffer.frag (deferred)
    │    │    └─ debug_surface (transient triangles, alpha blend)
    │    ├─ [Layer 3] CompositionPass (deferred only: deferred_lighting.frag)
    │    ├─ [Layer 4] LinePass  (line.vert/frag, BDA push constants)
    │    ├─ [Layer 5] PointPass (point_*.vert/frag, BDA push constants)
    │    ├─ [Layer 6] PostProcessPass
    │    │    ├─ Bloom (downsample + upsample chain)
    │    │    ├─ ToneMap (ACES + bloom add → SceneColorLDR)
    │    │    ├─ FXAA or SMAA (anti-aliasing on LDR)
    │    │    └─ Histogram (compute, async readback)
    │    ├─ [Layer 7] SelectionOutlinePass (Sobel on EntityId → tinted overlay)
    │    ├─ [Layer 8] DebugViewPass (optional, fullscreen buffer visualizer)
    │    └─ [Layer 9] ImGuiPass (ImGui_ImplVulkan_RenderDrawData)
    │
    ▼  vkQueueSubmit (graphics queue, timeline semaphore signal)
    │
    ▼  vkQueuePresentKHR
```
