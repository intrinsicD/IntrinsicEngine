# Rendering Architecture Refactor — Plan

## Goal

Replace the dual-path (transient CPU + retained GPU) rendering with a **single unified path per primitive type**. Each visualization is one self-contained pass with its own pipeline, shaders, ECS component, and both retained + transient data paths internally. No routing logic between passes. Adding a new rendering method = new shader + pipeline variant + register in DefaultPipeline.

---

## Architecture: Three Primitive Passes

| Pass | Primitives | Retained Data | Transient Data | Modes |
|------|-----------|---------------|----------------|-------|
| **SurfacePass** | Filled triangles | BDA from `GeometryGpuData` | `SubmitTriangles()` | Vertex colors, face colors, textured |
| **LinePass** | Thick anti-aliased edges | BDA positions + edge index SSBO | `SubmitLine(a, b, color)` | Uniform color, per-vertex, per-edge |
| **PointPass** | Expanded billboard quads | BDA positions + normals | `SubmitPoint(pos, normal, size, color)` | FlatDisc, Surfel, (future: EWA, Gaussian) |

**No separate DebugLinePass.** Each pass handles both retained and transient data internally. The rendering technique is identical — the data lifetime is an internal detail, not a separate pass.

- **Retained path**: device-local vertex buffer uploaded once, read via BDA every frame. Used for mesh geometry, graph geometry, point cloud geometry.
- **Transient path**: per-frame CPU submission API (`SubmitTriangles()`, `SubmitLine()`, `SubmitPoint()`). Data packed into a host-visible SSBO each frame. Used for debug overlays (octree bounds, contact manifolds, convex hulls, debug planes/quads, procedural preview geometry).

Same pipeline, same shaders, same fragment output. Two internal data sources, one draw call sequence.

---

## ECS: Per-Pass Typed Components

Each pass owns a dedicated component type. The **toggle is presence/absence of the component** — no boolean flags. Attaching a component enables that visualization, removing it disables it. EnTT is optimized for this pattern.

### Component Types

```cpp
// Owned by SurfacePass — mesh triangle rendering.
namespace ECS::Surface {
    struct Component {
        Geometry::GeometryHandle Geometry{};
        Core::Assets::AssetHandle Material{};

        // GPU-driven culling state (inherited from current MeshRenderer)
        uint32_t GpuSlot = kInvalidSlot;
        Graphics::MaterialHandle CachedMaterialHandle{};
        // ... (same culling/instance cache fields as current MeshRenderer)
    };
}

// Owned by LinePass — edge/wireframe rendering.
namespace ECS::Line {
    struct Component {
        Geometry::GeometryHandle Geometry{};     // shared vertex buffer (BDA)

        // Edge data
        std::vector<EdgePair> CachedEdges;       // index pairs into position buffer
        bool EdgeCacheDirty = true;              // triggers re-extraction from indices

        // Appearance
        glm::vec4 Color = {0.85f, 0.85f, 0.85f, 1.0f};
        float     Width = 1.5f;
        bool      Overlay = false;               // true = no depth test
    };
}

// Owned by PointPass — vertex/node/point cloud rendering.
namespace ECS::Point {
    struct Component {
        Geometry::GeometryHandle Geometry{};     // shared vertex buffer (BDA)

        // Appearance
        glm::vec4 Color = {1.0f, 0.6f, 0.0f, 1.0f};
        float     Size  = 0.008f;               // world-space radius
        PointRenderMode Mode = PointRenderMode::FlatDisc;
    };
}
```

### Entity Composition Examples

```
Mesh entity (Bunny):
  ├─ ECS::Surface::Component  { Geometry=bunnyHandle, Material=pbrMat }
  ├─ ECS::Line::Component     { Geometry=bunnyHandle, Color=white }     ← attached when wireframe enabled
  └─ ECS::Point::Component    { Geometry=bunnyHandle, Mode=Surfel }     ← attached when vertices enabled

Graph entity (kNN graph):
  ├─ ECS::Line::Component     { Geometry=graphHandle, Color=gray }      ← edges
  └─ ECS::Point::Component    { Geometry=graphHandle, Mode=FlatDisc }   ← nodes

Point cloud entity (scan.ply):
  └─ ECS::Point::Component    { Geometry=cloudHandle, Mode=Surfel }

Debug entity (octree visualization):
  └─ (no components — uses LinePass::SubmitLine() transient API directly)
```

