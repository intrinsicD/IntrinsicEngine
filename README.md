# Intrinsic Engine

**Intrinsic** is a state-of-the-art research and rendering engine designed for high-performance geometry processing and real-time rendering. It bridges rigorous mathematical formalism with close-to-the-metal engine architecture.

Built on **C++23 Modules**, **Vulkan 1.3** bindless rendering, coroutine-based task scheduling, and a mathematically rigorous geometry kernel.

---

## Canonical Architecture Docs

- `docs/architecture/rendering-three-pass.md` — pass contracts, render-resource invariants, and render-graph expectations.
- `docs/architecture/frame-loop-rollback-strategy.md` — staged-frame-loop rollback toggle, compatibility-shim policy, and pass/fail cutover gates.
- `docs/architecture/runtime-subsystem-boundaries.md` — runtime ownership map, module dependency directions, and startup/per-frame/shutdown lifecycle.
- `docs/architecture/feature-module-playbook.md` — standard feature-module contract, layering rules, and refactor workflow for reusable development.
- `docs/architecture/post-merge-audit-checklist.md` — stabilization checklist for architecture-touching merges and required post-merge validation gates.
- `docs/architecture/vectorfield-overlay-lifecycle-invariants.md` — explicit lifecycle invariants for vector-field overlays (factory, ECS tags, lifecycle sync, extraction).
- `docs/architecture/ground-up-redesign-blueprint-2026.md` — full-state redesign blueprint for a 3-graph runtime (CPU tasks, GPU frame graph, async streaming), robust geometry contracts, and migration gates.
- `docs/architecture/ground-up-redesign-vision.md` — deep architectural assessment of what to preserve, what to change (10 areas), and a phased implementation priority stack.

Runtime code follows a subsystem-first access policy: `Engine` acts as the composition root and frame-loop orchestrator, while lower-level GPU, scene, asset, and render state is accessed through the owning subsystem getters (`GetGraphicsBackend()`, `GetAssetPipeline()`, `GetSceneManager()`, `GetRenderOrchestrator()`).

## Build & Test Entry Points

- **Supported toolchain:** Ninja + Clang 20+ + CMake 3.28+.
- **Preset policy:** the repository now codifies CUDA selection with explicit configure presets so the default developer path stays stable:
  - `dev` — Debug + tests + Sandbox, **CUDA OFF**
  - `dev-cuda` — Debug + tests + Sandbox, **CUDA ON**
  - `ci` — Debug + tests, Sandbox OFF, **CUDA OFF**
- **Why this matters:** `INTRINSIC_ENABLE_CUDA` is opt-in. Enabling/disabling the backend is now an explicit configure choice rather than an accidental local cache carry-over.

### Configure

```bash
cmake --preset dev
```

Enable the optional CUDA backend only when you actually want the CUDA modules and have a working CUDA toolkit/driver install:

```bash
cmake --preset dev-cuda
```

### Targeted builds

Keep builds narrow; do not rebuild the whole tree unless you need to.

```bash
cmake --build --preset dev --target IntrinsicRuntime
cmake --build --preset dev --target IntrinsicTests
```

### Focused test run

```bash
./build/dev/bin/IntrinsicTests --gtest_filter="RuntimeFrameLoop.*"
```

### Compile hotspot benchmark (CI parity)

Capture and gate the slowest Ninja compile edges after a build:

```bash
python3 ./tools/compile_hotspots.py \
  --build-dir build \
  --top 40 \
  --json-out build/compile_hotspots_report.json \
  --baseline-json tools/compile_hotspot_baseline.json
```

---

## Architectural Pillars

### 1. Core Systems & Concurrency

- **C++23 Modular Design:** Strict interface boundaries using `.cppm` partitions. `std::expected` for monadic error handling. No exceptions (`-fno-exceptions`).
  - Prefer narrow named-module imports in implementation units; avoid umbrella imports when a file only needs a small surface (for example, use `Geometry:Handle` for handle-only code paths instead of importing the full `Geometry` module).
- **Zero-Overhead Memory:**
  - `LinearArena` — O(1) monotonic frame allocator with O(1) bulk deallocation.
  - `ScopeStack` — LIFO allocator with destructor support for complex per-frame objects.
  - `InplaceFunction` — Small-buffer-optimized callable (64B SBO, move-only, zero-heap).
  - `CommandHistory` — bounded undo/redo stack built on `std::move_only_function` plus `CmdComponentChange<T>` ECS snapshots for inspector and gizmo edits. Geometry operators (simplify, remesh, smooth, subdivide, repair) capture full mesh state via `shared_ptr<Mesh>` snapshots for undo/redo with GPU re-upload.
