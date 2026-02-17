# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks **what's left to do** in IntrinsicEngine's architecture.

**Policy:** If something is fixed/refactored, it should **not** remain as an "issue" here. We rely on Git history for the past.

---

## 0. Scope & Success Criteria

**Goal:** a data-oriented, testable, modular engine architecture with:

- Deterministic per-frame orchestration (CPU + GPU), explicit dependencies.
- Robust multithreading contracts in Core/RHI.
- Minimal "god objects"; subsystems testable in isolation.

---

## 1. Open TODOs (What's left)

### ~~1.1 Pre-existing Build Environment Issues~~ — RESOLVED (Clang 20)

The following Clang 18 issues are resolved by the upgrade to Clang 20 as the minimum compiler:

- ~~**Clang 18 `__cpp_concepts` mismatch:**~~ Clang 20 reports the correct value. `CMakeLists.txt` workaround removed.
- ~~**C++20 module partition visibility:**~~ Clang 20 handles transitive imports correctly. Redundant `import` statements in partition implementations (e.g., `RHI.Device.cpp`) remain as defensive practice but are no longer required.
- ~~**Clang 18 lambda noexcept deduction:**~~ Clang 20 correctly deduces nothrow-movability. `Core::InplaceFunction` strict `is_nothrow_move_constructible` static_assert restored.

---

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
- Line rendering needs a dedicated `LineRenderFeature` with GPU-side line storage (SSBO of segments).
- Thick lines via screen-space expansion in vertex shader (Vulkan has no guaranteed wide-line support).
- Node rendering can reuse point splatting infrastructure.

**Status:** Core line rendering infrastructure is DONE — see Phase 0 item 3. `LineRenderPass` provides the SSBO-based thick-line backend. **kNN graph construction is now available as an Octree-accelerated exact builder (`Geometry::Graph::BuildKNNGraph()`) and as manual build from precomputed neighbor index lists (`Geometry::Graph::BuildKNNGraphFromIndices()`) with Union/Mutual connectivity and degenerate-pair filtering.** `BuildKNNGraph()` now uses `Geometry::Octree::QueryKnn()` per source vertex, improving expected neighborhood-construction complexity from $O(n^2)$ brute force toward $O(n \log n + nk)$ on well-distributed point sets while preserving exact top-$k$ neighbor ordering. The graph module now also exposes **Fruchterman-Reingold force-directed embedding** (`Geometry::Graph::ComputeForceDirectedLayout()`), including degenerate-distance stabilization for coincident initial layouts. The graph module now also exposes **spectral 2D embedding** (`Geometry::Graph::ComputeSpectralLayout()`) via projected orthogonal iteration with selectable combinatorial or symmetric-normalized Laplacian operators, improving stability on irregular-degree graphs while preserving deterministic low-frequency embeddings. **A deterministic hierarchical layered embedding is now available** (`Geometry::Graph::ComputeHierarchicalLayout()`), combining BFS level assignment with barycentric crossing-minimization sweeps for stable DAG/tree-style visualization. Remaining work: mesh wireframe overlay (barycentric), render-feature/UI integration for kNN + layout visualization, halfedge debug view. All build on the existing `DebugDraw` + `LineRenderPass` primitives.

---

#### 2.1.3 Mesh Rendering Modes

**Context:** Currently only a single forward PBR pass (metallic-roughness) exists via `ForwardPass`. Need configurable shading for research and artistic workflows.

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

#### ~~2.2.1 Selection Visual Feedback (Contour Highlight)~~ — DONE

**Implementation:** Post-process outline rendering via `SelectionOutlinePass` (`Graphics.Passes.SelectionOutline.cppm/.cpp`).

- **Approach:** Fullscreen fragment shader reads the PickID buffer (R32_UINT, written by `PickingPass`), samples 8 neighbors at configurable radius, edge-detects selection/hover boundaries, and alpha-blends outlines onto the backbuffer.
- **Selection outline:** Orange (1.0, 0.6, 0.0) solid outline for entities with `SelectedTag`.
- **Hover highlight:** Light blue (0.3, 0.7, 1.0) semi-transparent outline for entity with `HoveredTag`.
- **Configurable:** Outline color, hover color, and width passed via push constants. Supports up to 16 simultaneously selected entities.
- **Pipeline integration:** Runs as step 3 in `DefaultPipeline::RebuildPath()` (after Forward, before DebugView/ImGui). Gated by `FeatureRegistry` (`"SelectionOutlinePass"_id`). Descriptor set updated via `PostCompile()` after `RenderGraph::Compile()`.
- **New files:** `selection_outline.frag` shader, `Graphics.Passes.SelectionOutline.cppm/.cpp`.
- **RHI addition:** `PipelineBuilder::EnableAlphaBlending()` for standard src-alpha/one-minus-src-alpha blending.