**Key insight:** The `Geometry` handle is shared across components on the same entity — same `GeometryGpuData`, same device-local buffer, same BDA. Zero vertex duplication.

### What Gets Replaced

| Current | New |
|---------|-----|
| `ECS::MeshRenderer::Component` | `ECS::Surface::Component` |
| `ECS::RenderVisualization::Component` | Split into `ECS::Line::Component` + `ECS::Point::Component` (attached/detached instead of bool toggles) |
| `ECS::GraphRenderer::Component` | `ECS::Line::Component` + `ECS::Point::Component` (graph-specific data like edges stays, geometry gets a handle) |
| `ECS::PointCloudRenderer::Component` | `ECS::Point::Component` (with device-local GeometryHandle) |
| `ECS::GeometryViewRenderer::Component` | **deleted** (each component carries its own GeometryHandle) |

### Graph-Specific Data

Graphs need edge index pairs and node-specific data (radii, colors) beyond what the generic `ECS::Line::Component` and `ECS::Point::Component` carry. Two options:

**Option A:** Extra graph-specific component alongside the generic ones:
```
Graph entity:
  ├─ ECS::Line::Component   { Geometry=graphHandle }
  ├─ ECS::Point::Component  { Geometry=graphHandle }
  └─ ECS::Graph::Data       { Edges, NodeRadii, NodeColors, ... }
```

**Option B:** The generic components are sufficient — edges go in `Line::Component.CachedEdges`, per-node colors/radii go in the vertex buffer's Aux channel.

Option A is cleaner — the passes don't need to know about graph-specific semantics. `ECS::Graph::Data` is a pure data component that systems use to populate the `Line` and `Point` components.

---

## Naming Convention

Everything that belongs together shares a prefix.

### Passes (C++)

| Current | New | Module Partition |
|---------|-----|-----------------|
| `ForwardPass` | `SurfacePass` | `Graphics:Passes.Surface` |
| `RetainedLineRenderPass` | `LinePass` | `Graphics:Passes.Line` |
| `RetainedPointCloudRenderPass` | `PointPass` | `Graphics:Passes.Point` |
| `MeshRenderPass` | **deleted** | — |
| `GraphRenderPass` | **deleted** | — |
| `PointCloudRenderPass` | **deleted** | — |
| `LineRenderPass` | **deleted** | — |

### Shaders

| Current | New | Purpose |
|---------|-----|---------|
| `triangle.vert/frag` | `surface.vert/frag` | Mesh surface rendering |
| `line_retained.vert/frag` | `line.vert/frag` | Thick edge rendering (retained BDA + transient) |
| `point_retained.vert/frag` | `point_flatdisc.vert/frag` | FlatDisc point rendering |
| *(new)* | `point_surfel.vert/frag` | Surfel point rendering |
| `line.vert/frag` | **deleted** | Was transient-only SSBO line path |
| `point.vert/frag` | **deleted** | Was transient-only SSBO point path |

### ShaderRegistry IDs

| Pass | Mode | Vertex | Fragment |
|------|------|--------|----------|
| SurfacePass | — | `"Surface.Vert"_id` | `"Surface.Frag"_id` |
| LinePass | — | `"Line.Vert"_id` | `"Line.Frag"_id` |
| PointPass | FlatDisc | `"Point.FlatDisc.Vert"_id` | `"Point.FlatDisc.Frag"_id` |
| PointPass | Surfel | `"Point.Surfel.Vert"_id` | `"Point.Surfel.Frag"_id` |
| PointPass | EWA | `"Point.EWA.Vert"_id` | `"Point.EWA.Frag"_id` |

### FeatureRegistry IDs