- **Hybrid Work-Stealing Task Scheduler:** Per-worker local deques execute local work in LIFO order, cross-worker stealing improves load balance, and an external inject queue handles non-worker producers. `Job` and `Yield()` provide cooperative coroutine multitasking.
- **Lock-Free Telemetry:** Ring-buffered telemetry system for real-time CPU frame times, draw calls, triangle counts, per-pass GPU/CPU timing entries, and per-heap GPU memory budget tracking (via `VK_EXT_memory_budget` / VMA). The Performance panel shows live device-local usage bars with color-coded warnings at 80% threshold.
- **Console/Log Panel:** Ring-buffer log sink (2048 entries) captures all `Core::Log` output. The Console panel provides scrollable, filterable, per-level (Info/Warn/Error/Debug) log viewing with `ImGuiTextFilter`-based search, auto-scroll, and clear. `TakeSnapshot()` decouples UI rendering from logging threads (no mutex held during ImGui drawing).
- **Benchmark Runner:** Deterministic `Core::Benchmark::BenchmarkRunner` captures per-frame snapshots (CPU/GPU/frame time, draw calls, triangles) with configurable warmup, computes percentile stats (avg/min/max/p95/p99), and writes structured JSON. Headless mode via `--benchmark <frames> --out file.json`. Threshold-based regression gate: `tools/check_perf_regression.sh`.
- **FrameGraph + DAG Scheduler:** ECS systems declare explicit data dependencies; `DAGScheduler` resolves topological execution order. Shared scheduling algorithm between FrameGraph and RenderGraph. The CPU ready-queue executor now dispatches dependents through a stable value-captured execution context, avoiding recursive lambda lifetime hazards under high worker counts.
- **Frame-loop seam hosts:** `Runtime::FrameLoop` exposes narrow platform, streaming, maintenance, and render-lane host interfaces so the engine’s per-frame ordering can be tested with lightweight fakes instead of requiring a full Vulkan/runtime boot. Maintenance work now has its own headless-safe seam, so transfer retirement can be exercised without a swapchain-backed render path. The rollout contract is now explicit: `FrameLoop.StagedPhases` is the default path, while `FrameLoop.LegacyCompatibility` remains a one-window rollback shim during frame-pipeline migration cutovers.
- **Platform-stage coordinator:** `Runtime::PlatformFrameCoordinator` now owns per-frame event pumping, minimize/quit gating, framebuffer-resize signal consumption, and sanitized frame-time sampling via `Runtime::FrameClock`, so `Engine::Run()` consumes one explicit platform-stage result before entering the streaming/fixed/render lanes. The coordinator now enforces the GLFW/SDL thread contract directly: event pumping is main-thread-only, and cross-thread calls are rejected before any platform host method runs.
- **Activity-aware frame pacing:** `Runtime::ActivityTracker` monitors GLFW input events (key, mouse button, scroll, char, drop, resize) and dynamically adjusts the frame rate. When no activity is detected for `IdleTimeoutSeconds` (default 2 s), the engine drops to `IdleFps` (default 15 fps); any input instantly restores the active rate. `FramePacingConfig` in `EngineConfig` exposes `ActiveFps` (0 = VSync-only), `IdleFps`, `IdleTimeoutSeconds`, and an `Enabled` switch. The default swapchain present mode is now `VSync` (`VK_PRESENT_MODE_FIFO_KHR`), which naturally limits frame rate to the display refresh without CPU busy-looping. `FrameClock::Resample()` re-anchors the clock after deliberate sleeps so the next frame does not see inflated frame time. Sandbox forwards `--max-active-fps`, `--idle-fps`, `--idle-timeout`, and `--no-idle-throttle`.
- **Configurable fixed-step simulation policy:** `Runtime::MakeFrameLoopPolicy()` centralizes the simulation accumulator contract with explicit defaults (`60 Hz`, `0.25 s` max frame delta, `8` max substeps) plus validated overrides for higher-frequency modes such as `120 Hz`. Sandbox forwards `--fixed-hz`, `--max-frame-dt`, `--max-substeps`.
- **Per-frame fixed-step telemetry:** the runtime now records fixed-step tick count, accumulator clamp hits, and simulation CPU time into `Core::Telemetry::TelemetrySystem` every frame, and the ImGui telemetry panel surfaces those counters under **Fixed-Step Simulation** for runtime verification of the staged loop.
- **Authoritative fixed-tick commits:** `Runtime::SceneManager::CommitFixedTick()` now advances a monotonically increasing committed-world generation after each completed fixed-step graph execution. `CreateReadonlySnapshot()` captures that generation into a lightweight `WorldSnapshot`, establishing the first explicit authoritative-world handoff seam for later extraction-stage work.
- **Typed render extraction seam:** `Runtime::RenderExtraction` now defines first-class `RenderFrameInput`, `RenderWorld`, `RenderViewport`, and `FrameContext` carriers. The runtime render lane now constructs render input from the committed `WorldSnapshot`, camera state, swapchain extent, and fixed-step interpolation factor $\alpha = \mathrm{clamp}(\text{accumulator} / \Delta t_{\text{fixed}}, 0, 1)$, then hands immutable extracted `RenderWorld` data into `RenderOrchestrator::{BeginFrame, ExtractRenderWorld, PrepareFrame, ExecuteFrame, EndFrame}` instead of letting application code call `RenderDriver::OnUpdate(...)` directly.
- **Explicit authoritative handoff:** the render lane now extracts the immutable `RenderWorld N+1` immediately after deferred event dispatch and before the application `OnRender(...)` hook. `RenderWorld` now carries frozen selection/picking, surface, line, point, HTEX preview, **light/environment**, **pick-request** (`PickRequestSnapshot`), **debug-view** (`DebugViewSnapshot`), and **GPU-scene** (`GpuSceneSnapshot`) packets. `Graphics::RenderDriver::BuildGraph(...)` no longer receives `ECS::Scene` and no longer queries live `InteractionSystem` state, so late UI/editor work cannot mutate render inputs out from under render preparation. Scene lighting (`Graphics::LightEnvironmentPacket`) is extracted once per frame into `RenderWorld::Lighting` and uploaded to the global camera UBO (`CameraBufferObject` at set 0 binding 0) so all lit shaders consume uniform data instead of hardcoded constants.
- **Bounded frame-context ring:** `Runtime::FrameContextRing` now owns a dedicated logical frame ring distinct from swapchain image count. Each `FrameContext` carries the prepared immutable `RenderWorld` snapshot for that slot, so `RenderOrchestrator::ExecuteFrame()` consumes frame-owned state instead of an orchestrator-global pending world. The default engine policy is double-buffered frame contexts (`2`), while `EngineConfig::FrameContextCount` may opt into `3` for throughput-heavy modes without changing the extraction/render orchestration API.
- **Bounded fence waits for responsiveness:** `RHI::SimpleRenderer::BeginFrame()` now waits on the in-flight fence with a short timeout instead of hard-blocking indefinitely. If the GPU is still busy, the frame is skipped and the loop stays responsive; if the device is lost, the renderer now logs and shuts down gracefully instead of aborting in the fence wait path.
- **Timeline-based resize drain:** Swapchain resize now uses `VulkanDevice::WaitForGraphicsIdle()` (timeline-semaphore `vkWaitSemaphores`) instead of `vkDeviceWaitIdle`, draining only the graphics queue so concurrent transfer-queue uploads continue uninterrupted. `FrameContextRing::InvalidateAfterResize()` resets stale timeline/submitted state on all frame-context slots after the drain, preventing double-waits on the next frame.
- **Staged renderer execution seam:** `Graphics::RenderDriver` now exposes explicit frame phases (`BeginFrame`, `AcquireFrame`, `ProcessCompletedGpuWork`, `UpdateGlobals`, `BuildGraph`, `ExecuteGraph`, `EndFrame`). `Runtime::RenderOrchestrator::PrepareFrame()` now moves the extracted `RenderWorld` into the owning `FrameContext` and performs CPU-side graph construction strictly from extracted packet/state inputs, while `ExecuteFrame()` is reduced to graph execution/submission. This removes the old one-shot render-update entry point and keeps render-graph build/compile/execute under renderer-owned frame execution.
- **Structured render preparation input:** `Graphics::BuildGraphInput` consolidates all per-frame extracted render state (camera, lighting, selection, picking, draw packets, debug draw, editor overlay) into a single non-owning struct. `RenderDriver::BuildGraph()` accepts `(AssetManager&, const BuildGraphInput&)` instead of 18 splayed parameters, establishing a clean data contract between the extraction stage (`RenderWorld`) and graph construction. `RenderOrchestrator::PrepareFrame()` constructs the input via designated initializers from the owned `RenderWorld`, ensuring span lifetimes are valid through graph build.
- **Stable render-context lifetime:** `Graphics::RenderDriver::BuildGraph()` now copies its transient `RenderPassContext` into the per-frame `ScopeStack` before pipeline setup, so render-graph closures can safely execute on worker threads after the build function returns. Transient `DebugDraw` data is now wired directly through the active pipeline instead of being threaded through per-pass context copies.
- **CUDA driver context safety:** `RHI::CudaDevice` now wraps each public CUDA driver call in a scoped `cuCtxPushCurrent` / `cuCtxPopCurrent`, so buffer allocation/free, stream creation/destruction, stream synchronization, and memory queries remain valid from any engine thread and restore any previously-current foreign CUDA context on that thread.
- **Async point-domain k-means workflow:** `Geometry::KMeans` now provides a Lloyd-style clustering operator with random or hierarchical farthest-point seeding. The Inspector’s unified **Geometry Processing** section exposes k-means by capability rather than asset-type panel: it runs on mesh vertices, graph nodes, and point-cloud points whenever authoritative data is present. Results publish back into domain-native PropertySets (`v:kmeans_*` for mesh/graph vertices, `p:kmeans_*` for point clouds) and refresh visuals through the existing dirty-tag/property-sync path. Point-cloud centroids are also materialized as ordinary retained scene entities with `ECS::PointCloud::Data`, so they render through the standard `PointPass` instead of living only as side data. Mesh results also feed the Htex patch preview path so the selected mesh can be rendered as a float atlas of KMeans clusters; the preview pass now caches patch metadata and signature inputs between mesh/k-means edits so stable frames skip the heavy rebuild path. When CUDA is enabled, the accelerated backend is available for any authoritative 3D point set; point clouds keep a persistent device cache for reuse, while meshes and graphs use transient snapshot buffers per dispatch. CUDA teardown now synchronizes each KMeans stream before freeing cached buffers, and the CUDA Lloyd solver batches point assignments into smaller tiles per iteration to keep the renderer responsive under large point counts.
- **AssetManager Read Phases:**
  - `AssetManager::Update()` is the single-writer phase on the main thread.
  - Parallel systems use `BeginReadPhase()` / `EndReadPhase()` brackets.
  - `AcquireLease()` for long-lived access across hot-reloads.
