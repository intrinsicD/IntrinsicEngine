# IntrinsicEngine — Architecture Roadmap

This document contains medium/long-horizon feature planning and phase prioritization that were previously co-located with the architecture TODO backlog.

## 1. Near-Term Execution Order (Architecture)

Recent completions (2026-02-26, details in git history): release-flag consolidation, offline dependency mode (`-DINTRINSIC_OFFLINE_DEPS=ON`), DAGScheduler flat-hash resource lookup + sorted-set edge dedupe with pathological fan-out validation, scheduler wait-queue scratch reuse, FrameGraph ready-queue execution (dependency-readiness dispatch), baseline Boolean CSG (disjoint/full-containment), and core task park/unpark semantics with SLO gates.

Recent completions (2026-03-02, details in git history): standalone retained-mode point cloud rendering — `PointCloudRendererLifecycle` system, `PointCloudRenderer::Component` with `GeometryHandle`, file-loaded and code-originated upload paths, `RetainedPointCloudRenderPass` iteration, `SceneManager` topology routing, `GPUSceneSync` transform sync, entity destroy hooks.

Recent completions (2026-03-02, details in git history): per-edge and per-face attribute rendering — `PtrEdgeAux` BDA channel in `RetainedLinePushConstants` for per-edge colors from PropertySets, `PtrFaceAttr` BDA channel in `MeshPushConstants` for per-face colors via `gl_PrimitiveID`, persistent attribute buffers in `RetainedLineRenderPass` and `ForwardPass`, `CachedEdgeColors`/`CachedFaceColors` on ECS components, `GraphGeometrySyncSystem` edge color extraction from `"e:color"` PropertySet. Contract tests in `Test_BDASharedBufferContract.cpp`.

Recent completions (2026-03-02, details in git history): `MeshViewLifecycleSystem` — `MeshEdgeView::Component` and `MeshVertexView::Component` for mesh-derived GPU geometry views, lifecycle system creates edge index buffers and vertex point views via `ReuseVertexBuffersFrom(meshHandle)`, GPUScene slot allocation, EnTT `on_destroy` hooks for automatic cleanup, FeatureRegistry-gated registration in `Engine::Run()`, contract tests in `Test_MeshViewLifecycle.cpp`.

Recent completions (2026-03-02, details in git history): retained render pass wiring to `MeshEdgeView`/`MeshVertexView` geometry handles — `RetainedLineRenderPass` now prefers `MeshEdgeView::Geometry` BDA index buffer (via `GeometryGpuData::GetIndexBuffer()->GetDeviceAddress()`) for mesh wireframe, falling back to internal `EnsureEdgeBuffer()` when the view is absent. `RetainedPointCloudRenderPass` now prefers `MeshVertexView::Geometry` BDA vertex buffer for mesh vertex visualization, falling back to direct `MeshRenderer::Geometry` lookup. Per-edge attribute buffers remain internally managed. Contract tests extended in `Test_MeshViewLifecycle.cpp`.

Recent completions (2026-03-02, details in git history): `GraphGeometrySyncSystem` GPUScene integration — `GpuSlot` field on `ECS::Graph::Data` with `AllocateSlot()`/`FreeSlot()` lifecycle matching `PointCloudRendererLifecycle` pattern, per-node attribute extraction (`"v:color"` → `CachedNodeColors`, `"v:radius"` → `CachedNodeRadii`) from PropertySets, `GPUSceneSync` transform-only sync for graph entities, `on_destroy` hook in `SceneManager` for automatic slot reclamation, `FeatureRegistry` registration as `"GraphGeometrySync"`. Contract tests extended in `Test_GraphRenderPass.cpp` and `Test_BDASharedBufferContract.cpp`.

Recent completions (2026-03-03, details in git history): `PointCloudGeometrySyncSystem` — `ECS::PointCloud::Data` component holding `shared_ptr<Geometry::PointCloud::Cloud>` as PropertySet-backed data authority (analogous to `ECS::Graph::Data`), `PointCloudGeometrySyncSystem` uploads `Cloud::Positions()`/`Normals()` spans to device-local `GeometryGpuData` via Staged upload, per-point attribute extraction (`"p:color"` → `CachedColors`, `"p:radius"` → `CachedRadii`) from Cloud PropertySets, `GPUScene` slot allocation, `GPUSceneSync` transform-only sync, `on_destroy` hook in `SceneManager` for automatic slot reclamation, `RetainedPointCloudRenderPass` iteration of `ECS::PointCloud::Data` entities, `FeatureRegistry` registration as `"PointCloudGeometrySync"`. Contract tests in `Test_PointCloudGeometrySync.cpp`.

Recent completions (2026-03-03, details in git history): LinePass consolidation (PLAN.md Phase 3, partial) — deleted old transient `LineRenderPass` class and SSBO-based `line.vert/line.frag` shaders, renamed `RetainedLineRenderPass` → `LinePass` and `line_retained.vert/frag` → `line.vert/frag`, unified retained BDA wireframe/graph edges and transient DebugDraw lines into single `LinePass` with depth-tested and overlay pipelines, per-frame BDA-based transient buffers (positions, identity edge pairs, per-edge colors) for DebugDraw integration, updated `DefaultPipeline` to replace separate `RetainedLines`/`LinePass` stages with single unified `LinePass` stage, shader registry consolidated (`"Line.Vert"_id`/`"Line.Frag"_id` now point to the BDA shaders), `FeatureRegistry` updated to `"LinePass"_id`.

Recent completions (2026-03-03, details in git history): per-pass typed ECS components (PLAN.md Phase 1) — `ECS::Surface::Component`, `ECS::Line::Component`, `ECS::Point::Component` defined in `Graphics.Components.cppm` alongside legacy components. `ComponentMigration` system (`Graphics.Systems.ComponentMigration`) syncs legacy → new components each frame (MeshRenderer→Surface, RenderVisualization wireframe→Line / vertices→Point, PointCloudRenderer→Point, Graph::Data→Line+Point, PointCloud::Data→Point). FeatureRegistry-gated registration in `Engine::Run()`. Contract tests in `Test_PerPassComponents.cpp`.

Recent completions (2026-03-03, details in git history): SurfacePass rename + consolidation (PLAN.md Phase 2) — `ForwardPass` → `SurfacePass` (class, files, module partition `Graphics:Passes.Surface`), shaders `triangle.vert/frag` → `surface.vert/frag`, transient triangle API (`SubmitTriangle()`/`ResetTransient()`/`GetTransientVertices()`) on `SurfacePass`, `GetTriangles()`/`Triangle()`/`Quad()` on `DebugDraw`, `SurfacePass` queries `ECS::Surface::Component` instead of `MeshRenderer::Component`, `GPUSceneSync` migrated to `Surface::Component`, pipeline constants renamed (`kPipeline_Surface/SurfaceLines/SurfacePoints`), `FeatureRegistry` entry renamed to `"SurfacePass"`, shader registry updated (`Surface.Vert/Frag`).

Recent completions (2026-03-03, details in git history): LinePass `ECS::Line::Component` iteration (PLAN.md Phase 3, continued) — LinePass now iterates `ECS::Line::Component` exclusively for retained-mode draws instead of querying `MeshRenderer+RenderVisualization` and `Graph::Data` directly. `ComponentMigration` populates `EdgeCount` on `Line::Component` from `MeshEdgeView::EdgeCount` or `CachedEdges`/`CachedEdgePairs` sizes. Retained draws correctly route to depth-tested or overlay pipeline based on `Line::Component.Overlay` (fixes: overlay was previously ignored for retained mesh wireframe and graph edges). Deleted CPU wireframe DebugDraw submission from `MeshRenderPass` (edge cache building retained for LinePass fallback). Deleted CPU edge DebugDraw submission from `GraphRenderPass` (node submission retained for PointCloudRenderPass fallback).

Recent completions (2026-03-03, details in git history): PropertySet-driven edge view creation (PLAN.md Phase 3, final) — `GraphGeometrySyncSystem` now creates edge index buffers via `ReuseVertexBuffersFrom` (`GpuEdgeGeometry`/`GpuEdgeCount` fields on `ECS::Graph::Data`), `MeshViewLifecycleSystem` auto-attaches/detaches `MeshEdgeView` based on `ShowWireframe` and extracts edges from collision data instead of `CachedEdges`, `ComponentMigration` wires `GpuEdgeGeometry` into `Line::Component.EdgeView` for graphs, LinePass internal `EnsureEdgeBuffer`/`m_EdgeBuffers` fallback path eliminated — `EdgeView` must be valid for rendering. Contract tests updated in `Test_MeshViewLifecycle.cpp`, `Test_GraphRenderPass.cpp`, `Test_BDASharedBufferContract.cpp`.

