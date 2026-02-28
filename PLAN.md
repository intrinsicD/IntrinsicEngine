# Rendering Architecture Refactor — Plan

## Goal

Replace the dual-path (transient CPU + retained GPU) rendering with a **single unified path per primitive type**. Each visualization is one self-contained pass: own pipeline, own shaders, own ECS query. No routing logic. Adding a new rendering method = new shader + pipeline variant + register in DefaultPipeline.

---

## Architecture: Three Primitive Passes + Debug Overlay

| Pass | Primitives | Data Source | Modes |
|------|-----------|-------------|-------|
| **SurfacePass** | Filled triangles | BDA from `GeometryGpuData` | Vertex colors, face colors, textured |
| **LinePass** | Thick anti-aliased edges | BDA positions + edge index SSBO | Uniform color, per-vertex interpolated, per-edge discrete |
| **PointPass** | Expanded billboard quads | BDA positions + normals | FlatDisc, Surfel, (future: EWA, Gaussian) |
| **DebugLinePass** | Thin overlay lines | Transient SSBO from DebugDraw | Uniform color (debug only) |

Each retained pass reads vertex data via BDA from the **shared device-local vertex buffer** uploaded once by `GeometryGpuData`. No CPU-side position transforms. No per-frame SSBO uploads for mesh/graph data.

`DebugLinePass` is the **only** transient path, reserved exclusively for ephemeral debug overlays (octree bounds, contact manifolds, convex hulls). It is not used for mesh wireframe or graph edges.

---

## Naming Convention

Everything that belongs together shares a prefix. Files, classes, shaders, registry IDs all follow the same pattern.

### Passes (C++)

| Current | New | Module Partition |
|---------|-----|-----------------|
| `ForwardPass` | `SurfacePass` | `Graphics:Passes.Surface` |
| `RetainedLineRenderPass` | `LinePass` | `Graphics:Passes.Line` |
| `RetainedPointCloudRenderPass` | `PointPass` | `Graphics:Passes.Point` |
| `LineRenderPass` | `DebugLinePass` | `Graphics:Passes.DebugLine` |
| `MeshRenderPass` | **deleted** | — |
| `PointCloudRenderPass` | **deleted** | — |
| `GraphRenderPass` | **deleted** (logic absorbed) | — |

### Shaders

| Current | New | Purpose |
|---------|-----|---------|
| `triangle.vert/frag` | `surface.vert/frag` | Mesh surface rendering |
| `line_retained.vert/frag` | `line.vert/frag` | Thick edge rendering (BDA) |
| `point_retained.vert/frag` | `point.vert/frag` | Point/vertex rendering (BDA) |
| `line.vert/frag` | `debug_line.vert/frag` | Debug overlay lines (transient SSBO) |
| `point.vert/frag` | **deleted** | Was transient SSBO point path |

### ShaderRegistry IDs

| Pass | Vertex | Fragment |
|------|--------|----------|
| SurfacePass | `"Surface.Vert"_id` | `"Surface.Frag"_id` |
| LinePass | `"Line.Vert"_id` | `"Line.Frag"_id` |
| PointPass | `"Point.Vert"_id` | `"Point.Frag"_id` |
| DebugLinePass | `"DebugLine.Vert"_id` | `"DebugLine.Frag"_id` |

### FeatureRegistry IDs

| Pass | ID |
|------|----|
| SurfacePass | `"SurfacePass"_id` |
| LinePass | `"LinePass"_id` |
| PointPass | `"PointPass"_id` |
| DebugLinePass | `"DebugLinePass"_id` |
| PickingPass | `"PickingPass"_id` (unchanged) |
| SelectionOutlinePass | `"SelectionOutlinePass"_id` (unchanged) |
| ImGuiPass | `"ImGuiPass"_id` (unchanged) |

### Source Files

```
src/Runtime/Graphics/Passes/
  Graphics.Passes.Surface.cppm      # interface
  Graphics.Passes.Surface.cpp       # implementation
  Graphics.Passes.Line.cppm
  Graphics.Passes.Line.cpp
  Graphics.Passes.Point.cppm
  Graphics.Passes.Point.cpp
  Graphics.Passes.DebugLine.cppm
  Graphics.Passes.DebugLine.cpp
  Graphics.Passes.Picking.cppm      # unchanged
  Graphics.Passes.Picking.cpp
  Graphics.Passes.SelectionOutline.cppm  # unchanged
  Graphics.Passes.SelectionOutline.cpp
  Graphics.Passes.DebugView.cppm    # unchanged
  Graphics.Passes.DebugView.cpp
  Graphics.Passes.ImGui.cppm        # unchanged
  Graphics.Passes.ImGui.cpp

assets/shaders/
  surface.vert / surface.frag
  line.vert / line.frag
  point.vert / point.frag
  debug_line.vert / debug_line.frag
  picking.vert / picking.frag       # unchanged
```