- **Registry-Driven Drag & Drop Import:** `Runtime::AssetIngestService` owns drag-drop/re-import orchestration and delegates format selection to `Graphics::IORegistry`, so any registered loader extension is accepted consistently by both file import and window drag-and-drop. Drag-drop requests now flow through the runtime streaming lane as an explicit ingest state machine before main-thread materialization/spawn.
- **Editor Geometry Workflow Hygiene:** When the editor reconstructs a `Halfedge::Mesh` from render/collider triangle soup, it now routes through UV-aware conversion helpers (`Geometry::MeshUtils::BuildHalfedgeMeshFromIndexedTriangles` / `ExtractIndexedTriangles`). Coincident vertices are welded only when their UVs also agree, so texture seams survive rebuilds instead of being averaged away, and edited meshes round-trip their `v:texcoord` property back into `Aux.xy` for GPU upload.

### 2. Geometry Processing Kernel

A **"Distinguished Scientist" grade** geometry kernel in `src/Runtime/Geometry/` (~11,000 lines):

**Collision & Spatial Queries:**
- **Primitives:** Spheres, AABBs, OBBs, Capsules, Cylinders, Convex Hulls.
- **Sphere fitting:** `Geometry::ToSphere(points, params)` now supports robust best-fit and conservative bounding policies on borrowed point spans. The default hybrid policy filters non-finite samples, uses an algebraic least-squares fit when the system is well-conditioned, and falls back to an AABB-derived bounding sphere on singular input. `EnforceContainment` can inflate the fitted result to cover every retained sample with an absolute slack.
- **GJK/EPA:** Gilbert-Johnson-Keerthi collision detection with Expanding Polytope Algorithm for contact points.
- **SDF:** Signed distance field evaluation with gradient-based contact manifold generation.
- **SAT:** Separating Axis Theorem for analytic primitive pair tests.
- **Linear Octree:** Cache-friendly spatial partitioning with Mean/Median/Center split strategies, tight bounds, KNN queries, and a direct `Octree::BuildFromPoints(...)` helper for point-set workflows that would otherwise hand-lift positions into zero-volume AABBs.
- **KD-tree (new):** Octree-inspired spatial accelerator built over element AABBs (points, triangles, or other volumetric primitives), with overlap queries plus exact AABB-distance kNN/radius queries.
- **Primitive BVH (new):** Flat local-space BVH built over primitive AABBs with exact payload caches (triangle/segment/point primitives). Attached per entity through `ECS::PrimitiveBVH::Data`, rebuilt by `PrimitiveBVHBuild`, and consumed first by CPU picking for mesh triangles and graph segments.