Near-term priority is the **rendering architecture refactor** defined in `PLAN.md` (TODO §1): consolidate into three unified passes (`SurfacePass`, `LinePass`, `PointPass`), rename/migrate existing passes to use new `ECS::Surface/Line/Point::Component` types, and delete legacy pass/component code. Phases 1–5 complete. Next: Phase 6 (lifecycle system adaptation) and Phase 7 (per-face attribute verification). Secondary priorities: staged upload path for static graphs (TODO §2), PropertySet dirty-domain sync (TODO §3).

Recent completions (2026-03-03, details in git history): Phase 5 dead code deletion — deleted `MeshRenderPass`, `GraphRenderPass`, `PointCloudRenderPass`, `RetainedPointCloudRenderPass` (classes, files, module partitions). Deleted `MeshRenderer::Component`, `RenderVisualization::Component`, `GeometryViewRenderer::Component` from `Graphics.Components.cppm`. Moved `EdgePair` to standalone `ECS::EdgePair`. Added `Visible`/`CachedVisible`/`CachedFaceColors` to `Surface::Component`, `CachedEdgeColors` to `Line::Component`. All consumers migrated: `SceneManager` creates `Surface::Component` directly, `MeshRendererLifecycle` writes `Surface::Component`, `MeshViewLifecycle` checks `Line::Component` presence for wireframe, `GPUSceneSync` reads `Surface::Visible`, `SurfacePass`/`LinePass`/`PickingPass` iterate new component types, `ComponentMigration` reduced to 3 sections (PointCloudRenderer→Point, Graph::Data→Line+Point, PointCloud::Data→Point). `DefaultPipeline` simplified (7 passes, no VisualizationCollect stage). Sandbox UI uses direct component attach/detach. Test suite updated.

Recent completions (2026-03-03, details in git history): PointPass consolidation (PLAN.md Phase 4) — renamed `RetainedPointCloudRenderPass` → `PointPass` (class, module partition `Graphics:Passes.Point`, files), split `point_retained.vert/frag` into `point_flatdisc.vert/frag` (camera-facing billboard) and `point_surfel.vert/frag` (normal-oriented disc + EWA splatting), PointPass stores pipeline array indexed by `PointRenderMode` (FlatDisc=0, Surfel/EWA=1), iterates `ECS::Point::Component` exclusively (replaces four separate entity source loops), `DebugDraw::Point()` / `GetPoints()` for transient point markers, per-frame host-visible BDA transient buffers for DebugDraw integration, `PtrAux` BDA channel for per-point colors from PropertySets (packed ABGR, persistent per-entity aux buffers), new push constants layout (120 bytes: Model + PtrPositions/PtrNormals/PtrAux + PointSize/SizeMultiplier/Viewport + Color/Flags). Shader registry registers `Point.FlatDisc.Vert/Frag` and `Point.Surfel.Vert/Frag`. FeatureRegistry registers `"PointPass"` feature gate.

## 2. Feature Roadmap


### 2.1 Rendering Modes

#### 2.1.1 Point Cloud Rendering

**Context:** No point cloud rendering support exists. The engine needs configurable point cloud visualization for 3D vision and scanning workflows.

**Required variants:**
- **Gaussian Splatting (3DGS):** Render 3D Gaussians as oriented, anisotropic splats. This is the dominant representation in neural radiance field / 3D reconstruction research. Requires a dedicated compute-based rasterizer or a tile-based sort+blend pipeline (not standard triangle rasterization).
- **Potree-style octree LOD:** Hierarchical out-of-core streaming for massive point clouds (billions of points). Octree nodes loaded on demand based on camera distance and screen-space error budget. The existing `Geometry.Octree` can serve as a starting point for the spatial index.
- **Flat / fixed-size splatting:** Simple screen-space or world-space constant-size point sprites. Fast baseline for small-to-medium clouds.
- **EWA (Elliptical Weighted Average) splatting:** Perspective-correct elliptical splats that avoid holes and aliasing at grazing angles. Classic Zwicker et al. approach.
- **Surfel rendering:** Oriented discs derived from local surface normals + estimated radius. Good intermediate between points and meshes.

**Architecture notes:**
- Point clouds should be a first-class `GeometryType` alongside triangle meshes, with their own GPU buffer layout (position + optional normal, color, scalar attributes).
- A `PointCloudRenderFeature` registered via the render pipeline system, with a config struct selecting the variant and parameters (splat size, LOD budget, etc.).
- Large point clouds need streaming — integrate with `TransferManager` for async chunk uploads.

**Status:** Standalone retained-mode point cloud rendering is **complete** (2026-03-02), with migration to unified `PointPass` **complete** (2026-03-03, Phase 4+5). Point clouds are first-class retained-mode renderables, same architectural tier as triangle meshes.

**What works:**
- **`Geometry.PointCloud` module** (`Geometry.PointCloud.cppm/.cpp`): First-class `Cloud` data structure with positions, normals, colors, radii. Operations: `ComputeBoundingBox`, `ComputeStatistics` (with KNN-based spacing), `VoxelDownsample` (O(n) hash-based), `EstimateRadii` (Octree-accelerated kNN density estimation), `RandomSubsample` (deterministic Fisher-Yates).
- **Unified `PointPass` rendering:** `PointPass` iterates `ECS::Point::Component` for all retained-mode point draws (mesh vertices, graph nodes, standalone point clouds, Cloud-backed data) plus transient `DebugDraw::GetPoints()` markers. Per-mode pipelines (`point_flatdisc.vert/frag` for FlatDisc, `point_surfel.vert/frag` for Surfel+EWA). `PtrAux` BDA channel for per-point colors from PropertySets. `ComponentMigration` bridges `PointCloudRenderer::Component` → `ECS::Point::Component` each frame.
- **BDA-based shared-buffer contract:** For mesh-derived vertex visualization, positions read via BDA from the mesh's existing buffer. For standalone point clouds, device-local buffer with `SHADER_DEVICE_ADDRESS_BIT`.

**Remaining future work:**
- Gaussian Splatting (3DGS) compute rasterizer (new `PointPass` mode), Potree-style octree LOD streaming, depth peeling for OIT.

---

#### 2.1.2 Graph / Wireframe Rendering

**Context:** The engine has `Geometry.Graph.cppm` and `Geometry.HalfedgeMesh.cppm` but no visualization for graph structures.

**Required variants:**
- **Mesh wireframe overlay:** Render triangle edges as lines over shaded geometry. Configurable color, thickness, and depth bias. Can be done via barycentric-coordinate fragment shader (no geometry shader needed) or a dedicated line-drawing pass.
- **Graph structure visualization:** Render abstract node-edge graphs (e.g., scene hierarchy, dependency graphs, connectivity graphs). Needs graph layout algorithms:
  - Force-directed (Fruchterman-Reingold)
  - Spectral layout (Laplacian eigenvectors)
  - Hierarchical / tree layout
- **kNN-graph of point clouds:** Compute k-nearest-neighbor graph from point cloud spatial queries and render as line segments. Useful for debugging spatial algorithms. The existing `Geometry.Octree` can accelerate neighbor queries.
- **Halfedge visualization:** Debug view of the halfedge data structure — vertices as points, edges as directed arrows, face normals.

**Architecture notes:**
- Line rendering is handled by `LinePass` (see `PLAN.md`) — a unified pass with both retained BDA and transient DebugDraw data paths internally.
- Thick lines via screen-space expansion in vertex shader (Vulkan has no guaranteed wide-line support).
- Node rendering handled by `PointPass` — reuses point splatting infrastructure with pipeline-per-mode (`FlatDisc`, `Surfel`, `EWA`).

**Status:** Line and wireframe rendering are **complete** via the unified `LinePass` and `PointPass` architecture (PLAN.md Phases 1–5). Legacy passes deleted. `LinePass` handles retained BDA mesh wireframe/graph edges and transient DebugDraw lines. `PointPass` handles retained BDA mesh vertices/graph nodes/point clouds and transient DebugDraw points. CPU-side graph algorithms remain functional.