### Deleted Files

```
# Passes (dual-path routing removed)
Graphics.Passes.Mesh.cppm / .cpp
Graphics.Passes.Graph.cppm / .cpp
Graphics.Passes.PointCloud.cppm / .cpp
Graphics.Passes.RetainedLine.cppm / .cpp
Graphics.Passes.RetainedPointCloud.cppm / .cpp

# Shaders (transient SSBO paths removed)
point.vert / point.frag             # was transient SSBO points
line.vert / line.frag               # renamed to debug_line.*
triangle.vert / triangle.frag       # renamed to surface.*
line_retained.vert / line_retained.frag    # renamed to line.*
point_retained.vert / point_retained.frag  # renamed to point.*
```

---

## Pass Design Details

### 1. SurfacePass

**Replaces:** `ForwardPass` (renamed, same GPU-driven culling architecture).

**What changes:** Name only (ForwardPass → SurfacePass). The GPU-driven culling pipeline, instance SSBO, compute cull shader, bindless texture array — all stay as-is. The shader is renamed `triangle.* → surface.*` but the code is identical.

**Why keep it simple:** The surface pass already works correctly. Renaming it for consistency is all that's needed now. The rendering modes (vertex color, face color, textured) are a shader/material concern that can evolve independently.

**ECS query:** `MeshRenderer::Component` + `RenderVisualization::Component` (ShowSurface=true).

### 2. LinePass

**Replaces:** `RetainedLineRenderPass` + the wireframe part of `MeshRenderPass` + the edge part of `GraphRenderPass`.

**Architecture:**
- Self-contained: iterates all entities that need edge rendering
- Sources: mesh wireframe (from `RenderVisualization::ShowWireframe`) AND graph edges (from `GraphRenderer::Component`)
- Reads positions from shared vertex buffer via BDA
- Edge indices uploaded to per-frame SSBO (same as current RetainedLineRenderPass)
- Vertex shader expands each edge to a screen-space quad (6 verts/segment)
- Anti-aliased fragment shader

**Edge caching moves into LinePass:**
The edge extraction logic (currently in `MeshRenderPass` lines 129-167) moves into `LinePass::AddPasses()`. The pass lazily builds `CachedEdges` from the collision mesh when `EdgeCacheDirty=true`, then uploads to the edge SSBO.

**Graph edges:**
`LinePass` also iterates `GraphRenderer::Component` entities. Graph edges are index pairs into the node position array. Since graph nodes are uploaded as a `GeometryGpuData` (device-local vertex buffer), graph edges work the same way as mesh edges: BDA position pointer + edge index SSBO.

**This requires:** Graph node positions get their own `GeometryHandle` / `GeometryGpuData`. This is the prerequisite for making graph edges retained BDA. The graph layout algorithm produces positions; these get uploaded once to a device-local buffer. On layout change, the buffer is re-uploaded.

**Push constants:**
```cpp
struct LinePushConstants {
    glm::mat4 Model;           // 64 bytes
    uint64_t  PtrPositions;    //  8 bytes
    float     LineWidth;       //  4 bytes
    float     ViewportWidth;   //  4 bytes
    float     ViewportHeight;  //  4 bytes
    uint32_t  Color;           //  4 bytes  (uniform fallback)
    uint32_t  ColorMode;       //  4 bytes  (0=uniform, 1=per-vertex, 2=per-edge)
    uint32_t  _pad;            //  4 bytes
};  // 96 bytes
```

**Color modes** (future, initial implementation uses uniform color only):
- `0 = Uniform`: single color from push constants (current behavior)
- `1 = PerVertex`: color interpolated from endpoint vertex colors (requires aux buffer)
- `2 = PerEdge`: each edge has its own color (requires color data in edge SSBO)

### 3. PointPass

**Replaces:** `RetainedPointCloudRenderPass` + the vertex part of `MeshRenderPass` + the node part of `GraphRenderPass` + `PointCloudRenderPass`.