| Pass | ID |
|------|----|
| SurfacePass | `"SurfacePass"_id` |
| LinePass | `"LinePass"_id` |
| PointPass | `"PointPass"_id` |
| PickingPass | `"PickingPass"_id` (unchanged) |
| SelectionOutlinePass | `"SelectionOutlinePass"_id` (unchanged) |
| ImGuiPass | `"ImGuiPass"_id` (unchanged) |

### Source Files

```
src/Runtime/Graphics/Passes/
  Graphics.Passes.Surface.cppm / .cpp
  Graphics.Passes.Line.cppm / .cpp
  Graphics.Passes.Point.cppm / .cpp
  Graphics.Passes.Picking.cppm / .cpp         # unchanged
  Graphics.Passes.SelectionOutline.cppm / .cpp # unchanged
  Graphics.Passes.DebugView.cppm / .cpp        # unchanged
  Graphics.Passes.ImGui.cppm / .cpp            # unchanged

assets/shaders/
  surface.vert / surface.frag
  line.vert / line.frag
  point_flatdisc.vert / point_flatdisc.frag
  point_surfel.vert / point_surfel.frag
  picking.vert / picking.frag                  # unchanged
```

### Deleted Files

```
# Passes
Graphics.Passes.Mesh.cppm / .cpp
Graphics.Passes.Graph.cppm / .cpp
Graphics.Passes.PointCloud.cppm / .cpp
Graphics.Passes.RetainedLine.cppm / .cpp
Graphics.Passes.RetainedPointCloud.cppm / .cpp
Graphics.Passes.Line.cppm / .cpp              # old transient-only LineRenderPass

# Shaders
line.vert / line.frag                         # old transient SSBO
point.vert / point.frag                       # old transient SSBO
line_retained.vert / line_retained.frag       # renamed to line.*
point_retained.vert / point_retained.frag     # split into point_flatdisc.* / point_surfel.*
triangle.vert / triangle.frag                 # renamed to surface.*
```

---

## Pass Design Details

### 1. SurfacePass

**Replaces:** `ForwardPass` (renamed, same GPU-driven culling architecture).

**What changes from current ForwardPass:**
- Renamed for consistency (ForwardPass → SurfacePass)
- Shader renamed (`triangle.* → surface.*`, code identical)
- Queries `ECS::Surface::Component` instead of `ECS::MeshRenderer::Component`
- Line/point topology variants dropped (those go to LinePass/PointPass)
- Gains transient submission API (same architecture as LinePass/PointPass)

**GPU-driven culling, instance SSBO, bindless textures — all preserved** for the retained path.

**Transient API:**
```cpp
struct TransientVertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    uint32_t  Color;     // packed ABGR
};

void SubmitTriangles(std::span<const TransientVertex> verts,
                     std::span<const uint32_t> indices);
void ResetTransient();
```

Use cases: debug planes/quads, collision mesh preview, marching cubes iso-surface while adjusting parameters, clipping plane visualization. Data packed into a per-frame host-visible buffer, BDA pointer passed via push constants with identity model matrix. The surface shader already reads positions/normals via BDA — it doesn't distinguish retained from transient.

**ECS query:** `registry.view<ECS::Surface::Component>()`

### 2. LinePass

**Replaces:** `RetainedLineRenderPass` + wireframe from `MeshRenderPass` + edges from `GraphRenderPass` + `LineRenderPass` (transient debug).

**Architecture — two internal data sources, one draw path:**

```cpp
class LinePass final : public IRenderFeature {
public:
    // --- Transient API (debug overlays, per-frame) ---
    void SubmitLine(glm::vec3 a, glm::vec3 b, uint32_t color);
    void SubmitOverlayLine(glm::vec3 a, glm::vec3 b, uint32_t color);
    void ResetTransient();

    // --- Retained path (automatic from ECS::Line::Component) ---
    // Handled internally in AddPasses()

    void AddPasses(RenderPassContext& ctx) override;
    // ...
};
```

**AddPasses() flow:**
1. Iterate `ECS::Line::Component` entities → build retained draws (BDA + edge SSBO)
2. Collect transient lines from staging buffer → pack into a separate per-frame SSBO
3. Record draw commands: retained entities first (per-entity push constants), then transient batch (identity model matrix, positions already in world space)