**What still works (CPU-side graph algorithms):**
- kNN graph construction: Octree-accelerated exact builder (`Geometry::Graph::BuildKNNGraph()`) and manual build from precomputed neighbor index lists (`Geometry::Graph::BuildKNNGraphFromIndices()`) with Union/Mutual connectivity and degenerate-pair filtering.
- Fruchterman-Reingold force-directed embedding (`ComputeForceDirectedLayout()`).
- Spectral 2D embedding (`ComputeSpectralLayout()`) with combinatorial or symmetric-normalized Laplacian.
- Hierarchical layered embedding (`ComputeHierarchicalLayout()`) with crossing diagnostics and diameter-aware auto-rooting.
- General 2D embedding crossing counter (`CountEdgeCrossings()`).

**Re-implementation plan (unified pass architecture, per `PLAN.md`):**

The target architecture consolidates all line/edge/wireframe rendering into a single **`LinePass`** that handles both retained and transient data internally. No separate debug line pass. Full spec in `PLAN.md`.

*Unified `LinePass` — retained + transient in one pass:*
- **BDA-based shared-buffer contract:** One device-local vertex buffer on the GPU, multiple index buffers with different topologies. A mesh uploads positions/normals once; wireframe, vertex, and graph views all `ReuseVertexBuffersFrom` that mesh handle — zero vertex duplication. Each topology view gets its **own VkPipeline** with its own vertex shader that reads from the shared buffer via BDA (`GL_EXT_buffer_reference` pointer in push constants). The sharing is at the data level (same `VulkanBuffer`, same device address), not at the pipeline level.
- **Separate pipelines required:** `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` gives 1px lines, `POINT_LIST` gives 1px dots. Thick anti-aliased lines and billboard points each need their own shader pipeline with vertex-shader expansion (6 verts/primitive).
- **Retained path:** `LinePass` iterates `ECS::Line::Component` entities — mesh wireframe (edge pairs from `Halfedge::Mesh` PropertySets), graph edges (edge pairs from `Graph` PropertySets), standalone line entities. Edge pairs uploaded as persistent index buffers via `ReuseVertexBuffersFrom`. BDA-addressed position reads from shared vertex buffer.
- **Transient path (same pass, same pipeline):** `LinePass` reads `DebugDraw::GetLines()`/`GetOverlayLines()` → packs into per-frame host-visible SSBO → draws with identity model matrix. Same shader reads same BDA pointer — data lifetime is an internal detail, not a separate pass.
- `ECS::Graph::Data`: PropertySet-backed data authority wrapping `shared_ptr<Geometry::Graph>`. `GraphGeometrySyncSystem` populates sibling `ECS::Line::Component` + `ECS::Point::Component` handles — passes don't need graph-specific knowledge.
- Lifecycle systems allocate `GPUScene` slots per view, sync transforms, frustum culling — same path as `MeshRendererLifecycle`.
- Thick-line vertex shader expansion (6 verts/segment), anti-aliased fragment shader, push constants for line width + viewport + BDA pointers + per-edge attribute BDA channel.

*`DebugDraw` remains a dumb accumulator* — stores lines, points, and triangles submitted during the frame. Each unified pass **pulls** from DebugDraw in its `AddPasses()`. No coupling between DebugDraw and any specific pass.

Remaining work after `LinePass` consolidation: mesh wireframe overlay (barycentric shader alternative), halfedge debug view, render-feature/UI integration for kNN + layout visualization.

---

#### 2.1.3 Mesh Rendering Modes

**Context:** Currently only a single forward PBR pass (metallic-roughness) exists via `SurfacePass`. Need configurable shading for research and artistic workflows.

**Required shading models:**
- **Physically-based (PBR):** Already implemented (metallic-roughness). Extend with clearcoat, sheen, transmission for glTF PBR extensions.
- **Flat shading:** Per-face constant color (no interpolation). Useful for low-poly and CAD visualization.
- **Gouraud / Phong:** Classic per-vertex and per-fragment Blinn-Phong. Useful for comparison and lightweight rendering.
- **Matcap (Material Capture):** View-space normal → texture lookup. Fast artistic shading with no lights needed.
- **Non-photorealistic rendering (NPR):**
  - Toon / cel shading (discrete light bands + ink outlines)
  - Gooch shading (warm-to-cool tone mapping based on surface orientation)
  - Hatching / cross-hatching (texture-based stroke patterns modulated by light)
  - Pencil / sketch style (edge detection + noise-based strokes)
- **Curvature visualization:** Per-vertex mean/Gaussian curvature mapped to a diverging colormap. Requires differential geometry operators on the mesh.
- **Scalar field visualization:** Map arbitrary per-vertex scalar data (e.g., geodesic distance, quality metric, segmentation label) to configurable colormaps (viridis, jet, coolwarm, etc.).
- **Normal visualization:** Display surface normals as color (world-space or view-space RGB mapping) or as hedgehog lines.
- **UV checker / texture-coordinate visualization:** Checkerboard pattern mapped via UVs to inspect parameterization quality.

**Architecture notes:**
- Implement as swappable `ShadingMode` enum on the material or render feature level, not as separate render passes.
- Share the same vertex pipeline; only fragment shaders differ.
- `PipelineLibrary` needs to support variant PSOs keyed by `(ShadingMode, VertexFormat)`.
- The `RenderSystem::RequestPipelineSwap()` mechanism can be extended to switch shading modes globally or per-material.

---

#### 2.1.4 Post-Processing Pipeline

**Context:** No post-processing exists. The forward pass writes directly to the swapchain.

**Required effects:**
- **Tone mapping:** HDR → LDR conversion (ACES, Reinhard, filmic, AgX). Prerequisite for HDR rendering.
- **SSAO (Screen-Space Ambient Occlusion):** Depth-based ambient occlusion for contact shadows. HBAO+ or GTAO for quality.
- **Bloom:** Bright-pass threshold + Gaussian blur cascade + additive blend.
- **FXAA / TAA:** Anti-aliasing. FXAA for simplicity, TAA for temporal stability (important for thin geometry like wireframes and point clouds).
- **Depth of field:** Optional — useful for presentation renders.
- **Color grading:** LUT-based or parametric (exposure, contrast, saturation, white balance).

**Architecture notes:**
- Post-processing should be a chain of `RenderGraph` passes operating on the HDR color buffer.
- Each effect is an independent pass that can be toggled at runtime.
- Requires a dedicated HDR render target (R16G16B16A16_SFLOAT) separate from the swapchain.

---

#### 2.1.5 Shadow Mapping

**Context:** No shadow support exists. Shadows are critical for spatial understanding in 3D scenes.

**Required:**
- **Cascaded shadow maps (CSM)** for directional lights.
- **Point light shadow maps** (cubemap or dual-paraboloid) if point lights are added.
- **Percentage-closer filtering (PCF)** or **variance shadow maps (VSM)** for soft edges.

**Architecture notes:**
- Shadow pass reuses the existing geometry pipeline but writes only depth.
- Shadow atlas or array texture managed by `RenderSystem`.
- Integrate as a `RenderGraph` pass that runs before the main forward pass.

---

#### 2.1.6 Transparency & Order-Independent Transparency

**Context:** No transparency support. Required for translucent surfaces, point cloud blending, and X-ray visualization modes.

**Options (pick one or provide configurable):**
- **Weighted blended OIT** (McGuire & Bavoil): Simple, fast, approximate. Good default.
- **Per-pixel linked lists:** Exact but memory-hungry and variable performance.
- **Depth peeling:** Exact, predictable memory, but multi-pass.

---

### 2.2 Selection & Interaction

#### 2.2.2 Sub-Entity Selection (Vertex, Edge, Face, Region)

**Context:** Current selection is entity-level only. `HalfedgeMesh` exists in `Geometry/` providing the topological foundation for sub-mesh queries.