**Architecture:**
- Self-contained: iterates all entities that need point rendering
- Sources:
  - Mesh vertices (`MeshRenderer` + `RenderVisualization::ShowVertices`)
  - Graph nodes (`GraphRenderer::Component`)
  - Standalone point clouds (`PointCloudRenderer::Component`)
- ALL sources use BDA from device-local vertex buffers
- Vertex shader expands each point to a billboard quad (6 verts)
- Fragment shader renders circular splats with mode-dependent shading

**Standalone point clouds get device-local upload:**
`PointCloudRenderer::Component` entities get a `GeometryHandle` pointing to `GeometryGpuData`. Positions/normals are uploaded once to device-local memory. On data change, the buffer is re-uploaded. This makes standalone point clouds identical to mesh-derived points from the pass's perspective — just another entity with BDA pointers.

**Rendering modes — separate pipelines, NOT mega-shader branching:**

Each rendering mode gets its own pipeline with its own vertex/fragment shaders:

```
point_flatdisc.vert / point_flatdisc.frag   — camera-facing billboard
point_surfel.vert   / point_surfel.frag     — normal-oriented disc
point_ewa.vert      / point_ewa.frag        — EWA splatting (future)
point_gaussian.vert / point_gaussian.frag   — Gaussian splatting (future)
```

**Why separate pipelines instead of push constant RenderMode:**
- FlatDisc, Surfel, EWA, and Gaussian splatting have fundamentally different vertex expansion and fragment shading logic
- A mega-shader with `if (mode == 0) ... else if (mode == 1) ...` branches wastes GPU cycles and becomes unmaintainable
- Separate pipelines allow per-mode optimizations (e.g., Gaussian splatting needs different blending state)
- Extensibility: adding a new mode = new shader pair + new pipeline entry. Zero changes to existing shaders.

**PointPass stores a pipeline per mode:**
```cpp
class PointPass final : public IRenderFeature {
    // One pipeline per active rendering mode.
    std::unique_ptr<RHI::GraphicsPipeline> m_Pipelines[kModeCount];
    // ...
};
```

The pass groups entities by mode, then issues one draw batch per mode with the correct pipeline bound.

**Push constants (shared across all point modes):**
```cpp
struct PointPushConstants {
    glm::mat4 Model;           // 64 bytes
    uint64_t  PtrPositions;    //  8 bytes
    uint64_t  PtrNormals;      //  8 bytes
    uint64_t  PtrAux;          //  8 bytes  (UVs for textured mode)
    float     PointSize;       //  4 bytes
    float     SizeMultiplier;  //  4 bytes
    float     ViewportWidth;   //  4 bytes
    float     ViewportHeight;  //  4 bytes
    uint32_t  Color;           //  4 bytes  (uniform fallback)
    uint32_t  ColorMode;       //  4 bytes  (0=uniform, 1=per-vertex, 2=textured)
    uint32_t  TextureID;       //  4 bytes  (bindless texture index for textured mode)
    uint32_t  _pad;            //  4 bytes
};  // 128 bytes (within Vulkan push constant limit)
```

### 4. DebugLinePass

**Replaces:** `LineRenderPass` (renamed, same transient SSBO architecture).

**What changes:** Renamed from `LineRenderPass` → `DebugLinePass`. The `line.vert/frag` shaders are renamed to `debug_line.vert/frag`. Internal logic is unchanged.

**Scope:** ONLY renders `DebugDraw` content (octree bounds, contact manifolds, convex hull overlays, kNN graph debug, etc.). Never mesh wireframe. Never graph edges.

---

## Pipeline Execution Order

```
DefaultPipeline::RebuildPath()
│
├── 1. PickingPass           — entity/primitive ID readback
├── 2. SurfacePass           — filled triangles (GPU-driven culling)
├── 3. LinePass              — thick wireframe + graph edges (BDA)
├── 4. PointPass             — vertex/node/point cloud splats (BDA)
├── 5. SelectionOutlinePass  — post-process selection overlay
├── 6. DebugLinePass         — transient debug overlays
├── 7. DebugViewPass         — render target inspector
└── 8. ImGuiPass             — editor UI
```

No VisualizationCollect composite stage. No collector passes feeding shared staging buffers. Each pass is a self-contained stage that queries ECS, gathers data, and draws.

---

## Shared Data Contract

All three retained passes (SurfacePass, LinePass, PointPass) share vertex data via **BDA pointers into the same device-local vertex buffer**.