Both use the same pipeline and shaders. The transient lines just use an internal position buffer instead of a BDA pointer to an entity's vertex buffer. In the shader, this is transparent — it reads positions from whatever BDA pointer the push constants provide.

**Edge caching** moves into this pass. When `Line::Component.EdgeCacheDirty == true`, the pass extracts unique edges from the collision mesh (via `MeshCollider::Component`) and caches them. Same algorithm as current `MeshRenderPass`.

**Transient line implementation detail:**
Transient lines carry inline world-space positions (no separate vertex buffer). The pass maintains a per-frame host-visible buffer with interleaved `{vec3 posA, vec3 posB}` pairs. A BDA pointer to this buffer is passed to the shader. The edge SSBO for transient lines is trivial: `{0,1}, {2,3}, {4,5}, ...` (sequential pairs). Alternatively, the shader can detect transient mode via a flag and read positions directly from the SSBO without indirection.

**Push constants:**
```cpp
struct LinePushConstants {
    glm::mat4 Model;           // 64 bytes
    uint64_t  PtrPositions;    //  8 bytes
    float     LineWidth;       //  4 bytes
    float     ViewportWidth;   //  4 bytes
    float     ViewportHeight;  //  4 bytes
    uint32_t  Color;           //  4 bytes
    uint32_t  Flags;           //  4 bytes  (overlay bit, color mode bits)
    uint32_t  _pad;            //  4 bytes
};  // 96 bytes
```

### 3. PointPass

**Replaces:** `RetainedPointCloudRenderPass` + vertex part of `MeshRenderPass` + node part of `GraphRenderPass` + `PointCloudRenderPass`.

**Architecture — pipeline-per-mode, two internal data sources:**

```cpp
class PointPass final : public IRenderFeature {
public:
    // --- Transient API (debug markers, per-frame) ---
    void SubmitPoint(glm::vec3 pos, glm::vec3 normal, float size,
                     uint32_t color, PointRenderMode mode);
    void ResetTransient();

    // --- Retained path (automatic from ECS::Point::Component) ---
    // Handled internally in AddPasses()

    void AddPasses(RenderPassContext& ctx) override;

private:
    // One pipeline per rendering mode
    std::unique_ptr<RHI::GraphicsPipeline> m_Pipelines[kModeCount];
};
```

**AddPasses() flow:**
1. Iterate `ECS::Point::Component` entities → group by `Mode`
2. Collect transient points from staging buffer → group by mode
3. For each active mode: bind pipeline, draw retained entities, then draw transient batch

**Separate pipelines per mode:**
```
point_flatdisc.vert / point_flatdisc.frag   — camera-facing billboard
point_surfel.vert   / point_surfel.frag     — normal-oriented disc
point_ewa.vert      / point_ewa.frag        — EWA splatting (future)
point_gaussian.vert / point_gaussian.frag   — Gaussian splatting (future)
```

Each mode has fundamentally different vertex expansion logic. Separate pipelines avoid mega-shader branching and allow per-mode blend state (Gaussian needs alpha blending, surfels may want depth write).

**Push constants (shared layout across all point modes):**
```cpp
struct PointPushConstants {
    glm::mat4 Model;           // 64 bytes
    uint64_t  PtrPositions;    //  8 bytes
    uint64_t  PtrNormals;      //  8 bytes
    uint64_t  PtrAux;          //  8 bytes
    float     PointSize;       //  4 bytes
    float     SizeMultiplier;  //  4 bytes
    float     ViewportWidth;   //  4 bytes
    float     ViewportHeight;  //  4 bytes
    uint32_t  Color;           //  4 bytes
    uint32_t  ColorMode;       //  4 bytes  (0=uniform, 1=per-vertex, 2=textured)
    uint32_t  TextureID;       //  4 bytes
    uint32_t  _pad;            //  4 bytes
};  // 128 bytes
```

---

## Pipeline Execution Order