**Required selection modes:**
- **Vertex selection:** Click to select individual vertices. Render selected vertices as highlighted points.
- **Edge selection:** Click to select edges (halfedge pairs). Render selected edges as highlighted line segments.
- **Face selection:** Click to select triangles/polygons. Render selected faces with a distinct color overlay.
- **Region / area selection:**
  - Lasso selection (freeform screen-space polygon)
  - Box / rectangle selection
  - Paint-brush selection (drag to grow selection)
  - Connected-component flood fill (select all connected faces matching a criterion)
  - Angle-based region growing (select faces within normal-angle threshold of seed face)

**Architecture notes:**
- Sub-entity picking requires a dedicated GPU pass that writes `(EntityID, PrimitiveID, BarycentricCoords)` — the existing `PickingPass` only writes `EntityID`.
- CPU-side: Use `HalfedgeMesh` for adjacency traversal (flood fill, region growing, connected components).
- Selection state: Per-entity bitsets or index sets for selected vertices/edges/faces, stored as an ECS component.
- Visualization: Overlay pass that reads the selection bitset and highlights geometry.

---

#### 2.2.3 Transform Gizmos

**Context:** No visual manipulation gizmos exist despite the engine having a full transform system (`Transform::Component`). Currently transforms can only be edited via the ImGui Inspector panel.

**Required:**
- **Translate gizmo:** Three axis arrows + three plane handles + center sphere.
- **Rotate gizmo:** Three axis rings (trackball).
- **Scale gizmo:** Three axis handles with cube endpoints.
- **Snap modes:** Grid snap (configurable step), angle snap for rotation.
- **Space modes:** World-space vs. local-space orientation.
- **Multi-object pivot:** Transform multiple selected entities around a shared pivot (centroid, first-selected, or custom).

---

### 2.3 Debug Visualization

#### 2.3.1 Spatial Data Structure Visualization

**Context:** `DebugViewPass` exists but is minimal (depth/picking debug only). The engine has `Geometry.Octree`, `Geometry.AABB`, `Geometry.OBB`, and other spatial structures but no way to visualize them.

**Required debug overlays:**
- ~~**Octree:**~~ — **DONE.** Sandbox can render the selected entity’s `MeshCollider` `Geometry::Octree` as color-by-depth wire boxes via `Graphics::DebugDraw` + `LinePass` (transient path). Controls live in `View Settings → Spatial Debug` (max depth, leaf-only, occupied-only, overlay/depth-tested).
- ~~**KD-tree:**~~ — **DONE.** Core geometry accelerator (`Geometry::KDTree`) now includes Sandbox debug overlays through `Graphics::DrawKDTree()` (leaf/internal AABB wires + split-plane rectangles), routed through `DebugDraw` + `LinePass` (transient path) with UI knobs for max depth, overlay/depth-tested mode, and per-category colors.
- ~~**BVH (Bounding Volume Hierarchy):**~~ — **DONE.** Sandbox now renders selected `MeshCollider` triangle BVHs as wireframe node AABBs with configurable max depth, leaf/internal filtering, per-category colors/alpha, and overlay/depth-tested routing via `Graphics::DrawBVH()` + `DebugDraw` + `LinePass` (transient path).
- **Uniform grid:** Wireframe cells with occupancy coloring.
- ~~**Bounding volumes:**~~ — **DONE.** Sandbox can render selected `MeshCollider` bounds as world AABB, world OBB, and conservative bounding sphere overlays. Per-type toggles, independent colors, alpha, and overlay/depth-tested routing are exposed in `View Settings → Spatial Debug`.
- ~~**Contact manifolds:**~~ — **DONE.** Sandbox now renders selected-entity contact manifolds against other `MeshCollider` OBBs: paired contact points, contact segment, and contact normal vector, with depth-tested/overlay modes and scale controls in `View Settings → Spatial Debug`.

**Architecture notes:**
- Debug drawing uses the transient path of `LinePass` (from §2.1.2 / `PLAN.md`). Each unified pass pulls from `DebugDraw` in its `AddPasses()` — no separate debug pass.
- Debug draw calls go through an immediate-mode API: `DebugDraw::Line(a, b, color)`, `DebugDraw::Box(aabb, color)`, `DebugDraw::Sphere(center, radius, color)`, etc.
- All debug geometry is transient — rebuilt each frame from `LinearArena`.
- Toggled per-category via the UI (§2.5).

**Status:** `DebugDraw` provides the immediate-mode accumulator with `Line`, `Box`, `WireBox`, `Sphere`, `Circle`, `Arrow`, `Axes`, `Frustum`, `Grid`, `Cross` plus overlay variants. Convex hull geometry backend is now available via `Geometry::ConvexHullBuilder::Build()` (§2.6), and the Sandbox includes a selected-collider convex-hull wire overlay (`Graphics::DrawConvexHull`) with overlay/depth-tested routing and color/alpha controls in `View Settings → Spatial Debug`. Sandbox debug overlays (octree, KD-tree, bounds, contact manifolds, convex hulls) all emit geometry through `DebugDraw`. However, the rendering backend is currently **broken** — all debug visualization is non-functional until the `LinePass` consolidation from `PLAN.md` is complete (see §2.1.2). Remaining work after `LinePass` is operational: broader per-category UI polish (§2.5) and additional spatial overlays (uniform grid).

---

### 2.4 Extension / Plugin Architecture

**Context:** `Core::FeatureRegistry` provides the central registration pattern (Tier 1 — DONE). Remaining work is shader hot-reload and an optional scripting layer.

**Tier 2 — Shader hot-reload (already partially exists):**
- `ShaderRegistry` provides the path lookup. Extend with file-watcher integration for automatic recompilation on save.
- Materials reference shaders by name — hot-reload propagates automatically.

**Tier 3 — Scripting layer (optional, for rapid prototyping):**
- **Lua** (via sol2) or **Python** (via pybind11) bindings for:
  - ECS entity manipulation (create, destroy, set/get components)
  - Geometry operator invocation (simplify, smooth, remesh)
  - UI panel scripting (ImGui bindings)
  - Custom per-frame logic (registered as FrameGraph systems)
- This is the practical path for "novel rendering approaches and geometry processing methods" without recompiling C++.
- Python bindings are particularly valuable for integration with scientific/ML workflows (NumPy, PyTorch, Open3D interop).

**What NOT to do:**
- Don't attempt `.so`/`.dll` dynamic plugins with C++20 modules — the ABI is not stable across compilers or even compiler versions.
- Don't introduce abstract base classes everywhere "just in case" — it contradicts the engine's concrete-type architecture and hurts performance.

---

### 2.5 UI Improvements

**Context:** Current UI is basic ImGui panels (Hierarchy, Inspector, Assets, Performance). Functional but not user-friendly.

**Required improvements:**
- **Dockable panel layout:** ImGui docking branch (already available via imgui) — allow users to arrange panels freely, save/restore layouts.
- **Viewport controls:** Toolbar for render mode switching (shading mode, wireframe overlay, debug views) directly in the 3D viewport, not buried in menus.
- **Property editor improvements:**
  - Undo/redo stack for all property changes.
  - Multi-object editing (edit shared properties across all selected entities).
  - Drag-and-drop for asset assignment (drag a material onto a mesh).
- **Asset browser:** Thumbnail previews, drag-and-drop import, directory navigation. Replace the current flat list.
- **Console / log panel:** Scrollable, filterable log output in the UI (currently logs go to stdout only).
- ~~**Status bar:**~~ — **DONE (baseline).** Sandbox now renders a persistent bottom status strip with frame time/FPS, live entity count, and active renderer label for at-a-glance viewport context. Remaining follow-up: wire GPU memory telemetry into the strip once allocator-level usage counters are exposed.
- **Keyboard shortcuts:** Configurable hotkeys for common operations (select mode, transform mode, render mode toggle).
- **Dark/light theme:** Configurable ImGui theme with presets.
- **Context menus:** Right-click menus on entities (delete, duplicate, rename, focus camera).

---

### 2.6 Geometry Processing Operators

The geometry module now includes `Geometry::KDTree`, an Octree-inspired accelerator over element AABBs (`Build(span<const AABB>)`, `BuildFromPoints`) for exact nearest-neighbor and radius queries under AABB distance, plus generic overlap filtering for `AABB`/`Sphere`/`Ray` query shapes. The builder uses median splits on the largest-extent axis with bounded leaf size/depth and branch-and-bound AABB pruning during query traversal. This provides a deterministic CPU fallback/complement to Octree queries for operators that benefit from axis-aligned binary partitioning and non-point primitives.