**Remaining enhancements (future work):**
- **Animation:** Pulsing outline via time-based alpha modulation.
- **JFA-based distance field:** For smoother, glow-style outlines at arbitrary widths (current approach uses discrete neighbor sampling).
- **Per-entity outline color:** Via per-entity SSBO instead of push constants (for more than 16 selections or custom colors).

---

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
- ~~**Octree:**~~ — **DONE.** Sandbox can render the selected entity’s `MeshCollider` `Geometry::Octree` as color-by-depth wire boxes via `Graphics::DebugDraw` + `LineRenderPass`. Controls live in `View Settings → Spatial Debug` (max depth, leaf-only, occupied-only, overlay/depth-tested).
- **KD-tree:** Wireframe splitting planes + leaf bounding boxes (KD-tree needs to be implemented first — see §2.6).
- **BVH (Bounding Volume Hierarchy):** Wireframe AABBs/OBBs at each BVH node level, with configurable max display depth.
- **Uniform grid:** Wireframe cells with occupancy coloring.
- ~~**Bounding volumes:**~~ — **DONE.** Sandbox can render selected `MeshCollider` bounds as world AABB, world OBB, and conservative bounding sphere overlays. Per-type toggles, independent colors, alpha, and overlay/depth-tested routing are exposed in `View Settings → Spatial Debug`.
- **Contact manifolds:** Render contact points and normals from `Geometry.ContactManifold`.

**Architecture notes:**
- Use the `LineRenderFeature` (from §2.1.2) as the backend for all debug drawing.
- Debug draw calls should go through an immediate-mode API: `DebugDraw::Line(a, b, color)`, `DebugDraw::Box(aabb, color)`, `DebugDraw::Sphere(center, radius, color)`, etc.
- All debug geometry is transient — rebuilt each frame from `LinearArena`.
- Toggled per-category via the UI (§2.5).

**Status:** Core API and rendering backend are DONE — see Phase 0 item 3. `DebugDraw` provides the immediate-mode accumulator with `Line`, `Box`, `WireBox`, `Sphere`, `Circle`, `Arrow`, `Axes`, `Frustum`, `Grid`, `Cross` plus overlay variants. Remaining work: per-category UI toggles (§2.5), contact manifold rendering, convex hull overlay. These are incremental additions to the existing `DebugDraw` API.

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
- **Status bar:** Frame time, entity count, GPU memory, active render mode at a glance.
- **Keyboard shortcuts:** Configurable hotkeys for common operations (select mode, transform mode, render mode toggle).
- **Dark/light theme:** Configurable ImGui theme with presets.
- **Context menus:** Right-click menus on entities (delete, duplicate, rename, focus camera).

---

### 2.6 Geometry Processing Operators

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
- **Adaptive remeshing:** Curvature-aware sizing field for non-uniform remeshing. The isotropic infrastructure (split/collapse/flip/smooth) is in place — extend with per-vertex target lengths driven by curvature estimates.
- ~~**Surface reconstruction:**~~ — **DONE.** Two modules: `Geometry.MarchingCubes` implements Lorensen & Cline 1987 isosurface extraction with full 256-entry lookup tables and grid-edge-indexed vertex welding (no hash maps). `Geometry.SurfaceReconstruction` implements Hoppe et al. 1992 signed distance approach: (1) compute or estimate normals, (2) build bounding box with padding, (3) construct scalar grid, (4) build octree for KNN queries, (5) evaluate signed distance field (single-nearest or inverse-distance-weighted average), (6) extract isosurface via Marching Cubes, (7) convert to HalfedgeMesh via `ToMesh()` (skips non-manifold triangles). ScalarGrid provides O(1) vertex lookup and world-space coordinate mapping. 25 tests (18 MC + 7 SR).
- **Boolean operations:** CSG union/intersection/difference. Built on `HalfedgeMesh` + exact/robust predicates.
- **Parameterization (UV mapping):** LSCM, ABF++, or Boundary First Flattening for UV unwrapping.