```
DefaultPipeline::RebuildPath()
│
├── 1. PickingPass           — entity/primitive ID readback
├── 2. SurfacePass           — filled triangles (GPU-driven culling)
├── 3. LinePass              — thick edges (retained + transient)
├── 4. PointPass             — points/vertices (retained + transient)
├── 5. SelectionOutlinePass  — post-process selection overlay
├── 6. DebugViewPass         — render target inspector
└── 7. ImGuiPass             — editor UI
```

No VisualizationCollect composite stage. No collector passes. No DebugLinePass. Each pass is self-contained.

`DebugDraw` becomes a thin API that delegates to `SurfacePass::SubmitTriangles()`, `LinePass::SubmitLine()`, and `PointPass::SubmitPoint()`. The `DebugDraw` class itself no longer owns staging buffers — it's just a convenience wrapper.

**Uniform pass shape — all three passes follow the same pattern:**
```
class XxxPass final : public IRenderFeature {
public:
    // Retained: automatic from ECS::Xxx::Component in AddPasses()
    // Transient: per-frame CPU submission API
    void SubmitXxx(...);    // accepts world-space data
    void ResetTransient();  // called once per frame before collection

    void AddPasses(RenderPassContext& ctx) override;
    // AddPasses flow:
    //   1. Iterate ECS components → build retained draws (BDA)
    //   2. Collect transient staging → pack into per-frame SSBO
    //   3. Record draw commands: retained first, then transient batch
};
```

---

## Shared Data Contract

All three passes share vertex data via **BDA pointers into the same device-local vertex buffer**.

**Contract:**
1. `GeometryGpuData` owns the vertex buffer (`std::shared_ptr<VulkanBuffer>`)
2. Multiple components on the same entity reference the same `GeometryHandle`
3. Each pass reads `GetVertexBuffer()->GetDeviceAddress() + layout.{Positions,Normals,Aux}Offset`
4. Retained vertex buffers are immutable after upload (no per-frame writes)
5. Buffer lifetime managed by `GeometryPool` with retirement frames matching frames-in-flight
6. Transient data uses separate per-frame host-visible buffers owned by each pass

**Who uploads what:**
- Mesh entity: `ModelLoader` uploads positions/normals/aux + triangle indices → `GeometryGpuData`
- Graph entity: layout system uploads node positions (+ optional normals) → `GeometryGpuData` (new)
- Point cloud entity: load system uploads positions/normals → `GeometryGpuData` (new)

---

## Extensibility

### Adding a new point mode (e.g., Gaussian Splatting)

1. Add enum value: `PointRenderMode::Gaussian = 3`
2. Write shaders: `point_gaussian.vert` / `point_gaussian.frag`
3. Register in ShaderRegistry: `"Point.Gaussian.Vert"_id`, `"Point.Gaussian.Frag"_id`
4. Add pipeline in `PointPass::BuildPipelines()` — may use custom blend state
5. Expose in UI: add to render mode combo

No routing changes. No new passes. No new components.

### Adding a completely new primitive type (e.g., volume rendering)

1. Create `VolumePass` class implementing `IRenderFeature`
2. Create `ECS::Volume::Component`
3. Write shaders: `volume.vert` / `volume.frag`
4. Register in `DefaultPipeline::Initialize()` + `RebuildPath()`
5. Register in `FeatureRegistry`: `"VolumePass"_id`

Each pass is self-contained. No existing code changes.

---

## Migration Order (Implementation Steps)

### Phase 1: Per-pass typed components

1. Define `ECS::Surface::Component` (initially mirrors `MeshRenderer::Component`)
2. Define `ECS::Line::Component` (wireframe settings from `RenderVisualization`)
3. Define `ECS::Point::Component` (vertex settings from `RenderVisualization`)
4. Define `ECS::Graph::Data` (graph-specific edge/node data)
5. Migration system: attach new components alongside old ones during transition

### Phase 2: Rename and consolidate passes

6. Rename `ForwardPass` → `SurfacePass` (class, files, shaders, registry IDs)
7. Rename `RetainedLineRenderPass` → `LinePass` (class, files, shaders, registry IDs)
8. Rename `RetainedPointCloudRenderPass` → `PointPass` (class, files, shaders, registry IDs)
9. Update `DefaultPipeline`, `FeatureRegistry`, `ShaderRegistry`, `CMakeLists.txt`
10. Build + verify tests pass