**Context:** The engine has collision and spatial query primitives (`GJK`, `EPA`, `Octree`, `HalfedgeMesh`, `Raycast`, etc.) and now a growing set of higher-level geometry processing operators.

**Implemented operators:**
- ~~**Topological mesh editing:**~~ — **DONE.** `Geometry.HalfedgeMesh` now supports `EdgeCollapse` (with Dey-Edelsbrunner link condition), `EdgeFlip` (with valence/duplicate-edge guards), and `EdgeSplit`. These Euler operations are the building blocks for remeshing, simplification, and adaptive refinement.
- ~~**Mesh simplification / decimation:**~~ — **DONE.** `Geometry.Simplification` implements Garland-Heckbert QEM edge collapse with per-vertex quadric error accumulation, optimal vertex placement via 3×3 Cramer solve (with midpoint/endpoint fallback), version-based lazy-deletion min-heap, and optional boundary constraint planes. Params: target face count, max error threshold, boundary preservation.
- ~~**Mesh smoothing:**~~ — **DONE.** `Geometry.Smoothing` implements explicit uniform Laplacian smoothing, cotangent-weighted Laplacian smoothing (unnormalized cotan weights, clamped non-negative for stability on obtuse triangles), and Taubin shrinkage-free smoothing (λ/μ alternating passes with passband frequency control). All methods support boundary preservation.
- ~~**Curvature computation:**~~ — **DONE.** `Geometry.Curvature` computes mean curvature (Laplace-Beltrami of position with cotan weights), Gaussian curvature (angle defect with mixed Voronoi areas, per Meyer et al. 2003), principal curvatures (H ± √(H²−K)), and mean curvature normal vectors. Verified against Gauss-Bonnet theorem (Σ K_i·A_i = 2πχ).
- ~~**CG sparse solver:**~~ — **DONE.** `Geometry.DEC` now includes a Jacobi-preconditioned Conjugate Gradient solver (`SolveCG`) for SPD systems in CSR format, plus a shifted variant (`SolveCGShifted`) that solves (αM + βA)x = b without forming the combined matrix. These are the linear algebra backbone for the heat method and future implicit smoothing operators.
- ~~**Loop subdivision:**~~ — **DONE.** `Geometry.Subdivision` implements Loop 1987 subdivision with Warren's simplified β weights (β = 3/(8n) for n > 3, β = 3/16 for n = 3). Even vertex rule: interior `(1-nβ)v + β·Σneighbors`, boundary `1/8·prev + 3/4·v + 1/8·next`. Odd vertex rule: interior `3/8·(v0+v1) + 1/8·(v2+v3)`, boundary midpoint. Multi-iteration via ping-pong between two meshes. Preserves Euler characteristic on closed meshes. 8 tests.
- ~~**Isotropic remeshing:**~~ — **DONE.** `Geometry.Remeshing` implements Botsch & Kobbelt 2004 isotropic remeshing: (1) split edges > 4/3·target, (2) collapse edges < 4/5·target with neighbor length guard, (3) equalize valence via edge flips toward target valence 6 (interior) / 4 (boundary), (4) tangential Laplacian smoothing projected onto vertex normal plane. In-place modification with boundary preservation option. 7 tests.
- ~~**Geodesic distance:**~~ — **DONE.** `Geometry.Geodesic` implements the heat method (Crane, Weischedel & Wardetzky 2013): (1) solve heat diffusion (M + t·L)u = δ via shifted CG, (2) compute normalized negative gradient X = −∇u/|∇u| per face, (3) compute divergence via cotan-weighted edge integrals, (4) solve Poisson equation (ε·I + L)φ = div(X) with small diagonal regularization to break the constant null space. Time step t = h² (mean edge length squared). Supports multiple source vertices. 8 tests.

- ~~**Catmull-Clark subdivision:**~~ — **DONE.** `Geometry.CatmullClark` implements Catmull & Clark 1978 subdivision for arbitrary polygon meshes (triangles, quads, n-gons, or mixed). Face points = centroid, edge points = average of endpoints + adjacent face centroids (boundary: midpoint), vertex points = (Q/n + 2R/n + S(n-3)/n) where Q = avg face points, R = avg edge midpoints, S = original position, n = valence (boundary: cubic B-spline rule). Produces all-quad output after one iteration. Multi-iteration via ping-pong. Preserves Euler characteristic on closed meshes. Convergence verified on cube → sphere. 10 tests.
- ~~**Normal estimation:**~~ — **DONE.** `Geometry.NormalEstimation` estimates surface normals for unstructured point clouds using PCA-based local plane fitting (Hoppe et al. 1992). For each point, k nearest neighbors are found via octree KNN queries, a 3×3 covariance matrix is computed, and the normal is the eigenvector of the smallest eigenvalue (analytical eigendecomposition via Cardano's method for 3×3 symmetric matrices). Consistent orientation via minimum spanning tree (Prim's algorithm) on a Riemannian graph weighted by (1 − |nᵢ · nⱼ|), propagating from the highest-z seed point. 8 tests.
- ~~**Mesh repair:**~~ — **DONE.** `Geometry.MeshRepair` provides: (1) **Boundary loop detection** — traces all boundary halfedge cycles to identify holes. (2) **Hole filling** — advancing-front ear-clipping triangulation that iteratively fills the minimum-angle ear, with winding auto-correction. (3) **Degenerate triangle removal** — detects and removes faces below an area threshold, followed by garbage collection. (4) **Consistent face orientation** — BFS-based orientation propagation through connected components (in valid halfedge meshes, orientation is structurally consistent by construction). (5) **Combined repair pipeline** — runs all steps in sequence. 15 tests.

**Remaining operators:**
- ~~**Adaptive remeshing:**~~ — **DONE.** `Geometry.AdaptiveRemeshing` implements the Botsch & Kobbelt adaptive remeshing variant with a curvature-driven per-vertex sizing field: `L(v) = L_base / (1 + α·|H(v)|)` where H(v) is mean curvature and α is the configurable adaptation strength. Uses `Geometry.Curvature::ComputeCurvature()` for per-iteration curvature recomputation. Per-edge thresholds are the average of endpoint targets. Split/collapse/flip/smooth with local thresholds, boundary preservation, configurable min/max edge length bounds. 10 tests.
- ~~**Surface reconstruction:**~~ — **DONE.** Two modules: `Geometry.MarchingCubes` implements Lorensen & Cline 1987 isosurface extraction with full 256-entry lookup tables and grid-edge-indexed vertex welding (no hash maps). `Geometry.SurfaceReconstruction` implements Hoppe et al. 1992 signed distance approach: (1) compute or estimate normals, (2) build bounding box with padding, (3) construct scalar grid, (4) build octree for KNN queries, (5) evaluate signed distance field (single-nearest or inverse-distance-weighted average), (6) extract isosurface via Marching Cubes, (7) convert to HalfedgeMesh via `ToMesh()` (skips non-manifold triangles). ScalarGrid provides O(1) vertex lookup and world-space coordinate mapping. 25 tests (18 MC + 7 SR).
- ~~**Parameterization (UV mapping):**~~ — **DONE.** `Geometry.Parameterization` implements Least Squares Conformal Maps (LSCM) with automatic boundary pin selection via most-distant-pair arc-length heuristic. Assembles the conformal energy as a rectangular sparse system, forms normal equations A^T·A, solves via CG. Returns per-vertex UV coordinates with quality diagnostics: mean/max conformal distortion (singular value ratio), flipped triangle count, CG convergence status. 8 tests.
- ~~**Mesh quality metrics:**~~ — **DONE.** `Geometry.MeshQuality` computes comprehensive per-element and aggregate quality diagnostics: angle distribution (min/max/mean), aspect ratios (normalized equilateral = 1.0), edge lengths (min/max/mean/stddev), valence statistics, face areas, volume (divergence theorem), degenerate element counts, Euler characteristic, and boundary loop counts. Single-pass computation with configurable thresholds.
- ~~**Convex hull construction:**~~ — **DONE.** `Geometry.ConvexHullBuilder` implements the Quickhull algorithm (Barber, Dobkin & Huhdanpaa 1996) for 3D convex hull computation from point clouds. Produces the `Geometry::ConvexHull` struct with both V-Rep (vertices) and H-Rep (face planes), filling the gap where `ConvexHull` was consumed by GJK/SDF/SAT/Containment but had no builder in the geometry kernel. Features: initial tetrahedron via extreme-point search, conflict-list partitioning, BFS visible-face discovery, horizon-edge extraction, iterative expansion with edge-to-face adjacency tracking. Optional `Halfedge::Mesh` output for downstream mesh operations. `Build(span<vec3>)` + `BuildFromMesh()` convenience overload. O(n log n) expected, O(n²) worst-case. Epsilon-based robustness for near-coplanar/near-coincident points. 30 tests.
- ~~**Boolean operations:**~~ — **PARTIALLY DONE (baseline).** `Geometry.Boolean` provides CSG union/intersection/difference over closed `Halfedge::Mesh` inputs with exact behavior for disjoint and full-containment configurations, and an explicit `nullopt` fallback for partial-overlap cases that still need robust triangle clipping + stitched remeshing.