**Contract:**
1. `GeometryGpuData` owns the vertex buffer (`std::shared_ptr<VulkanBuffer>`)
2. Multiple views can share the same vertex buffer via `ReuseVertexBuffersFrom`
3. Each pass reads `GetVertexBuffer()->GetDeviceAddress() + layout.PositionsOffset` etc.
4. The vertex buffer is immutable after upload (no per-frame writes)
5. Buffer lifetime is managed by `GeometryPool` with retirement frames matching frames-in-flight

**Who uploads what:**
- Mesh entity: `ModelLoader` uploads positions/normals/aux + triangle indices → `GeometryGpuData`
- Graph entity: layout algorithm uploads node positions → `GeometryGpuData` (new)
- Point cloud entity: `PointCloudRenderer` uploads positions/normals → `GeometryGpuData` (new)

Once uploaded, all three passes can read from the same buffer. Zero duplication.

---

## ECS Component Changes

### `RenderVisualization::Component` — simplified

```cpp
struct Component {
    // Mode toggles
    bool ShowSurface   = true;
    bool ShowWireframe = false;
    bool ShowVertices  = false;

    // Wireframe settings
    glm::vec4 WireframeColor = {0.85f, 0.85f, 0.85f, 1.0f};
    float     WireframeWidth = 1.5f;
    bool      WireframeOverlay = false;

    // Vertex settings
    glm::vec4 VertexColor = {1.0f, 0.6f, 0.0f, 1.0f};
    float     VertexSize  = 0.008f;
    PointRenderMode VertexRenderMode = PointRenderMode::FlatDisc;

    // Edge cache (populated by LinePass, lazily)
    std::vector<EdgePair> CachedEdges;
    bool EdgeCacheDirty = true;

    // Sync state
    bool CachedShowSurface = true;

    // DELETED: CachedVertexNormals, VertexNormalsDirty (normals from GPU buffer)
    // DELETED: VertexView, VertexViewDirty (no separate ForwardPass point-list path)
};
```

### `PointRenderMode` enum — moved to top-level, extensible

```cpp
// In Graphics:Passes.Point or a shared header
enum class PointRenderMode : uint32_t {
    FlatDisc  = 0,
    Surfel    = 1,
    // Future:
    // EWA       = 2,
    // Gaussian  = 3,
};
```

### `GraphRenderer::Component` — gains GeometryHandle

```cpp
struct Component {
    // Node data
    std::vector<glm::vec3> NodePositions;
    std::vector<glm::vec4> NodeColors;
    std::vector<float>     NodeRadii;

    // Edge data (index pairs into NodePositions)
    std::vector<std::pair<uint32_t, uint32_t>> Edges;

    // GPU geometry (device-local, uploaded on layout change)
    Geometry::GeometryHandle NodeGeometry{};   // NEW — positions/normals in device-local buffer
    bool GeometryDirty = true;                 // NEW — triggers re-upload

    // Rendering parameters
    PointRenderMode NodeRenderMode = PointRenderMode::FlatDisc;
    float     DefaultNodeRadius  = 0.01f;
    // ... (rest unchanged)
};
```

### `PointCloudRenderer::Component` — gains GeometryHandle

```cpp
struct Component {
    // Point cloud data
    std::vector<glm::vec3> Positions;
    std::vector<glm::vec3> Normals;
    std::vector<glm::vec4> Colors;
    std::vector<float>     Radii;

    // GPU geometry (device-local, uploaded once)
    Geometry::GeometryHandle PointGeometry{};  // NEW
    bool GeometryDirty = true;                 // NEW

    // Rendering parameters
    PointRenderMode RenderMode = PointRenderMode::FlatDisc;
    // ... (rest unchanged)
};
```

### `GeometryViewRenderer::Component` — simplified

```cpp
struct Component {
    Geometry::GeometryHandle Surface{};
    uint32_t SurfaceGpuSlot = kInvalidSlot;
    bool ShowSurface = true;

    // DELETED: Vertices handle (PointPass reads from same buffer via BDA directly)
    // DELETED: VerticesGpuSlot
    // DELETED: ShowVertices (lives in RenderVisualization)
};
```

---

## Extensibility: Adding a New Point Mode (e.g., Gaussian Splatting)

1. **Add enum value:**
   ```cpp
   enum class PointRenderMode : uint32_t {
       FlatDisc = 0, Surfel = 1, EWA = 2, Gaussian = 3,
   };
   ```

2. **Write shaders:** `point_gaussian.vert` / `point_gaussian.frag`