**Halfedge Mesh (PMP-style):**
- Full halfedge data structure with `VertexHandle`, `EdgeHandle`, `FaceHandle`, `HalfedgeHandle`.
- Euler operations: `EdgeCollapse` (Dey-Edelsbrunner link condition), `EdgeFlip`, `EdgeSplit`.
- Arbitrary polygon support: `AddTriangle`, `AddQuad`, `AddFace(span<VertexHandle>)`.
- **Primitive mesh builders:** `Halfedge::MakeMesh(...)` now lifts analytic primitives directly into retained halfedge topology: `AABB` / `OBB` become 6-quad closed boxes, `Sphere` uses an icosphere refinement path, `Ellipsoid` applies an affine transform of that sphere, `Cylinder` builds a capped revolution mesh, `Capsule` adds hemispherical end-caps, and `MakeMeshTetrahedron()` / `MakeMeshOctahedron()` / `MakeMeshIcosahedron()` / `MakeMeshDodecahedron()` expose canonical platonic-solid fixtures for geometry operators and tests.
- Zero-allocation traversal helpers for common adjacency walks are centralized in `Geometry::Circulators`, and `Halfedge::ConstMeshView` / `Halfedge::Mesh` reuse them for `HalfedgesAroundFace`, `VerticesAroundFace`, `HalfedgesAroundVertex`, `FacesAroundVertex`, `BoundaryHalfedges`, and `BoundaryVertices`. `Halfedge::Mesh` now binds directly to caller-supplied `PropertySet`s for vertices, halfedges, edges, and faces; the default constructor is the only path that allocates its own `MeshProperties` bundle. `ConstMeshView` exposes a dedicated read-only `ConstPropertySet` façade, so const geometry code cannot accidentally request mutable property handles. Read-only geometry code should prefer `ConstMeshView`; topology-editing algorithms should take `Mesh` (mutable view). The same read-only boundary now applies to `Geometry::Graph::Graph` and `Geometry::PointCloud::Cloud`, and the editor widgets consume `ConstPropertySet` views instead of taking mutable property-set pointers.
- Property system with typed per-element storage and garbage collection.
- **Attribute propagation contract (PMP-style):**
  - Topology edits (`Split` / `Collapse`) preserve *typed* per-vertex properties when you opt-in via
    `Halfedge::Mesh::SetVertexAttributeTransferRules()`.
  - For each property name (e.g. `"v:texcoord"`, `"v:color"`) you can choose a policy:
    `Average` (interpolate), `KeepA`, `KeepB`, or `None`.
  - `Average` is evaluated from the requested split/collapse position projected onto the source edge,
    i.e. $u' = (1-t)u_a + t u_b$ with $t = \mathrm{clamp}\left(\frac{(x' - x_a)\cdot(x_b-x_a)}{\|x_b-x_a\|^2}, 0, 1\right)$.
    This keeps UV transfer coherent for QEM simplification, isotropic remeshing, and adaptive remeshing,
    where the new vertex is not always the exact midpoint.
  - The editor geometry workflow now enables this contract automatically for `"v:texcoord"`, so
    remeshing and simplification keep UVs coherent instead of regenerating planar coordinates.
  - Triangle-soup bridge helpers preserve UVs across CPU mesh ↔ halfedge conversion, and Loop /
    Catmull-Clark subdivision propagate `"v:texcoord"` with the same refinement stencils used for
    positions. Polygonal subdivision output is fan-triangulated on extraction so quad faces round-trip
    back into the render/upload path with `Aux.xy` intact.
- **Mesh-backed analysis results:** mutable overloads of `Curvature`, `Geodesic`, and `Parameterization`
  publish their outputs as persistent mesh properties in addition to returning vectors/diagnostics.
  Examples: `v:mean_curvature`, `v:gaussian_curvature`, `v:mean_curvature_normal`,
  `v:geodesic_distance`, `v:is_geodesic_source`, `v:texcoord`, and `v:lscm_pinned`.