**Architecture notes:**
- Each operator follows a consistent pattern: input mesh → parameters struct → output/modified mesh + diagnostics. See `Geometry::Simplification::Simplify()` for the canonical example.
- Operators register via `FeatureRegistry` (§2.4) so they appear in the UI automatically.
- Long-running operators should execute on the task scheduler (`Core::Tasks`) with progress reporting.

**Lessons learned during implementation:**
- **Spatial debug should be DebugDraw-first:** keep visualization CPU-side and transient (`DebugDraw`) and reuse the transient path of `LinePass` for rendering; always provide coarse culling knobs (`MaxDepth`, `LeafOnly`, `OccupiedOnly`) to prevent debug visualizations from becoming the performance bottleneck.
- **Shader/C++ descriptor set index must match:** When adding new descriptor set layouts to a pipeline, the order of `AddDescriptorSetLayout()` calls must match the `set = N` indices declared in the shader. If the C++ adds layouts in order [globalSet, lineSSBOSet], the shader must use `set = 0` for globalSet and `set = 1` for lineSSBOSet. A mismatch (e.g., shader declares `set = 2` but only 2 layouts are added) causes "descriptor set N is out of bounds" validation errors.
- **DebugDraw timing is critical:** `DrawXxx()` calls that emit geometry into `DebugDraw` must happen **before** `RenderSystem::BuildGraph()` executes. If emission happens in ImGui panel callbacks (which run after render), the geometry appears one frame late or not at all (cleared by `DebugDraw.Reset()` before use). Move debug emission into `OnUpdate()` before the render system call.
- **Convex hull builder fills a V-Rep/H-Rep gap:** The `Geometry::ConvexHull` struct existed as a consumer type (GJK support, SDF evaluation, SAT, containment) with both V-Rep (vertices) and H-Rep (planes) — but the geometry kernel had no way to *build* one from raw points. The `ConvexHullBuilder` module closes this loop. When adding new primitives or spatial types, check whether the type is read-only in existing modules and needs a corresponding builder/cooker in the geometry kernel.

---

### 2.7 Data I/O

**Status: Phase 0 DONE.** Two-layer architecture implemented — I/O backend (how bytes are read) is separated from format loaders (how bytes become CPU objects). Loaders receive `std::span<const std::byte>` and never open files, enabling future migration to archive containers, `io_uring`, or DirectStorage without touching parser code.

**What exists:**
- `Core.IOBackend` — `IIOBackend` interface + `FileIOBackend` (Phase 0: `std::ifstream` read, `std::ofstream` write).
- `Graphics:IORegistry` — `IORegistry`, `IAssetLoader` / `IAssetExporter` interfaces, `ImportResult` variant (`MeshImportData`, `PointCloudImportData`, `GraphImportData`), `LoadContext`, `RegisterBuiltinLoaders()`, `RegisterBuiltinExporters()`.
- **8 I/O-agnostic importer partitions:** OBJ, PLY (ASCII + binary), XYZ, PCD (ASCII), TGF, glTF 2.0 / GLB, STL (ASCII + binary), OFF.
- **3 exporter partitions:** OBJ (ASCII), PLY (binary + ASCII), STL (binary + ASCII). `IAssetExporter::Export()` returns `std::expected<std::vector<std::byte>, AssetError>`. `IORegistry::Export()` convenience method finds exporter by extension, serializes, writes via `IIOBackend::Write()`.
- `ModelLoader` new overload accepting `IORegistry` + `IIOBackend`; `Engine` owns both, populates at startup.
- `Engine::LoadDroppedAsset` uses `IORegistry::CanImport()` instead of hardcoded extension list.
- 32 tests covering `FileIOBackend`, `AssetId`, `IORegistry` mechanics, in-memory byte parsing for all 8 import formats, and round-trip export/re-import for OBJ, PLY, STL.

**Remaining format support (incremental):**
- **Point cloud formats:** LAS/LAZ (LiDAR).
- **Scene formats:** FBX (via Assimp or ufbx), glTF materials/hierarchy/cameras/lights extraction.
- **Image formats:** PNG, JPEG, HDR/EXR (for environment maps and HDR textures).
- **Gaussian splat formats:** `.ply` (3DGS standard), `.splat` (compressed variants).

**Remaining architecture work:**
- Export on worker thread with progress callback.
- I/O backend Phase 1: archive/pack-file backend, async I/O.

---

### 2.8 Benchmarking & Profiling

**Context:** `Core::Telemetry` provides basic lock-free ring-buffered metrics. No GPU profiling, no reproducible benchmark infrastructure.

**Required:**
- **GPU timing:** Vulkan timestamp queries per render pass. Display per-pass timings in the Performance panel.
- **Pipeline statistics:** Vulkan pipeline statistics queries (vertex invocations, fragment invocations, clipping primitives) for performance analysis.
- **CPU frame profiling:** Per-system timing in the FrameGraph. Expose via telemetry.
- **Reproducible benchmark scenes:** Predefined scenes with known entity counts, geometry complexity, and camera paths for consistent measurement.
- **Benchmark runner:** Automated mode that runs N frames of a benchmark scene, collects min/avg/max/p99 frame times, GPU pass times, and memory usage, then outputs a report (JSON or CSV).
- **Regression detection:** Compare benchmark results across commits/branches. Flag regressions above a configurable threshold.
- **Memory profiling:** Track GPU memory allocation (via VMA statistics) and CPU allocator usage (LinearArena high watermarks, ScopeStack peak).

---

### 2.9 Clipping Planes & Cross-Sections

**Context:** No clipping support. Essential for inspecting interiors of scanned objects, point clouds, and volumetric data.

**Required:**
- **User-defined clip planes:** Up to N (e.g., 6) arbitrary clip planes, toggled and positioned via gizmos.
- **Cross-section rendering:** Fill the clipped surface with a solid color or hatch pattern to show the interior.
- **Clip volume:** Combine planes into a convex clip volume (box, frustum) for region-of-interest isolation.

---

### 2.10 Measurement & Annotation Tools

**Context:** No measurement tools exist. Common requirement for inspection and analysis workflows.

**Required:**
- **Point-to-point distance:** Click two points, display distance with a leader line.
- **Angle measurement:** Click three points, display the angle.
- **Area measurement:** Select a face region, compute and display total surface area.
- **Volume measurement:** Compute volume of a closed mesh (via divergence theorem — `Geometry.Properties` already has this).
- **Annotations:** Place text labels at 3D positions, persistently stored as ECS components.

---

### 2.11 Scene Serialization

**Context:** No save/load mechanism for scenes. Required for any practical workflow.

**Required:**
- **Scene save/load:** Serialize entity hierarchy, component data, asset references to a file format (JSON, binary, or glTF extension).
- **Undo/redo:** Command-pattern undo stack for all scene modifications (entity creation/deletion, component edits, selection changes).
- **Project files:** Store scene + asset references + editor layout as a project.

---

### Prioritization (Dependency-Ordered)

The ordering below follows the dependency graph: each phase builds on what the previous phase established. Items within a phase are ordered by impact.