3. **Register shaders:** Add to `CompileShaders.cmake` glob (auto-discovered) and `ShaderRegistry`

4. **Add pipeline in PointPass:**
   ```cpp
   // In PointPass::BuildPipelines() — one pipeline per mode
   m_Pipelines[Gaussian] = BuildPipeline(colorFmt, depthFmt, "Point.Gaussian.Vert", "Point.Gaussian.Frag",
                                          /* custom blend state for Gaussian */);
   ```

5. **Expose in UI:** Add to the render mode combo in Inspector

That's it. No routing changes. No new passes. No new components. The PointPass already groups entities by mode and draws each batch with the correct pipeline.

---

## Migration Order (Implementation Steps)

### Phase 1: Rename and consolidate (no behavior change)

1. Rename `ForwardPass` → `SurfacePass` (class, files, shaders, registry IDs)
2. Rename `RetainedLineRenderPass` → `LinePass` (class, files, shaders, registry IDs)
3. Rename `RetainedPointCloudRenderPass` → `PointPass` (class, files, shaders, registry IDs)
4. Rename `LineRenderPass` → `DebugLinePass` (class, files, shaders, registry IDs)
5. Update `DefaultPipeline`, `FeatureRegistry`, `ShaderRegistry`, `CMakeLists.txt`
6. Update all imports/references
7. Build + verify tests pass

### Phase 2: Make LinePass self-contained

8. Move edge caching logic from `MeshRenderPass` into `LinePass::AddPasses()`
9. Add graph edge iteration to `LinePass` (iterate `GraphRenderer` entities)
10. Remove wireframe submission from `MeshRenderPass` → DebugDraw
11. Remove `MeshRenderPass::SetRetainedPassesActive()` and all routing booleans
12. Remove graph edge submission from `GraphRenderPass` → DebugDraw

### Phase 3: Make PointPass self-contained

13. Add graph node iteration to `PointPass` (iterate `GraphRenderer` entities with `NodeGeometry`)
14. Add `PointCloudRenderer` iteration to `PointPass` (iterate entities with `PointGeometry`)
15. Add device-local upload for `GraphRenderer::NodeGeometry` (on layout change)
16. Add device-local upload for `PointCloudRenderer::PointGeometry` (on data change)
17. Remove vertex submission from `MeshRenderPass` → `PointCloudRenderPass`
18. Remove node submission from `GraphRenderPass` → `PointCloudRenderPass`
19. Remove inline `PointCloudRenderer` collection from `DefaultPipeline`

### Phase 4: Delete dead code

20. Delete `MeshRenderPass` (class, files, module partition)
21. Delete `GraphRenderPass` (class, files, module partition)
22. Delete `PointCloudRenderPass` (class, files, module partition, shaders)
23. Delete old transient shader files (`point.vert/frag` for SSBO path)
24. Clean up `RenderVisualization::Component` (remove `CachedVertexNormals`, `VertexView`, etc.)
25. Clean up `GeometryViewRenderer::Component` (remove `Vertices` handle)
26. Remove `VisualizationCollect` composite stage from `DefaultPipeline::RebuildPath()`

### Phase 5: Pipeline-per-mode for PointPass

27. Split `point.vert/frag` into `point_flatdisc.vert/frag` and `point_surfel.vert/frag`
28. PointPass stores pipeline array indexed by `PointRenderMode`
29. Group entities by mode, draw each batch with correct pipeline
30. Add `PointRenderMode` UI selector (ImGui combo in Inspector)

### Phase 6: Update docs

31. Update `TODO.md` — remove completed items, add new remaining work
32. Update `CLAUDE.md` — document new pass naming and architecture
33. Update `README.md` — update rendering architecture description

---

## What Does NOT Change

- `PickingPass` — unchanged
- `SelectionOutlinePass` — unchanged
- `DebugViewPass` — unchanged
- `ImGuiPass` — unchanged
- `GeometryGpuData` / `GeometryPool` / `GeometryUploadRequest` — unchanged
- `RHI` layer — unchanged
- `RenderGraph` — unchanged
- `GPUScene` / `GPUSceneSync` — unchanged (SurfacePass inherits ForwardPass's culling)
- `PipelineLibrary` — unchanged (SurfacePass uses same compiled pipelines)
- `DebugDraw` accumulator — unchanged (still feeds DebugLinePass)
- All geometry algorithms — unchanged
- All test targets — unchanged (but tests may need updated imports)