- **KMeans Htex Scalar Atlas:** the Htex preview path consumes mesh `v:kmeans_*` properties and bakes a float scalar atlas in the background, then only commits the texture once the worker signals readiness. Each texel samples its enclosing triangle in object space and classifies that sample against the authoritative retained centroid array from the last k-means publish; if no centroid array is retained, it falls back to the label-derived reconstruction path. This preserves the expected centroidal Voronoi-style structure instead of reusing per-triangle nearest-vertex partitions. The preview pass caches patch metadata and the last desired signature so unchanged meshes avoid re-enumerating edges and rehashing labels/colors every frame. The atlas is intentionally scalar-field data so the existing UI colormap/debug-view path can colorize it downstream; only truly degenerate / non-finite triangles are rejected, so valid sliver faces still participate.
- **Shortest Path:** `Geometry::ShortestPath::Dijkstra()` now supports both `Halfedge::Mesh` and `Graph::Graph`
  with the same multi-source / multi-target contract. It writes persistent vertex properties
  (`v:shortest_path_distance`, `v:shortest_path_predecessor`) so path trees can be visualized or reused after
  the query completes. `Geometry::ShortestPath::ExtractPathGraph()` converts the predecessor tree into an actual
  `Geometry::Graph::Graph`, which makes single routes and multi-source / multi-goal path networks easy to inspect
  or render. Extracted graphs also carry `v:original_vertex` for provenance. Empty-set behavior is explicit: both
  empty returns `std::nullopt`, empty sources seed a reverse tree rooted at the targets, and empty targets seed a
  forward tree rooted at the sources.


**Graph Processing Operators:**
- **kNN Graph Builders:** `Geometry::Graph::BuildKNNGraph()` (Octree-accelerated neighbor discovery, exact kNN) and `Geometry::Graph::BuildKNNGraphFromIndices()` (manual graph assembly from precomputed kNN index lists) both support Union/Mutual connectivity and epsilon-based coincident-point rejection for degenerate robustness.
- **Graph Layouts:** Fruchterman-Reingold force-directed embedding (`ComputeForceDirectedLayout`), spectral embedding (`ComputeSpectralLayout`) with both combinatorial and symmetric-normalized Laplacian variants, and deterministic hierarchical layering (`ComputeHierarchicalLayout`) for stable DAG/tree-style 2D graph visualization. The hierarchical solver now performs a diameter-aware auto-root selection (two-sweep BFS, root at path midpoint) for components when no explicit root is provided, reducing depth skew on long chains.
- **Surface Reconstruction Robustness:** Weighted signed distance uses an adaptive Gaussian kernel with normal-consistency weighting and automatic invalid-normal filtering before Marching Cubes extraction.

**Mesh Processing Operators:**
| Operator | Algorithm | Reference |
|---|---|---|
| **Simplification** | Configurable QEM with plane / triangle / point quadrics, selectable quadric residence (vertices, faces, or both), minimizer-aware placement policies, plus normal-cone deviation tracking and Hausdorff error bounds; `Geometry::Quadric` also exposes probabilistic plane/triangle factories for uncertainty-aware QEFs | Garland & Heckbert 1997; Trettner & Kobbelt 2020 |
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
| **Mesh Analysis** | Per-vertex/edge/halfedge/face defect markers plus issue masks for editor inspection | — |
| **Convex Hull** | Quickhull 3D (V-Rep + H-Rep + optional Halfedge::Mesh output) | Barber et al. 1996 |
| **Boolean CSG (baseline)** | Union/intersection/difference for disjoint/full-containment meshes, conservative fallback on partial overlaps | — |

**Discrete Exterior Calculus (DEC):**
- Exterior derivatives d0, d1 in CSR sparse matrix format.
- Hodge star operators: diagonal mass matrices for 0-forms, 1-forms, 2-forms.
- Cotan Laplacian: L = d0^T * star1 * d0 (positive semidefinite).
- Jacobi-preconditioned Conjugate Gradient solver (`SolveCG`, `SolveCGShifted`).

All operators follow a consistent contract: `Params` struct with defaults, `Result` struct with diagnostics, `std::optional<Result>` return for degenerate input.

QEM simplification now exposes a configurable `SimplificationParams::Quadric` block:
- **Primitive energy:** `Plane`, `Triangle`, or `Point` quadrics.
- **Residence domain:** accumulate error on `Vertices`, `Faces`, or `VerticesAndFaces`.
- **Probabilistic mode:** deterministic, isotropic uncertainty, or covariance-driven plane/triangle quadrics.
- **Collapse placement:** keep the survivor, use the quadric minimizer when the system is well-conditioned, or evaluate the best candidate among both endpoints plus the minimizer.

The default remains the previous Garland-Heckbert style behavior: **vertex-resident plane quadrics** averaged over incident faces and evaluated at the surviving endpoint, so existing callers preserve their old simplification profile unless they opt into richer quadric models.
Optional quality guards include **normal-cone deviation tracking** (`MaxNormalDeviationDegrees`), **Hausdorff error bounds** (`HausdorffError`) via per-face point lists, **aspect ratio**, **edge length**, and **max valence** limits.
Open-boundary robustness defaults to treating the live boundary as immutable when `PreserveBoundary = true` (no collapse may touch a boundary vertex), additionally rejecting **boundary → interior** collapses (`ForbidBoundaryInteriorCollapse = true`) and requiring at least two incident faces on the removed vertex (`MinRemovedVertexIncidentFaces = 2`).
The exported `Geometry::Quadric` API now includes `ProbabilisticPlaneQuadric(...)` and `ProbabilisticTriangleQuadric(...)` in both isotropic and full-covariance forms for uncertainty-aware fitting experiments.

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
  - Execution packet merging: consecutive passes in a linear DAG chain are merged into single secondary command buffer recordings. Both compute/copy and raster passes are eligible — raster passes merge when they target the exact same color+depth attachments *and* matching attachment semantics (load/store ops plus clear values when relevant), sharing a single `vkCmdBeginRendering` scope with load ops from the first pass and store ops from the last.