```
Dependency graph (→ means "is required by"):

FeatureRegistry ──→ [DONE] All dependents can now use Core::FeatureRegistry

Data I/O ─────────→ everything (can't render what you can't load)

Rendering refactor → Post-processing (build HDR pipeline on clean pass architecture)
(PLAN.md)         → All subsequent rendering features (built on SurfacePass/LinePass/PointPass)

Post-processing ──→ Shadow mapping (composites into HDR buffer)
                ──→ Transparency / OIT (needs HDR blend target)
                ──→ Mesh rendering modes (HDR output assumed)
                ──→ Point cloud rendering (blending, tone mapping)

LinePass ─────────→ Transform gizmos (axis arrows, rings, handles)
                 ──→ Debug visualization (wireframe boxes, lines, arrows)
                 ──→ Graph / wireframe rendering (edges, layout)
                 ──→ Measurement tools (leader lines, angle arcs)
                 ──→ Clipping plane visualization (plane outlines)
                 ──→ Normal / tangent visualization (hedgehog lines)

Sub-entity select → Geometry processing (interactive operator input)
                  → Measurement tools (click-to-pick points)
```

#### Phase 0a — Rendering Architecture Refactor (PLAN.md)
*Consolidate the rendering pass architecture before adding new features. Building post-processing, shadows, and rendering modes on the old multi-pass architecture would require double-work when the refactor lands. Do the refactor first so all subsequent features are built on the clean three-pass foundation.*

1. **Unified pass consolidation (`PLAN.md` Phases 1–5)**
   *Depends on: nothing. Depended on by: everything — all rendering features should target the new pass architecture.*
   Define `ECS::Surface/Line/Point::Component` (done), consolidate ~~`ForwardPass` → `SurfacePass`~~ (done), `RetainedLineRenderPass` + `LineRenderPass` → `LinePass`, `RetainedPointCloudRenderPass` + `PointCloudRenderPass` → `PointPass`. Delete `MeshRenderPass`, `GraphRenderPass`, `RenderVisualization::Component`, `GeometryViewRenderer::Component`. Migrate lifecycle systems. Full migration spec in `PLAN.md`, actionable items in `TODO.md §1`.

#### Phase 0b — Architecture & Plumbing
*HDR pipeline and dirty-domain sync. Built on the refactored pass architecture.*

2. **Post-processing pipeline (§2.1.4)**
   *Depends on: rendering refactor (Phase 0a — builds on `SurfacePass`). Depended on by: shadow mapping, transparency, mesh rendering modes, point cloud blending.*
   The HDR intermediate render target and the post-pass chain (tone mapping at minimum). `SurfacePass` currently writes directly to the swapchain — every rendering feature added later assumes an HDR intermediate exists. Establish the plumbing now; individual effects (SSAO, bloom) can be added incrementally alongside other work.

3. **PropertySet dirty-domain sync (`TODO.md §3`)**
   *Depends on: rendering refactor (Phase 0a — sync system targets new component types). Depended on by: interactive geometry processing (operators that modify mesh topology/attributes need automatic GPU re-upload).*
   Per-frame CPU→GPU synchronization with independent dirty tracking per data domain (vertex/edge/face). Six dirty tag components, selective re-upload of affected PropertySet spans.

#### Phase 1 — Core UX
*Make the engine usable for interactive work. Without these, it's a viewer, not a tool.*

4. **Transform gizmos (§2.2.3)**
   *Depends on: `LinePass` (Phase 0a). Depended on by: nothing directly, but every editing workflow uses them.*
   Translate/rotate/scale handles. Without these, the only way to move objects is typing numbers into the Inspector panel. The `LinePass` transient path provides the axis arrows and rings.

5. **UI improvements (§2.5)**
   *Depends on: nothing (ImGui exists). Depended on by: all interactive workflows (usability multiplier).*
   Dockable panels (ImGui docking branch), viewport toolbar for render mode switching, keyboard shortcuts, console/log panel, context menus. Each improvement is independent — can be done incrementally alongside other work.

6. **Scene serialization + undo/redo (§2.11)**
   *Depends on: Data I/O (Phase 0, for asset references). Depended on by: any practical editing workflow.*
   Save/load scenes + command-pattern undo stack. Without this, all interactive editing is lost on exit. Becomes critical once gizmos and property editing exist.

---

#### Phase 2 — Rendering Variety
*Expand what the engine can show. Each item is a render feature registered via FeatureRegistry.*

7. **Mesh rendering modes (§2.1.3)**
   *Depends on: FeatureRegistry (DONE), post-processing / HDR target (Phase 0b). Depended on by: nothing (leaf features).*
   PBR extensions, flat, Gouraud/Phong, matcap, NPR (toon, Gooch, hatching), curvature/scalar field visualization, normal visualization, UV checker. Each shading mode is a variant PSO on `SurfacePass`. Share vertex pipeline, swap fragment shaders.

8. **Shadow mapping (§2.1.5)**
    *Depends on: post-processing pipeline (Phase 0b). Depended on by: nothing directly.*
    Cascaded shadow maps for directional lights. Critical for depth/spatial perception — without shadows, 3D scenes look flat. Shadow pass reuses `SurfacePass` geometry pipeline, writes depth only.

9. **Debug visualization of spatial structures (§2.3.1)**
    *Depends on: `LinePass` + DebugDraw API (Phase 0a). Depended on by: nothing (development tool).*
    Octree, BVH, bounding volumes, contact manifolds, normals/tangent frames, convex hulls — all rendered via DebugDraw transient path in `LinePass`. Toggled per-category in the UI. Essential for debugging every algorithm you build afterward.

10. **Benchmarking & profiling (§2.8)**
    *Depends on: nothing (Vulkan timestamp queries, `Core::Telemetry` exists). Depended on by: nothing directly, but establishes baselines before Phase 3+ adds heavy features.*
    GPU per-pass timing, CPU per-system timing, reproducible benchmark scenes, automated runner, regression detection. Best to establish baselines now so you can measure the cost of every feature added in later phases.

---

#### Phase 3 — Advanced Rendering
*New geometry types and rendering techniques. Each is a substantial feature building on Phase 0–2 infrastructure.*

11. **Point cloud rendering — advanced modes (§2.1.1)**
    *Depends on: FeatureRegistry, Data I/O (PLY/PCD/LAS loaders), `PointPass` (Phase 0a), post-processing (blending, tone mapping). Depended on by: transparency (splat blending).*
    Advanced `PointPass` modes: 3DGS compute rasterizer, Potree-style octree LOD streaming. Basic modes (FlatDisc, Surfel, EWA) already exist in the retained point rendering path. Each new mode = new shader + pipeline variant registered in `PointPass` (see `PLAN.md` extensibility model).

12. **Graph / wireframe rendering — full (§2.1.2 advanced)**
    *Depends on: `LinePass` + `PointPass` (Phase 0a). Depended on by: nothing.*
    Layout algorithm UI integration (force-directed, spectral, hierarchical), kNN-graph visualization, halfedge debug view, barycentric wireframe shader alternative. The `LinePass`/`PointPass` infrastructure handles the GPU side; this phase adds the interactive algorithms and UI.

13. **Transparency / OIT (§2.1.6)**
    *Depends on: post-processing pipeline (Phase 0b, HDR blend target). Depended on by: nothing directly, but improves point cloud and translucent surface rendering.*
    Weighted blended OIT as the default (simple, fast). Exact methods (linked lists, depth peeling) as alternatives.

---

#### Phase 4 — Advanced Interaction & Geometry Processing
*Sub-mesh selection and geometry operators for research workflows.*

14. **Sub-entity selection — vertex, edge, face, region (§2.2.2)**
    *Depends on: `LinePass` + `PointPass` (Phase 0a, for highlighting edges/vertices), HalfedgeMesh (exists). Depended on by: geometry processing operators, measurement tools.*
    Dedicated GPU picking pass writing `(EntityID, PrimitiveID, BarycentricCoords)`. Lasso/box/paint-brush/flood-fill/region-growing selection modes. Per-entity bitsets for selected sub-elements. This is the gateway to interactive geometry processing.