**Architecture notes:**
- Each operator follows a consistent pattern: input mesh → parameters struct → output/modified mesh + diagnostics. See `Geometry::Simplification::Simplify()` for the canonical example.
- Operators register via `FeatureRegistry` (§2.4) so they appear in the UI automatically.
- Long-running operators should execute on the task scheduler (`Core::Tasks`) with progress reporting.

**Lessons learned during implementation:**
- **Spatial debug should be DebugDraw-first:** keep visualization CPU-side and transient (`DebugDraw`) and reuse `LineRenderPass` for rendering; always provide coarse culling knobs (`MaxDepth`, `LeafOnly`, `OccupiedOnly`) to prevent debug visualizations from becoming the performance bottleneck.
- **Shader/C++ descriptor set index must match:** When adding new descriptor set layouts to a pipeline, the order of `AddDescriptorSetLayout()` calls must match the `set = N` indices declared in the shader. If the C++ adds layouts in order [globalSet, lineSSBOSet], the shader must use `set = 0` for globalSet and `set = 1` for lineSSBOSet. A mismatch (e.g., shader declares `set = 2` but only 2 layouts are added) causes "descriptor set N is out of bounds" validation errors.
- **DebugDraw timing is critical:** `DrawXxx()` calls that emit geometry into `DebugDraw` must happen **before** `RenderSystem::BuildGraph()` executes. If emission happens in ImGui panel callbacks (which run after render), the geometry appears one frame late or not at all (cleared by `DebugDraw.Reset()` before use). Move debug emission into `OnUpdate()` before the render system call.

---

### 2.7 Data I/O

**Status: Phase 0 DONE.** Two-layer architecture implemented — I/O backend (how bytes are read) is separated from format loaders (how bytes become CPU objects). Loaders receive `std::span<const std::byte>` and never open files, enabling future migration to archive containers, `io_uring`, or DirectStorage without touching parser code.

**What exists:**
- `Core.IOBackend` — `IIOBackend` interface + `FileIOBackend` (Phase 0: `std::ifstream`).
- `Graphics:IORegistry` — `IORegistry`, `IAssetLoader` / `IAssetExporter` interfaces, `ImportResult` variant (`MeshImportData`, `PointCloudImportData`, `GraphImportData`), `LoadContext`, `RegisterBuiltinLoaders()`.
- **5 I/O-agnostic importer partitions:** OBJ, PLY (ASCII + binary), XYZ, TGF, glTF 2.0 / GLB.
- `ModelLoader` new overload accepting `IORegistry` + `IIOBackend`; `Engine` owns both, populates at startup.
- `Engine::LoadDroppedAsset` uses `IORegistry::CanImport()` instead of hardcoded extension list.
- 21 tests covering `FileIOBackend`, `AssetId`, `IORegistry` mechanics, and in-memory byte parsing for all 5 formats.

**Remaining format support (incremental):**
- **Mesh formats:** STL, OFF.
- **Point cloud formats:** PCD (Point Cloud Library format), LAS/LAZ (LiDAR).
- **Scene formats:** FBX (via Assimp or ufbx), glTF materials/hierarchy/cameras/lights extraction.
- **Image formats:** PNG, JPEG, HDR/EXR (for environment maps and HDR textures).
- **Gaussian splat formats:** `.ply` (3DGS standard), `.splat` (compressed variants).

**Remaining architecture work:**
- Export pipeline (`IAssetExporter` interface exists but no concrete exporters yet).
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

Post-processing ──→ Shadow mapping (composites into HDR buffer)
                ──→ Transparency / OIT (needs HDR blend target)
                ──→ Mesh rendering modes (HDR output assumed)
                ──→ Point cloud rendering (blending, tone mapping)

Line rendering ───→ Transform gizmos (axis arrows, rings, handles)
               ───→ Debug visualization (wireframe boxes, lines, arrows)
               ───→ Graph / wireframe rendering (edges, layout)
               ───→ Measurement tools (leader lines, angle arcs)
               ───→ Clipping plane visualization (plane outlines)
               ───→ Normal / tangent visualization (hedgehog lines)

Sub-entity select → Geometry processing (interactive operator input)
                  → Measurement tools (click-to-pick points)
