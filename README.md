# Intrinsic Engine

**Intrinsic** is a state-of-the-art research and rendering engine designed for high-performance geometry processing and real-time rendering. It bridges rigorous mathematical formalism with close-to-the-metal engine architecture.

Built on **C++23 Modules**, **Vulkan 1.3** bindless rendering, coroutine-based task scheduling, and a mathematically rigorous geometry kernel.

---

## Architectural Pillars

### 1. Core Systems & Concurrency

- **C++23 Modular Design:** Strict interface boundaries using `.cppm` partitions. `std::expected` for monadic error handling. No exceptions (`-fno-exceptions`).
- **Zero-Overhead Memory:**
  - `LinearArena` — O(1) monotonic frame allocator with O(1) bulk deallocation.
  - `ScopeStack` — LIFO allocator with destructor support for complex per-frame objects.
  - `InplaceFunction` — Small-buffer-optimized callable (64B SBO, move-only, zero-heap).
- **Hybrid Work-Stealing Task Scheduler:** Per-worker local deques execute local work in LIFO order, cross-worker stealing improves load balance, and an external inject queue handles non-worker producers. `Job` and `Yield()` provide cooperative coroutine multitasking.
- **Lock-Free Telemetry:** Ring-buffered telemetry system for real-time CPU frame times, draw calls, triangle counts, and per-pass GPU/CPU timing entries.
- **Benchmark Runner:** Deterministic `Core::Benchmark::BenchmarkRunner` captures per-frame snapshots (CPU/GPU/frame time, draw calls, triangles) with configurable warmup, computes percentile stats (avg/min/max/p95/p99), and writes structured JSON. Headless mode via `--benchmark <frames> --out file.json`. Threshold-based regression gate: `tools/check_perf_regression.sh`.
- **FrameGraph + DAG Scheduler:** ECS systems declare explicit data dependencies; `DAGScheduler` resolves topological execution order. Shared scheduling algorithm between FrameGraph and RenderGraph. The CPU ready-queue executor now dispatches dependents through a stable value-captured execution context, avoiding recursive lambda lifetime hazards under high worker counts.
- **AssetManager Read Phases:**
  - `AssetManager::Update()` is the single-writer phase on the main thread.
  - Parallel systems use `BeginReadPhase()` / `EndReadPhase()` brackets.
  - `AcquireLease()` for long-lived access across hot-reloads.
- **Registry-Driven Drag & Drop Import:** The runtime drop path delegates to `Graphics::IORegistry`, so any registered loader extension is accepted consistently by both file import and window drag-and-drop.

### 2. Geometry Processing Kernel

A **"Distinguished Scientist" grade** geometry kernel in `src/Runtime/Geometry/` (~11,000 lines):

**Collision & Spatial Queries:**
- **Primitives:** Spheres, AABBs, OBBs, Capsules, Cylinders, Convex Hulls.
- **GJK/EPA:** Gilbert-Johnson-Keerthi collision detection with Expanding Polytope Algorithm for contact points.
- **SDF:** Signed distance field evaluation with gradient-based contact manifold generation.
- **SAT:** Separating Axis Theorem for analytic primitive pair tests.
- **Linear Octree:** Cache-friendly spatial partitioning with Mean/Median/Center split strategies, tight bounds, and KNN queries.
- **KD-tree (new):** Octree-inspired spatial accelerator built over element AABBs (points, triangles, or other volumetric primitives), with overlap queries plus exact AABB-distance kNN/radius queries.

**Halfedge Mesh (PMP-style):**
- Full halfedge data structure with `VertexHandle`, `EdgeHandle`, `FaceHandle`, `HalfedgeHandle`.
- Euler operations: `EdgeCollapse` (Dey-Edelsbrunner link condition), `EdgeFlip`, `EdgeSplit`.
- Arbitrary polygon support: `AddTriangle`, `AddQuad`, `AddFace(span<VertexHandle>)`.
- Property system with typed per-element storage and garbage collection.
- **Attribute propagation contract (PMP-style):**
  - Topology edits (`Split` / `Collapse`) preserve *typed* per-vertex properties when you opt-in via
    `Halfedge::Mesh::SetVertexAttributeTransferRules()`.
  - For each property name (e.g. `"v:texcoord"`, `"v:color"`) you can choose a policy:
    `Average` (interpolate), `KeepA`, `KeepB`, or `None`.
  - This is the engine-side equivalent of PMP's “property lifecycle” and is required if you want
    dependent attributes (texcoords/colors/weights, etc.) to remain valid after remeshing or
    simplification.