15. **Geometry processing operators (§2.6)** — **PARTIALLY DONE**
    *Depends on: FeatureRegistry (DONE), sub-entity selection (Phase 4, for interactive input). Depended on by: nothing (leaf features).*
    **Done:** Topological editing (collapse/flip/split), simplification (QEM), smoothing (Laplacian/cotan/Taubin), curvature computation (mean/Gaussian/principal), CG solver, Loop subdivision, isotropic remeshing, adaptive remeshing (curvature-driven sizing), geodesic distance, Catmull-Clark subdivision, normal estimation (PCA + MST orientation), mesh repair (hole filling, degenerate removal, consistent orientation), surface reconstruction (Marching Cubes + Hoppe SDF), parameterization (LSCM), mesh quality metrics, convex hull construction (Quickhull), and baseline Boolean operations for disjoint/full-containment CSG cases. **Remaining:** Exact partial-overlap Boolean clipping + stitched remeshing. Each operator follows the pattern: input → params → output + diagnostics. Registered via FeatureRegistry, invokable from UI and (later) scripting.

16. **Clipping planes & cross-sections (§2.9)**
    *Depends on: `LinePass` (Phase 0a, for plane visualization). Depended on by: nothing.*
    User-defined clip planes with gizmo positioning, cross-section fill, clip volumes.

17. **Measurement & annotation tools (§2.10)**
    *Depends on: sub-entity selection (Phase 4, for click-to-pick), `LinePass` (Phase 0a, for leader lines). Depended on by: nothing.*
    Point-to-point distance, angle, area, volume measurement. Persistent 3D annotations as ECS components.

---

#### Phase 5 — Long-term
*High-effort features that unlock new usage patterns.*

18. **Scripting layer (§2.4 Tier 3)**
    *Depends on: FeatureRegistry (DONE, stable API to bind). Depended on by: nothing.*
    Python (pybind11) or Lua (sol2) bindings for ECS manipulation, geometry operators, UI panels, and custom per-frame logic. Highest effort, but unlocks rapid prototyping of novel rendering and geometry methods without recompiling C++. Python especially valuable for ML/scientific workflow integration.

---

#### Ongoing (Carried forward from existing roadmap)
- **Port-based testing boundaries** — type-erased "port" interfaces for filesystem, windowing, and time so subsystems can be tested without Vulkan. (See §4.1.) Implement opportunistically as new subsystems are added.
- **Shader hot-reload (§2.4 Tier 2)** — file-watcher integration for automatic recompilation on save. Implement alongside mesh rendering modes (Phase 2) for fast shader iteration.

---

## 3. Non-goals (For this doc)

- Historical "fixed" issues (see Git history / PR descriptions).
- Implementation-level refactoring playbooks for already-landed fixes.

---

## 4. Open Questions (Callouts)

### 4.1 Subsystems: interfaces vs. concrete types

**Long-term answer:** **Concrete-by-default**, with dependency injection at construction and **interfaces/type-erased "ports" only at boundaries**.

- Use **concrete types** for hot-path subsystems (FrameGraph, scheduler/tasking, render graph execution, ECS update plumbing) to preserve inlining and keep inner loops vtable-free.
- Use **ports/adapters** (pure virtual or type-erased) for boundary dependencies: filesystem, file watching, OS window/surface creation, time, telemetry sinks, etc.

**Testing model:** instantiate concrete subsystems with fake ports (test doubles) rather than making the whole engine "virtual".

---

## 5. Rendering Modality Redesign Vision (Long-Term)

*This section describes the long-term approach/mode framework that the `PLAN.md` three-pass refactor enables. The three-pass architecture is a stepping stone toward this vision.*

### Problem Statement

The current runtime has strong per-feature rendering paths, but modality selection is not modeled as an explicit first-class contract. The long-term goal is a unified architecture where:

1. **Mesh rendering**, **graph rendering**, and **point cloud rendering** are explicit top-level rendering approaches.
2. Each approach can expose multiple **render modes** (e.g., points as flat discs, surfels, EWA splats, Potree-style screen-space sizing).
3. Approach + mode selection is user-toggleable at runtime (per-view and per-entity-class), while preserving GPU-driven batching and predictable frame graph behavior.

### Theoretical Model

Define a rendered sample set $\mathcal{S}$ for a scene view $v$:

$$\mathcal{S}(v) = \mathcal{S}_{mesh}(v) \cup \mathcal{S}_{graph}(v) \cup \mathcal{S}_{point}(v)$$

Each subset is generated by an **approach** $a \in \{mesh, graph, point\}$ and parameterized by a **mode** $m \in \mathcal{M}_a$. Visibility and shading output:

$$I(p) = \sum_{a}\sum_{m \in \mathcal{M}_a} \chi_{a,m}(v) \cdot \mathcal{R}_{a,m}(p; \Theta_{a,m}, \mathcal{G}_a)$$

- $\chi_{a,m}(v) \in \{0,1\}$ is the toggle gate (view/feature config).
- $\Theta_{a,m}$ are mode parameters (radius, attenuation, BRDF variant, splat bandwidth, etc.).
- $\mathcal{G}_a$ is geometry payload for that approach.

### Approach/Mode Matrix (Target)

**Mesh approach:** ShadedTriangles, Wireframe, VertexPoints, NormalsDebug.

**Graph approach:** LineList, TubeImpostor, NodeDiscs, EdgeHeatmap.

**Point approach:** FlatDisc, Surfel, EWA, PotreeAdaptive, GaussianSplat (optional).

PotreeAdaptive core policy: $r_{px} = \mathrm{clamp}(\alpha \cdot \frac{s_{world}}{z} f_y, r_{min}, r_{max})$ where $s_{world}$ is local spacing estimate, $z$ view depth, $f_y$ focal scale.

### Data-Oriented Design

**Core packet schema (SoA):** `ApproachPacketSoA` with `entityId`, `approach` (Mesh=0, Graph=1, Point=2), `mode`, `geometryHandle`, `materialHandle`, `transformHandle`, `modeParamIndex`. All hot iteration data in SoA, `StrongHandle<T>` / generational indices only, per-frame transient storage from linear allocator.

**Mode parameter table:** Packed, bindless-addressable table per approach (e.g., `PointModeParams { baseRadiusPx, ewaBandwidth, normalWeight, densityScale, flags }`).

**Geometry abstraction:** Unified `GeometryViewDescriptor { approach, positions, normals, indicesOrEdges, aux, count, layoutFlags }` keeping mesh/graph/point as peer entities.

### CPU Task Graph Integration

- **Render Approach Classifier Task:** Input: visible render proxies. Output: `ApproachPacketBuffer` (SoA) partitioned by approach and mode. Per-worker local bins then merge via prefix offsets. $O(N)$ over renderables.
- **Mode Policy Resolve Task:** Resolves effective mode from stacked policy levels (global defaults → view override → entity tag → debug forced mode).

### Frame Graph Passes

First-class passes: `MeshApproachPass`, `GraphApproachPass`, `PointApproachPass`. Each internally executes mode subpipelines via indirect draws/dispatches. Optional `PointModePreprocessCompute` for splat parameter conditioning.

### Runtime Config API (Target)

```cpp
struct RenderApproachConfig {
    bool enableMesh = true;
    bool enableGraph = true;
    bool enablePointCloud = true;
    MeshMode defaultMeshMode;
    GraphMode defaultGraphMode;
    PointMode defaultPointMode;
};

struct ViewRenderApproachOverride {
    std::optional<MeshMode> meshMode;
    std::optional<GraphMode> graphMode;
    std::optional<PointMode> pointMode;
    uint32_t disableMask;
};
```

### Migration from Three-Pass Architecture

The `PLAN.md` three-pass refactor establishes clean primitive ownership. The modality redesign adds:
- Phase 0: Introduce enums + config plumbing (no visual change).
- Phase 1: Extract/rename pass logic into approach passes.
- Phase 2: Graph approach formalization with mode params and UI.
- Phase 3: Point mode expansion (PotreeAdaptive, unified mode table).
- Phase 4: Cleanup legacy toggles and compatibility shims.

Runtime feature flag `RenderApproachV2` gates the full path for one-click rollback.

### Robustness Safeguards

1. Normals: renormalize with epsilon guard; fallback to view-facing basis.
2. EWA covariance: SPD enforcement by eigenvalue floor.
3. Potree radius: clamp to avoid popping and overdraw explosion.
4. Graph tubes: parallel transport frame fallback for Frenet-frame instability.
5. Depth conflicts: mode-specific depth bias policy.

### Performance Budgets

CPU (<2ms target): classification + packet build <0.25ms for 200k visible primitives, mode policy resolve <0.10ms. GPU: one indirect command buffer per approach, persistent pipelines, mode selection via push constants + mode param index.