```

#### Phase 0 — Architecture & Plumbing
*Everything else plugs into these. Build them first and every subsequent feature snaps in cleanly.*

~~1. **Extension architecture / FeatureRegistry (§2.4 Tier 1)** — DONE.~~ `Core::FeatureRegistry` (`Core.FeatureRegistry.cppm/.cpp`) provides type-erased registration by category (RenderFeature, GeometryOperator, Panel, System) with factory-based instance creation, enable/disable, and query APIs. 27 dedicated tests in `IntrinsicCoreTests`.

~~1. **Data I/O (§2.7)**~~ — DONE (Phase 0).
   `Core.IOBackend` (`Core.IOBackend.cppm/.cpp`) provides `IIOBackend` interface with `FileIOBackend`. `Graphics:IORegistry` (`Graphics.IORegistry.cppm/.cpp`) provides `IORegistry` with `IAssetLoader`/`IAssetExporter` and `RegisterBuiltinLoaders()`. Five I/O-agnostic importer partitions (OBJ, PLY, XYZ, TGF, GLTF) under `Importers/`. `ModelLoader` new overload routes through the registry. `Engine` owns and wires the subsystem. 21 tests in `IntrinsicTests`. Remaining formats (STL, OFF, PCD, LAS/LAZ, FBX, image, splat) and export pipeline are incremental.

2. **Post-processing pipeline (§2.1.4)**
   *Depends on: nothing (rendering infrastructure). Depended on by: shadow mapping, transparency, mesh rendering modes, point cloud blending.*
   The HDR intermediate render target and the post-pass chain (tone mapping at minimum). Currently the forward pass writes directly to the swapchain — every rendering feature added later assumes an HDR intermediate exists. Establish the plumbing now; individual effects (SSAO, bloom) can be added incrementally alongside other work.

~~3. **Line rendering + DebugDraw API (§2.1.2 infrastructure + §2.3 API)** — DONE.~~ `DebugDraw` (`Graphics.DebugDraw.cppm/.cpp`) provides an immediate-mode CPU-side accumulator with depth-tested and overlay line lists. API: `Line()`, `Box()`, `WireBox()`, `Sphere()`, `Circle()`, `Arrow()`, `Axes()`, `Frustum()`, `Grid()`, `Cross` plus overlay variants. `LineRenderPass` (`Graphics.Passes.Line.cppm/.cpp`) implements `IRenderFeature` with screen-space thick-line expansion in the vertex shader (no geometry shader) — each segment becomes a 6-vertex quad via SSBO. Two pipeline variants: depth-tested (`VK_COMPARE_OP_LESS_OR_EQUAL`, no depth write) and overlay (depth test disabled). Per-frame SSBOs with power-of-2 growth. Anti-aliased edges via smoothstep in fragment shader. Integrated into `DefaultPipeline` (step 4, after SelectionOutline, before DebugView). `RenderOrchestrator` owns `DebugDraw` lifetime, threaded via `RenderSystem` → `RenderPassContext` → `LineRenderPass`. 32 tests in `IntrinsicTests`.

---

#### Phase 1 — Core UX
*Make the engine usable for interactive work. Without these, it's a viewer, not a tool.*

~~5. **Selection visual feedback / contour highlight (§2.2.1)** — DONE.~~ `SelectionOutlinePass` (`Graphics.Passes.SelectionOutline.cppm/.cpp`) provides post-process outline rendering for selected and hovered entities. Reads PickID buffer, edge-detects boundaries, alpha-blends configurable outlines onto backbuffer. Supports up to 16 simultaneous selections. `PipelineBuilder::EnableAlphaBlending()` added to RHI.

6. **Transform gizmos (§2.2.3)**
   *Depends on: line rendering (Phase 0). Depended on by: nothing directly, but every editing workflow uses them.*
   Translate/rotate/scale handles. Without these, the only way to move objects is typing numbers into the Inspector panel. The line rendering infrastructure from Phase 0 provides the axis arrows and rings.

7. **UI improvements (§2.5)**
   *Depends on: nothing (ImGui exists). Depended on by: all interactive workflows (usability multiplier).*
   Dockable panels (ImGui docking branch), viewport toolbar for render mode switching, keyboard shortcuts, console/log panel, context menus. Each improvement is independent — can be done incrementally alongside other work.

8. **Scene serialization + undo/redo (§2.11)**
   *Depends on: Data I/O (Phase 0, for asset references). Depended on by: any practical editing workflow.*
   Save/load scenes + command-pattern undo stack. Without this, all interactive editing is lost on exit. Becomes critical once gizmos and property editing exist.

---

#### Phase 2 — Rendering Variety
*Expand what the engine can show. Each item is a render feature registered via FeatureRegistry.*

9. **Mesh rendering modes (§2.1.3)**
   *Depends on: FeatureRegistry (DONE), post-processing / HDR target (Phase 0). Depended on by: nothing (leaf features).*
   PBR extensions, flat, Gouraud/Phong, matcap, NPR (toon, Gooch, hatching), curvature/scalar field visualization, normal visualization, UV checker. Each shading mode is a variant PSO registered as a render feature. Share vertex pipeline, swap fragment shaders.

10. **Shadow mapping (§2.1.5)**
    *Depends on: post-processing pipeline (Phase 0). Depended on by: nothing directly.*
    Cascaded shadow maps for directional lights. Critical for depth/spatial perception — without shadows, 3D scenes look flat. Shadow pass reuses existing geometry pipeline, writes depth only.

11. **Debug visualization of spatial structures (§2.3.1)**
    *Depends on: line rendering + DebugDraw API (Phase 0). Depended on by: nothing (development tool).*
    Octree, BVH, bounding volumes, contact manifolds, normals/tangent frames, convex hulls — all rendered via DebugDraw. Toggled per-category in the UI. Essential for debugging every algorithm you build afterward.

12. **Benchmarking & profiling (§2.8)**
    *Depends on: nothing (Vulkan timestamp queries, `Core::Telemetry` exists). Depended on by: nothing directly, but establishes baselines before Phase 3+ adds heavy features.*
    GPU per-pass timing, CPU per-system timing, reproducible benchmark scenes, automated runner, regression detection. Best to establish baselines now so you can measure the cost of every feature added in later phases.

---

#### Phase 3 — Advanced Rendering
*New geometry types and rendering techniques. Each is a substantial feature building on Phase 0–2 infrastructure.*

13. **Point cloud rendering (§2.1.1)**
    *Depends on: FeatureRegistry, Data I/O (PLY/PCD/LAS loaders), post-processing (blending, tone mapping). Depended on by: graph rendering (kNN-graph), transparency (splat blending).*
    3DGS, Potree-style LOD, flat splatting, EWA splatting, surfels. Point clouds as a first-class `GeometryType` with dedicated GPU buffer layout. The largest single feature — start with flat splatting, iterate toward 3DGS.

14. **Graph / wireframe rendering — full (§2.1.2 advanced)**
    *Depends on: line rendering (Phase 0), point cloud rendering (for kNN-graph on point clouds). Depended on by: nothing.*
    Layout algorithms (force-directed, spectral, hierarchical) for abstract graphs, kNN-graph visualization, halfedge debug view. The line rendering infrastructure from Phase 0 handles the GPU side; this phase adds the algorithms.

15. **Transparency / OIT (§2.1.6)**
    *Depends on: post-processing pipeline (Phase 0, HDR blend target). Depended on by: nothing directly, but improves point cloud and translucent surface rendering.*
    Weighted blended OIT as the default (simple, fast). Exact methods (linked lists, depth peeling) as alternatives.

---

#### Phase 4 — Advanced Interaction & Geometry Processing
*Sub-mesh selection and geometry operators for research workflows.*

16. **Sub-entity selection — vertex, edge, face, region (§2.2.2)**
    *Depends on: line rendering (Phase 0, for highlighting edges/vertices), HalfedgeMesh (exists). Depended on by: geometry processing operators, measurement tools.*
    Dedicated GPU picking pass writing `(EntityID, PrimitiveID, BarycentricCoords)`. Lasso/box/paint-brush/flood-fill/region-growing selection modes. Per-entity bitsets for selected sub-elements. This is the gateway to interactive geometry processing.

17. **Geometry processing operators (§2.6)** — **PARTIALLY DONE**
    *Depends on: FeatureRegistry (DONE), sub-entity selection (Phase 4, for interactive input). Depended on by: nothing (leaf features).*
    **Done:** Topological editing (collapse/flip/split), simplification (QEM), smoothing (Laplacian/cotan/Taubin), curvature computation (mean/Gaussian/principal), CG solver, Loop subdivision, isotropic remeshing, geodesic distance, Catmull-Clark subdivision, normal estimation (PCA + MST orientation), mesh repair (hole filling, degenerate removal, consistent orientation). **Remaining:** Adaptive remeshing, surface reconstruction, boolean operations, UV parameterization. Each operator follows the pattern: input → params → output + diagnostics. Registered via FeatureRegistry, invokable from UI and (later) scripting.

18. **Clipping planes & cross-sections (§2.9)**
    *Depends on: line rendering (Phase 0, for plane visualization). Depended on by: nothing.*
    User-defined clip planes with gizmo positioning, cross-section fill, clip volumes.

19. **Measurement & annotation tools (§2.10)**
    *Depends on: sub-entity selection (Phase 4, for click-to-pick), line rendering (Phase 0, for leader lines). Depended on by: nothing.*
    Point-to-point distance, angle, area, volume measurement. Persistent 3D annotations as ECS components.

---

#### Phase 5 — Long-term
*High-effort features that unlock new usage patterns.*

20. **Scripting layer (§2.4 Tier 3)**
    *Depends on: FeatureRegistry (DONE, stable API to bind). Depended on by: nothing.*
    Python (pybind11) or Lua (sol2) bindings for ECS manipulation, geometry operators, UI panels, and custom per-frame logic. Highest effort, but unlocks rapid prototyping of novel rendering and geometry methods without recompiling C++. Python especially valuable for ML/scientific workflow integration.

---

#### Ongoing (Carried forward from existing roadmap)
- **Port-based testing boundaries** — type-erased "port" interfaces for filesystem, windowing, and time so subsystems can be tested without Vulkan. (See §4.1.) Implement opportunistically as new subsystems are added.
- ~~**Migrate `std::function` hot paths to `Core::InplaceFunction`**~~ — **DONE.** `Graphics::RenderStage::ExecuteFn` (`Graphics.RenderPath.cppm`) and `RHI::VulkanDevice` deferred deletion queues (`RHI.Device.cppm`) now use `Core::InplaceFunction`. `std::function` remains only in cold paths (UI callbacks, asset loading, startup config) per policy. The strict `is_nothrow_move_constructible` static_assert is restored now that Clang 20 is the minimum compiler.
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

### ~~4.2 `std::function` in hot loops~~ — RESOLVED

**Answer:** **Ban `std::function` in hot loops.** Three alternatives are standardized:

1. **Thunk + context pointer**: `{ void(*Fn)(void*), void* Ctx }` — already used in FrameGraph.
2. ~~**`Core::InplaceFunction`**~~ — **DONE.** `Core.InplaceFunction.cppm` provides a move-only, zero-heap, small-buffer owning callable (`InplaceFunction<R(Args...), BufferSize>`). Default 64-byte buffer. 46 dedicated tests in `IntrinsicCoreTests`. **Migration complete:** `Graphics::RenderStage::ExecuteFn` and `RHI::VulkanDevice` deferred deletion queues now use `InplaceFunction`.
3. **Arena-backed closures** — allocate capture payload out of `ScopeStack` / per-frame arena and store only thunk+ctx.

**Policy:** `std::function` is acceptable in cold paths (editor UI, startup config, tooling) but not in per-frame/per-entity loops.

---

### ~~4.3 FrameGraph vs RenderGraph: shared algorithm extracted~~ — DONE

Removed (see Git history). `Core::DAGScheduler` extracts the shared scheduling algorithm; both FrameGraph and RenderGraph delegate to it. 18 dedicated tests.

---

### ~~4.4 Clang 18 vtable emission failure with module partitions~~ — RESOLVED (Clang 20)

**Status:** Resolved. The `Sandbox` target links successfully with Clang 20.

**What was the problem:** Clang 18 did not reliably emit vtables when a polymorphic class was declared in a module partition interface (`.cppm`) and its virtual methods were defined in a separate partition implementation (`.cpp`). This caused `undefined reference to 'vtable for ...'` linker errors for `DefaultPipeline`, `ForwardPass`, `PickingPass`, `SelectionOutlinePass`, and `DebugViewPass`.

**Resolution:** Upgraded minimum compiler to Clang 20, which correctly handles vtable emission across module partition boundaries. Existing vtable anchor patterns (out-of-line destructors in `Graphics.Pipelines.cppm` and `Graphics.IORegistry.cpp`) are retained as defensive practice.