**Graph Processing Operators:**
- **kNN Graph Builders:** `Geometry::Graph::BuildKNNGraph()` (Octree-accelerated neighbor discovery, exact kNN) and `Geometry::Graph::BuildKNNGraphFromIndices()` (manual graph assembly from precomputed kNN index lists) both support Union/Mutual connectivity and epsilon-based coincident-point rejection for degenerate robustness.
- **Graph Layouts:** Fruchterman-Reingold force-directed embedding (`ComputeForceDirectedLayout`), spectral embedding (`ComputeSpectralLayout`) with both combinatorial and symmetric-normalized Laplacian variants, and deterministic hierarchical layering (`ComputeHierarchicalLayout`) for stable DAG/tree-style 2D graph visualization. The hierarchical solver now performs a diameter-aware auto-root selection (two-sweep BFS, root at path midpoint) for components when no explicit root is provided, reducing depth skew on long chains.
- **Surface Reconstruction Robustness:** Weighted signed distance uses an adaptive Gaussian kernel with normal-consistency weighting and automatic invalid-normal filtering before Marching Cubes extraction.

**Mesh Processing Operators:**
| Operator | Algorithm | Reference |
|---|---|---|
| **Simplification** | Garland-Heckbert QEM edge collapse | Garland & Heckbert 1997 |
| **Smoothing** | Uniform / Cotangent / Taubin Laplacian | Botsch et al. 2010 |
| **Curvature** | Mean, Gaussian, Principal (angle defect + mixed Voronoi) | Meyer et al. 2003 |
| **Loop Subdivision** | Warren's simplified weights, boundary rules | Loop 1987, Warren 1995 |
| **Catmull-Clark Subdivision** | Arbitrary polygon mesh, all-quad output | Catmull & Clark 1978 |
| **Isotropic Remeshing** | Split/collapse/flip/smooth with target edge length | Botsch & Kobbelt 2004 |
| **Geodesic Distance** | Heat method (diffusion + Poisson solve) | Crane et al. 2013 |
| **Normal Estimation** | PCA local plane fitting + MST orientation | Hoppe et al. 1992 |
| **Mesh Repair** | Hole filling (ear-clipping), degenerate removal, orientation | — |
| **Marching Cubes** | Isosurface extraction from scalar fields with vertex welding | Lorensen & Cline 1987 |
| **Surface Reconstruction** | Point cloud → mesh via robust oriented SDF (Gaussian + normal-consistency weighting) + Marching Cubes | Hoppe et al. 1992 |
| **Adaptive Remeshing** | Curvature-driven split/collapse/flip/smooth with per-vertex sizing field | Botsch & Kobbelt 2004 |
| **Parameterization** | LSCM (Least Squares Conformal Maps) with auto boundary pinning | Lévy et al. 2002 |
| **Mesh Quality** | Angle, aspect ratio, edge length, valence, area, volume diagnostics | — |
| **Convex Hull** | Quickhull 3D (V-Rep + H-Rep + optional Halfedge::Mesh output) | Barber et al. 1996 |
| **Boolean CSG (baseline)** | Union/intersection/difference for disjoint/full-containment meshes, conservative fallback on partial overlaps | — |

**Discrete Exterior Calculus (DEC):**
- Exterior derivatives d0, d1 in CSR sparse matrix format.
- Hodge star operators: diagonal mass matrices for 0-forms, 1-forms, 2-forms.
- Cotan Laplacian: L = d0^T * star1 * d0 (positive semidefinite).
- Jacobi-preconditioned Conjugate Gradient solver (`SolveCG`, `SolveCGShifted`).