### Phase 3: Each pass becomes self-contained (retained + transient)

**SurfacePass:**
11. Add `SubmitTriangles()` / `ResetTransient()` transient API to SurfacePass
12. Internal per-frame host-visible buffer for transient triangle data
13. SurfacePass draws retained entities (GPU-driven culling) then transient batch (direct draw)

**LinePass:**
14. Move edge caching into `LinePass::AddPasses()` (from `MeshRenderPass`)
15. Add `ECS::Line::Component` iteration (replaces `RenderVisualization::ShowWireframe` check)
16. Add graph edge iteration (replaces `GraphRenderPass` edge submission)
17. Add `SubmitLine()` / `SubmitOverlayLine()` / `ResetTransient()` transient API
18. Delete wireframe code from `MeshRenderPass` and `GraphRenderPass`

**PointPass:**
19. Add `ECS::Point::Component` iteration (replaces `RenderVisualization::ShowVertices` check)
20. Add graph node iteration (replaces `GraphRenderPass` node submission)
21. Add standalone point cloud iteration (replaces `PointCloudRenderPass`)
22. Add device-local upload for graph node positions → `GeometryHandle`
23. Add device-local upload for point cloud positions → `GeometryHandle`
24. Add `SubmitPoint()` / `ResetTransient()` transient API
25. Delete vertex/node code from `MeshRenderPass`, `GraphRenderPass`, `PointCloudRenderPass`

### Phase 4: Route DebugDraw through pass transient APIs

26. `DebugDraw` delegates to `LinePass::SubmitLine()` / `PointPass::SubmitPoint()` / `SurfacePass::SubmitTriangles()`
27. Remove staging buffers from `DebugDraw` — becomes a thin convenience wrapper

### Phase 5: Delete dead code

28. Delete `MeshRenderPass` (class, files, module partition)
29. Delete `GraphRenderPass` (class, files, module partition)
30. Delete `PointCloudRenderPass` (class, files, module partition)
31. Delete `LineRenderPass` (old transient-only pass)
32. Delete old transient shaders (`point.vert/frag`, `line.vert/frag` SSBO versions)
33. Delete `RenderVisualization::Component` (replaced by typed components)
34. Delete `GeometryViewRenderer::Component` (each component carries its own handle)
35. Remove `VisualizationCollect` composite stage from `DefaultPipeline`

### Phase 6: Pipeline-per-mode for PointPass

36. Split `point.vert/frag` into `point_flatdisc.vert/frag` and `point_surfel.vert/frag`
37. PointPass stores pipeline array indexed by `PointRenderMode`
38. Group entities by mode, draw each batch with correct pipeline
39. Add `PointRenderMode` UI combo selector in Inspector

### Phase 7: SurfacePass query migration

40. SurfacePass queries `ECS::Surface::Component` instead of `MeshRenderer::Component`
41. Migrate `GPUSceneSync` to use new component types
42. Remove old `MeshRenderer::Component` (or alias it during transition)

### Phase 8: Update docs

43. Update `TODO.md` — remove completed items, add new remaining work
44. Update `CLAUDE.md` — document new pass naming and architecture
45. Update `README.md` — update rendering architecture description

---

## What Does NOT Change

- `PickingPass` — unchanged
- `SelectionOutlinePass` — unchanged
- `DebugViewPass` — unchanged
- `ImGuiPass` — unchanged
- `GeometryGpuData` / `GeometryPool` / `GeometryUploadRequest` — unchanged
- `RHI` layer — unchanged
- `RenderGraph` — unchanged
- `GPUScene` / `GPUSceneSync` — adapts to new component names but same architecture
- `PipelineLibrary` — unchanged (SurfacePass uses same compiled pipelines)
- `MeshCollider::Component` — unchanged (still provides collision data for edge extraction)
- All geometry algorithms — unchanged
- All test targets — unchanged (but tests may need updated imports)