- **Async Transfer System:**
  - Staging Belt: Persistent ring-buffer allocator for CPU-to-GPU streaming.
  - Timeline Semaphore synchronization for async asset uploads.
  - No loader thread ever calls `vkWaitForFences` for texture uploads.
- **GPUScene:** Retained-mode instance table with independent slot allocation/deallocation.
- **Dynamic Rendering:** No `VkRenderPass` or `VkFramebuffer`; fully dynamic attachment binding.
- **Three-Pass Architecture:** Unified `SurfacePass` (filled triangles), `LinePass` (thick anti-aliased edges), and `PointPass` (billboard/surfel points) — each handling both retained BDA and transient debug data internally. Per-pass ECS components (`ECS::Surface::Component`, `ECS::Line::Component`, `ECS::Point::Component`) gate rendering via presence/absence. `DefaultPipeline` currently schedules: Picking, Surface, Composition, Line, Point, `HtexPatchPreview`, PostProcess, SelectionOutline, DebugView, ImGui, then a final `Present` blit when an intermediate LDR target is active. That order preserves a dedicated forward-overlay lane for primitives that should stay out of the deferred G-buffer.
- **Mesh Overlays:** `Graphics::OverlayEntityFactory::CreateMeshOverlay()` now eagerly uploads the child mesh into `GeometryPool` and writes `ECS::Surface::Component::Geometry` immediately, so mesh overlays are renderable/pickable on the frame they are created. Degenerate or non-triangular meshes remain valid ECS overlays but are intentionally left with an invalid surface handle and skipped by `MeshRendererLifecycle`.
- **Deferred Lighting Path:** `CompositionPass` implements fullscreen deferred lighting composition. When `FrameLightingPath::Deferred` is active (toggled via `FeatureRegistry` → "DeferredLighting"), `SurfacePass` is the deferred-capable lane: opaque surface geometry writes a 3-channel G-buffer MRT (SceneNormal `R16G16B16A16_SFLOAT`, Albedo `R8G8B8A8_UNORM`, Material0 `R16G16B16A16_SFLOAT`) instead of `SceneColorHDR`. `CompositionPass` then reads the G-buffer + depth, reconstructs world positions via push-constant `InvViewProj`, and applies Blinn-Phong lighting into `SceneColorHDR`. `LinePass` and `PointPass` remain forward-overlay passes and execute after composition, which keeps wireframe, graph, point-cloud, debug, and future transparent/special shading paths composited on top of the lit HDR scene. The forward path remains the default. Selection outlines, debug views, and post-processing are independent of the lighting path. The `FrameRecipe` system allocates G-buffer resources only when deferred is active. The typed `FrameLightingPath::Hybrid` contract now also reserves the same deferred-backed resources/composition scheduling, so future hybrid composition work can land without weakening recipe validation or pass contracts. The architecture is designed for future `HybridComposition` / `ForwardPlusComposition` implementations without changing pass registration or the render graph.
- **Canonical Render Resources + Frame Recipe:** Render setup is now recipe-driven rather than hard-coded in `RenderDriver`. `FrameRecipe` declares which canonical targets are needed for a frame (`SceneDepth`, `EntityId`, `PrimitiveId`, `SceneNormal`, `Albedo`, `Material0`, `SceneColorHDR`, `SceneColorLDR`). `SelectionMask` and `SelectionOutline` remain reserved canonical resource definitions for a future split-outline path, but they are not required by the current selection recipe. A single `FrameSetup` pass imports the swapchain backbuffer and depth image, creates only the requested transient intermediates, and seeds the blackboard with stable resource IDs.
- **HDR Post-Processing / Presentation:** Scene geometry writes to canonical `SceneColorHDR`. `PostProcessPass` tone maps into canonical `SceneColorLDR` (with an internal temporary when AA is enabled), with optional color grading (lift/gamma/gain, saturation, contrast, white balance) applied in linear space after tone mapping. An optional **luminance histogram** compute pass bins log-luminance from `SceneColorHDR` into a 256-bin SSBO (GPU→CPU readback), displaying the distribution and average luminance in the View Settings panel for exposure debugging. Overlay passes (`SelectionOutline`, viewport `DebugView`, `ImGui`) compose onto the current presentation target, which is `SceneColorLDR` when post/overlay stages are active and otherwise the imported backbuffer. The render debug UI now exposes a **Viewport debug source** dropdown that lists every currently sampleable compiled render target/texture (for example `EntityId`, `PrimitiveId`, depth, HDR/LDR scene color, and post-process intermediates when present), letting the main viewport show those buffers directly for visual debugging. Non-sampled attachments remain visible in the diagnostics tables but are marked diagnostics-only. A final `Present` stage copies `SceneColorLDR` into the swapchain image explicitly, eliminating implicit backbuffer ownership assumptions from earlier passes.
- **Line/Graph Rendering:** `LinePass` renders via BDA shared vertex buffers with persistent per-entity edge index buffers. Wireframe edges share mesh vertex buffers (same device address) with edge topology view. `line.vert` expands segments to screen-space quads (6 verts/segment) with anti-aliasing. Graph entities (`ECS::Graph::Data`) hold `shared_ptr<Geometry::Graph>` with PropertySet-backed data authority, CPU-side layout algorithms (force-directed, spectral, hierarchical), and persistent GPU buffers managed by `GraphLifecycleSystem`. All retained passes do CPU-side frustum culling. Vector-field overlays are attached as child graph overlays that inherit the parent transform: the sync system publishes `e:vf_center` and `f:vf_center` vec3 properties for edge/face domains, each entry selects a base-point property plus a vec3 vector source on that same domain, and the child graph is rendered as ordinary lines through `LinePass`.
- **Point Cloud Rendering:** `PointPass` renders via BDA shared vertex buffers. `point_flatdisc.vert` / `point_surfel.vert` expand points to billboard quads (6 verts/point). Four render modes: FlatDisc (camera-facing billboard), Surfel (normal-oriented disc with Lambertian shading), EWA splatting (Zwicker et al. 2001 — perspective-correct elliptical Gaussian splats via per-surfel Jacobian projection with 1px² low-pass anti-aliasing), and Sphere impostors (depth-writing sphere billboards for correct point/mesh occlusion). Surfel/EWA require real per-point normals; if normals are absent, runtime falls back to FlatDisc. KMeans centroid clouds follow the same `ECS::PointCloud::Data` + transform/hierarchy contract as imported point clouds, so they are rendered by the same pass without a special-case pipeline. The `Geometry.PointCloud` CPU module (data structures, downsampling, statistics) remains functional. Registered drag-and-drop/import point-cloud formats now include `.xyz`, `.pts`, `.xyzrgb`, `.txt`, and `.pcd` (ASCII + binary PCD). The XYZ importer also tolerates common scan-export rows with `LH<n>` scan markers and semicolon-delimited `x; y; z;` samples.
- **Capability-Based Geometry Processing Inspector:** The Inspector now contains a unified **Geometry Processing** section driven by per-entity domain discovery. Each algorithm is exposed only when the selected entity has a compatible authoritative source, and the same reusable algorithm widgets are shared between the Inspector and dedicated workflow panels. The domain contract is now explicit: meshes expose `Mesh Vertices`, `Mesh Edges`, `Mesh Halfedges`, and `Mesh Faces`; graphs expose `Graph Nodes`, `Graph Edges`, and `Graph Halfedges`; point clouds expose `Point Cloud Points` only. Surface-topology operators (remeshing, simplification, smoothing, subdivision, repair) additionally require collider-backed editable surface authority, while point-set operators such as k-means resolve against the compatible point domains (`Mesh Vertices`, `Graph Nodes`, `Point Cloud Points`). When a single entity carries multiple authoritative point domains, the UI enumerates each source explicitly (with per-domain point counts) and keeps the chosen source domain stable as you switch between mesh/graph/point-cloud inputs.
- **Inspector Property Browsers + Spectral Panels:** Mesh, graph, and point-cloud inspector sections now expose reusable `PropertySet` browsers for mesh `vertex/edge/halfedge/face`, graph `vertex/edge/halfedge`, and point-cloud `point` domains, so arbitrary authored attributes can be inspected directly rather than only selected as color sources. Imported triangle meshes spawned through `SceneManager::SpawnModel()` now inherit `ECS::Mesh::Data` directly from the collision-side authoritative `SourceMesh`, so default assets such as the duck model retain their full halfedge topology domains in the editor instead of degrading to a render-only surface path. The editor also adds dedicated **Geometry - Mesh Spectral** and **Geometry - Graph Spectral** panels plus inline inspector widgets: mesh spectral analysis publishes low-frequency cotan-Laplacian scalar modes back into mesh vertex properties through the standard `VertexAttributes` dirty-tag path, while graph spectral layout can either publish embedding coordinates/radii as graph vertex properties or rewrite authoritative graph node positions through `VertexPositions` for immediate retained-mode re-upload.
- **Mesh Analysis Markers:** The new `Geometry::MeshAnalysis` module writes selectable defect markers into `v:analysis_problem`, `e:analysis_problem`, `h:analysis_problem`, and `f:analysis_problem` plus matching `*_analysis_issue_mask` bitmasks. The editor can expose these properties through the existing property-browser workflow so boundary, isolated, non-manifold, degenerate, skinny, and non-finite regions are visually inspectable on the live halfedge mesh.
- **Inspector Mesh Analysis Action:** The Inspector now includes a `Run Mesh Analysis` action on mesh entities. It writes scalar visualization mirrors (`v:analysis_problem_f`, `e:analysis_problem_f`, `h:analysis_problem_f`, `f:analysis_problem_f`) and automatically switches the mesh color sources to those fields so the current defect set is visible immediately in the viewport.
- **Mesh Quality vs. Mesh Analysis:** `Geometry::MeshQuality` remains a separate aggregate summary module (min/max/mean angles, aspect ratio, edge length, valence, area, volume, etc.). It does not publish per-element marker properties, so it is not a substitute for the defect-marker view. Use `MeshQuality` for statistics, `MeshAnalysis` for selecting and visually inspecting broken regions.
- **Point-Set Clustering:** Inspector-side k-means results are materialized as PropertySet data (`v:kmeans_label`, `v:kmeans_distance`, `v:kmeans_color` on mesh/graph vertices; `p:kmeans_label`, `p:kmeans_distance`, `p:kmeans_color` on point clouds) rather than ad hoc side tables, so color visualization, serialization-adjacent tooling, and GPU attribute refresh continue to use the engine’s standard PropertySet + `ECS::DirtyTag::VertexAttributes` pipeline. Mesh vertex clustering also drives the Htex patch preview atlas, which is enabled by default for mesh inspection and now waits for the background bake gate before the preview texture is rendered; the pass caches patch metadata/signature inputs and only schedules a rebake when the mesh or k-means revision changes. CUDA scheduling is now backend-independent at the UI level: any authoritative point set can request CUDA, with point clouds retaining a persistent cache and mesh/graph inputs using transient device uploads.
- **DebugDraw:** Immediate-mode transient overlay for dynamic debug visualization (contact manifolds, ad hoc instrumentation, and short-lived editor overlays) rendered by `LinePass` and `PointPass` transient paths. Depth-tested and overlay sub-passes. Comprehensive test coverage.
- **Graph Processing (CPU):** Halfedge-based graph topology with Octree-accelerated kNN construction, force-directed 2D layout, spectral embedding (combinatorial/symmetric-normalized Laplacian), hierarchical layered layout with crossing diagnostics and diameter-aware auto-rooting. Graph now also exposes the shared vertex-ring and boundary circulators (`HalfedgesAroundVertex`, `BoundaryHalfedges`, `BoundaryVertices`) so traversal-heavy algorithms can reuse the same ring semantics as `Halfedge::Mesh`.
- **Transform Gizmos:** `Graphics::TransformGizmo` is now an `ImGuizmo`-backed editor wrapper instead of a custom `DebugDraw` manipulator. The engine caches selection/camera state during `OnUpdate()`, then executes the gizmo during the active ImGui frame through a lightweight overlay callback so transform interaction stays in the same input/render path as the rest of the editor UI. World/local space, snap increments, and pivot strategies (centroid / first-selected) remain configurable from the viewport toolbar. For parented entities, manipulation happens in world space and is converted back to the child’s parent-local `Transform::Component` via `Transform::TryComputeLocalTransform()`, so local-space editing follows the composed world orientation rather than the raw stored local TRS. Multi-selection uses a shared pivot and applies the resulting world-space delta matrix to each cached selected entity.
- **ImGui panel startup policy:** Closable editor panels no longer force themselves open every startup. `Interface::GUI::RegisterPanel(name, callback, isClosable, flags, defaultOpen)` treats `defaultOpen` as a first-registration policy, preserves the user’s open/closed choice on later re-registration, and `Interface::GUI::OpenPanel(name)` is now the explicit menu/tool path for reopening a hidden panel. `imgui.ini` still controls layout and `Collapsed=1/0`, but not true closed-vs-open visibility.
- **Unified Picker:** `Runtime.Selection` now maintains a first-class `Picked` state alongside entity selection. Each click resolves the selected entity plus sub-element metadata (`vertex_idx`, `edge_idx`, `face_idx`, `pick_radius`, hit-space positions/normals/barycentrics) and keeps it synchronized when selection changes externally from the hierarchy or other editor tools. GPU picking uses a dual-channel MRT pipeline producing both `EntityId` and `PrimitiveId` (R32_UINT each) in one frame via three dedicated pick pipelines: `pick_mesh`, `pick_line`, and `pick_point`. `PrimitiveId` is a self-describing hint: the high 2 bits encode the primitive domain (surface triangle / line segment / point). For surfaces, the low 30 bits carry the authoritative mesh face ID when a triangle→face sidecar exists for the rendered mesh; otherwise they fall back to the raster triangle index. For lines and points, the low 30 bits store the zero-based segment or point index. The point-pick path now reserves the same 120-byte point-style push-constant budget as `PointPass`, keeping point-radius selection aligned with the render path. After the entity hit is known, the picker completes the full primitive tuple from the projected hit point on the picked object: meshes resolve point-on-face → closest edge → closest vertex, graphs resolve the nearest edge/node pair around the hit, and point clouds keep direct point `PrimitiveId` selection. On the CPU backend, entities can optionally attach `ECS::PrimitiveBVH::Data`; mesh and graph picking use the attached primitive-AABB BVH as a broadphase and then preserve the existing exact triangle/segment refinement path. Mesh picking still has a collider-free fallback: if an entity has authoritative `ECS::Mesh::Data` but no `ECS::MeshCollider::Component`, selection raycasts the retained halfedge mesh directly and resolves sub-elements from the object-space hit point. Invalid GPU primitive IDs use `UINT_MAX`, so primitive index `0` remains selectable.
- **Sub-Element Selection:** `ElementMode` (Entity/Vertex/Edge/Face) radio buttons in the Selection panel enable sub-entity picking. In Vertex mode, selected vertices are highlighted with red overlay spheres (green in Geodesic mode); edges with yellow overlay lines; faces with blue tinted triangles and outlines. Shift-click adds/toggles sub-elements (multi-select). `SubElementSelection` tracks per-entity sets of selected vertex, edge, and face indices. Highlights are rendered via `DebugDraw` overlay (always-on-top, no depth test) and are independent of the lighting path.
- **Geodesic Distance UI:** When Vertex selection mode is active, the Geodesic Distance tool computes heat-method geodesic distances from selected source vertices. Results are stored as the `v:geodesic_distance` mesh property and can be visualized via the Vertex Color Source selector with any colormap.
- **Selection Outlines:** Post-process contour highlight for selected/hovered entities using canonical `EntityId` input. Three outline modes: Solid (constant color), Pulse (animated alpha oscillation), and Glow (distance-based falloff). Configurable fill overlay tints selected and hovered entities with semi-transparent color. Editor hierarchy selection is subtree-aware: selecting a non-renderable parent still resolves the `PickID`s of renderable descendants, so imported model roots outline correctly. All parameters (colors, width, mode, fill alpha, pulse speed, glow falloff) adjustable at runtime via View Settings panel.