All operators follow a consistent contract: `Params` struct with defaults, `Result` struct with diagnostics, `std::optional<Result>` return for degenerate input.

Adaptive remeshing now exposes runtime safety controls in `AdaptiveRemeshingParams` for robustness on pathological inputs:
`MaxOpsPerIteration` bounds split/collapse work per pass, and `MaxTopologyGrowthFactor` caps vertex/edge growth relative to the input mesh.

### 3. Rendering (Vulkan 1.3)

- **Bindless Architecture:** Full `VK_EXT_descriptor_indexing` for bindless textures.
- **Render Graph:**
  - Automatic dependency tracking and barrier injection (VK_KHR_synchronization2).
  - Transient resource aliasing (memory reuse).
  - Lambda-based pass declaration: `AddPass<Data>(setup, execute)`.
  - Debug/audit introspection now records per-pass attachment metadata (format, load/store ops, imported vs. transient) and per-resource first/last read-write provenance for UI inspection and temporary render-path logging.
  - Structured validation (`ValidateCompiledGraph`) returns `RenderGraphValidationResult` with typed diagnostics (error/warning). Imported-resource write policies (`ImportedResourceWritePolicy`) enforce which passes may write to imported resources (e.g., only `Present.LDR` may write to the Backbuffer). Missing required resources and transient resources without producers are now errors rather than warnings.
- **Async Transfer System:**
  - Staging Belt: Persistent ring-buffer allocator for CPU-to-GPU streaming.
  - Timeline Semaphore synchronization for async asset uploads.
  - No loader thread ever calls `vkWaitForFences` for texture uploads.
- **GPUScene:** Retained-mode instance table with independent slot allocation/deallocation.
- **Dynamic Rendering:** No `VkRenderPass` or `VkFramebuffer`; fully dynamic attachment binding.
- **Three-Pass Architecture:** Unified `SurfacePass` (filled triangles), `LinePass` (thick anti-aliased edges), and `PointPass` (billboard/surfel points) — each handling both retained BDA and transient debug data internally. Per-pass ECS components (`ECS::Surface::Component`, `ECS::Line::Component`, `ECS::Point::Component`) gate rendering via presence/absence. `DefaultPipeline` currently schedules: Picking, Surface, Line, Point, Composition, PostProcess, SelectionOutline, DebugView, ImGui, then a final `Present` blit when an intermediate LDR target is active.
- **Composition Strategy:** The `ICompositionStrategy` interface abstracts how scene geometry is composed into `SceneColorHDR`. The active strategy (`ForwardComposition` for now) is selected by `FrameLightingPath` and determines: (1) which canonical resource geometry passes write to, (2) whether G-buffer channels are needed, and (3) what composition passes run between geometry and post-processing. The interface is designed for future `DeferredComposition` / `HybridComposition` / `ForwardPlusComposition` implementations without changing pass registration or the render graph.
- **Canonical Render Resources + Frame Recipe:** Render setup is now recipe-driven rather than hard-coded in `RenderSystem`. `FrameRecipe` declares which canonical targets are needed for a frame (`SceneDepth`, `EntityId`, `PrimitiveId`, `SceneNormal`, `Albedo`, `Material0`, `SceneColorHDR`, `SceneColorLDR`). `SelectionMask` and `SelectionOutline` remain reserved canonical resource definitions for a future split-outline path, but they are not required by the current selection recipe. A single `FrameSetup` pass imports the swapchain backbuffer and depth image, creates only the requested transient intermediates, and seeds the blackboard with stable resource IDs.
- **HDR Post-Processing / Presentation:** Scene geometry writes to canonical `SceneColorHDR`. `PostProcessPass` tone maps into canonical `SceneColorLDR` (with an internal temporary when AA is enabled), with optional color grading (lift/gamma/gain, saturation, contrast, white balance) applied in linear space after tone mapping. An optional **luminance histogram** compute pass bins log-luminance from `SceneColorHDR` into a 256-bin SSBO (GPU→CPU readback), displaying the distribution and average luminance in the View Settings panel for exposure debugging. Overlay passes (`SelectionOutline`, viewport `DebugView`, `ImGui`) compose onto the current presentation target, which is `SceneColorLDR` when post/overlay stages are active and otherwise the imported backbuffer. A final `Present` stage copies `SceneColorLDR` into the swapchain image explicitly, eliminating implicit backbuffer ownership assumptions from earlier passes.
- **Line/Graph Rendering:** `LinePass` renders via BDA shared vertex buffers with persistent per-entity edge index buffers. Wireframe edges share mesh vertex buffers (same device address) with edge topology view. `line.vert` expands segments to screen-space quads (6 verts/segment) with anti-aliasing. Graph entities (`ECS::Graph::Data`) hold `shared_ptr<Geometry::Graph>` with PropertySet-backed data authority, CPU-side layout algorithms (force-directed, spectral, hierarchical), and persistent GPU buffers managed by `GraphGeometrySyncSystem`. All retained passes do CPU-side frustum culling.
- **Point Cloud Rendering:** `PointPass` renders via BDA shared vertex buffers. `point_flatdisc.vert` / `point_surfel.vert` expand points to billboard quads (6 verts/point). Four render modes: FlatDisc (camera-facing billboard), Surfel (normal-oriented disc with Lambertian shading), EWA splatting (Zwicker et al. 2001 — perspective-correct elliptical Gaussian splats via per-surfel Jacobian projection with 1px² low-pass anti-aliasing), and Sphere impostors (depth-writing sphere billboards for correct point/mesh occlusion). Surfel/EWA require real per-point normals; if normals are absent, runtime falls back to FlatDisc. The `Geometry.PointCloud` CPU module (data structures, downsampling, statistics) remains functional. Registered drag-and-drop/import point-cloud formats now include `.xyz`, `.pts`, `.xyzrgb`, `.txt`, and `.pcd` (ASCII + binary PCD). The XYZ importer also tolerates common scan-export rows with `LH<n>` scan markers and semicolon-delimited `x; y; z;` samples.
- **DebugDraw:** Immediate-mode transient overlay for dynamic debug visualization (contact manifolds, ad hoc instrumentation, and short-lived editor overlays) rendered by `LinePass` and `PointPass` transient paths. Depth-tested and overlay sub-passes. Comprehensive test coverage.
- **Graph Processing (CPU):** Halfedge-based graph topology with Octree-accelerated kNN construction, force-directed 2D layout, spectral embedding (combinatorial/symmetric-normalized Laplacian), hierarchical layered layout with crossing diagnostics and diameter-aware auto-rooting.
- **Transform Gizmos:** `Graphics::TransformGizmo` is now an `ImGuizmo`-backed editor wrapper instead of a custom `DebugDraw` manipulator. The engine caches selection/camera state during `OnUpdate()`, then executes the gizmo during the active ImGui frame through a lightweight overlay callback so transform interaction stays in the same input/render path as the rest of the editor UI. World/local space, snap increments, and pivot strategies (centroid / first-selected) remain configurable from the viewport toolbar. For parented entities, manipulation happens in world space and is converted back to the child’s parent-local `Transform::Component` via `Transform::TryComputeLocalTransform()`, so local-space editing follows the composed world orientation rather than the raw stored local TRS. Multi-selection uses a shared pivot and applies the resulting world-space delta matrix to each cached selected entity.
- **Selection Outlines:** Post-process contour highlight for selected/hovered entities using canonical `EntityId` input. Three outline modes: Solid (constant color), Pulse (animated alpha oscillation), and Glow (distance-based falloff). Configurable fill overlay tints selected and hovered entities with semi-transparent color. Editor hierarchy selection is subtree-aware: selecting a non-renderable parent still resolves the `PickID`s of renderable descendants, so imported model roots outline correctly. All parameters (colors, width, mode, fill alpha, pulse speed, glow falloff) adjustable at runtime via View Settings panel.
