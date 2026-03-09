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
- **FrameGraph + DAG Scheduler:** ECS systems declare explicit data dependencies; `DAGScheduler` resolves topological execution order. Shared scheduling algorithm between FrameGraph and RenderGraph.
- **AssetManager Read Phases:**
  - `AssetManager::Update()` is the single-writer phase on the main thread.
  - Parallel systems use `BeginReadPhase()` / `EndReadPhase()` brackets.
  - `AcquireLease()` for long-lived access across hot-reloads.

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
- **Point Cloud Rendering:** `PointPass` renders via BDA shared vertex buffers. `point_flatdisc.vert` / `point_surfel.vert` expand points to billboard quads (6 verts/point). Three render modes: FlatDisc (camera-facing billboard), Surfel (normal-oriented disc with Lambertian shading), and EWA splatting (Zwicker et al. 2001 — perspective-correct elliptical Gaussian splats via per-surfel Jacobian projection with 1px² low-pass anti-aliasing). Surfel/EWA require real per-point normals; if normals are absent, runtime falls back to FlatDisc. The `Geometry.PointCloud` CPU module (data structures, downsampling, statistics) remains functional.
- **DebugDraw:** Immediate-mode transient overlay for dynamic debug visualization (contact manifolds, transform gizmos, ad hoc instrumentation) rendered by `LinePass` and `PointPass` transient paths. Depth-tested and overlay sub-passes. Comprehensive test coverage. The Sandbox transform gizmo now stays on this transient path all the way to `LinePass`; it no longer rebuilds retained `ECS::Line::Component` geometry every frame, which avoids per-frame buffer churn and deferred-destruction stalls when switching gizmo modes.
- **Graph Processing (CPU):** Halfedge-based graph topology with Octree-accelerated kNN construction, force-directed 2D layout, spectral embedding (combinatorial/symmetric-normalized Laplacian), hierarchical layered layout with crossing diagnostics and diameter-aware auto-rooting.
- **Transform Gizmos:** `Graphics::TransformGizmo` renders translate/rotate/scale handles via `DebugDraw` overlay lines. Three-state interaction machine (idle/hovered/active) with deterministic axis picking, world/local space, configurable snap increments, and multi-entity pivot strategies (centroid, first-selected). Once a handle is engaged, dragging follows an ImGuizmo-like screen-space mapping evaluated against a frozen drag-start anchor: axis and plane motion are derived from the projected handle basis in NDC, rotation is driven from the cursor angle around the projected pivot, and scale follows the projected axis or vertical screen drag for uniform scaling. The live gizmo pose can update for display, but the target transform is always solved from the drag-start pivot, rotation, cached transforms, and handle scale, preventing feedback that would otherwise push the entity out of frame. World-space projection remains as a fallback for degenerate camera/handle alignments. Axis translation uses a camera-aligned drag plane for stable fallback motion, rotation deltas are wrapped across $[-\pi, \pi]$, and scale avoids tiny-denominator instability. Viewport toolbar panel for runtime configuration. Keyboard shortcuts: W/E/R for mode, X for space toggle. In the Sandbox app, gizmo geometry is emitted directly into the frame-local `DebugDraw` accumulator instead of the retained overlay cache so mode changes remain allocation-free on the retained line path.
- **Selection Outlines:** Post-process contour highlight for selected/hovered entities using canonical `EntityId` input. Three outline modes: Solid (constant color), Pulse (animated alpha oscillation), and Glow (distance-based falloff). Configurable fill overlay tints selected and hovered entities with semi-transparent color. Editor hierarchy selection is subtree-aware: selecting a non-renderable parent still resolves the `PickID`s of renderable descendants, so imported model roots outline correctly. All parameters (colors, width, mode, fill alpha, pulse speed, glow falloff) adjustable at runtime via View Settings panel.
- **Per-Element Attribute Rendering:** Per-edge colors via `PtrEdgeAux` BDA channel in `LinePass`, per-face colors via `PtrFaceAttr` BDA channel and `gl_PrimitiveID` in `SurfacePass`. Scalar-to-heat and label-to-categorical color utilities (`ScalarToHeatColor`, `LabelToColor`) for curvature visualization and segmentation coloring.
- **PropertySet Dirty Tracking:** Six zero-size `ECS::DirtyTag` tag components (`VertexPositions`, `VertexAttributes`, `EdgeTopology`, `EdgeAttributes`, `FaceTopology`, `FaceAttributes`) for incremental CPU→GPU sync. Attribute-only changes re-extract cached vectors without full vertex buffer re-upload.
- **Numerical Safeguards:** Epsilon-guarded normal renormalization with camera-facing fallback in all shaders, degenerate triangle filtering during edge extraction, line width clamping [0.5, 32.0] px, point radius clamping [0.0001, 1.0] world-space, zero-length graph edge filtering, EWA covariance conditioning (eigenvalue floor, isotropic fallback), and per-pass depth bias to prevent z-fighting.
- **Push Constant Validation:** `PipelineBuilder::Build()` validates each push constant range against device limits before pipeline creation, returning `std::unexpected` on violation.
- **Status Bar:** Persistent bottom strip with frame time/FPS, live entity count, and active renderer label.

### 4. Data I/O

Two-layer architecture: I/O backend (byte transport) separated from format loaders (parsing).

**Supported formats:**
| Category | Formats |
|---|---|
| **Mesh (Import)** | glTF 2.0 / GLB, OBJ, PLY (ASCII + binary), STL (ASCII + binary), OFF |
| **Mesh (Export)** | OBJ, PLY (binary + ASCII), STL (binary + ASCII) |
| **Point Cloud** | XYZ, PCD (ASCII), PLY |
| **Graph** | TGF |
| **Texture** | PNG, JPG, KTX (via stb/KTX loaders) |
| **Scene** | JSON (`.json`) — save/load via `Runtime::SceneSerializer` |

Drag-and-drop file loading with automatic format detection via `IORegistry`. Export pipeline with symmetric `IORegistry::Export()` API.

**Scene Serialization (JSON):** `Runtime::SaveScene()` / `Runtime::LoadScene()` serialize the full entity hierarchy — names, transforms, parent-child relationships, asset source paths, visibility flags, and per-component rendering parameters (point cloud modes, graph settings, wireframe/vertex display). GPU state is NOT serialized; it is reconstructed on load by re-importing assets from their recorded source paths. Missing asset files are gracefully skipped with structured log diagnostics. Dirty-state tracking (`SceneDirtyTracker`) warns before discarding unsaved changes.

---

## Architecture Backlog

For the current, living list of *remaining* architectural work, see `TODO.md`. Completed milestones are intentionally removed from that document to keep it focused (Git history is the source of truth for what used to be on the list).

C++23 style/convergence guidance for contributors is tracked in `CLAUDE.md` §C++23 Adoption Policy (monadic `std::expected` usage + explicit object parameter adoption rules).

---

## Build Requirements

- **Compiler:** Clang 20+ (C++23 modules)
- **Build system:** CMake 3.28+ with Ninja
- **GPU:** Vulkan 1.3 compatible driver
- **OS:** Linux (Ubuntu 22.04+)

### Quick Setup

```bash
# Clone and set up (installs dependencies, configures, builds libraries)
git clone <repo-url> && cd IntrinsicEngine
bash .claude/setup.sh

# Or manual:
sudo apt install build-essential cmake ninja-build git \
    vulkan-tools libvulkan-dev vulkan-validationlayers-dev spirv-tools glslc \
    libwayland-dev libxkbcommon-dev xorg-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev

# Install Clang 20
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 20
sudo apt install clang-tools-20 libstdc++-14-dev
```

### Build

```bash
# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20

# Fast local iteration profile (smaller build graph, no sanitizers)
cmake -B build-fast -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    -DINTRINSIC_BUILD_TESTS=OFF \
    -DINTRINSIC_BUILD_SANDBOX=OFF \
    -DINTRINSIC_ENABLE_SANITIZERS=OFF

# Offline configure (no network access during FetchContent)
# Requires pre-populated external/cache/<dep>-src directories.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    -DINTRINSIC_OFFLINE_DEPS=ON

# Optional: keep an existing mirror elsewhere and point each dependency at it
# with CMake cache overrides (example for GLM):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    -DINTRINSIC_OFFLINE_DEPS=ON \
    -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/mirror/glm

# Build everything
ninja -C build

# Build specific targets (faster incremental rebuilds)
ninja -C build IntrinsicGeometry      # geometry library only
ninja -C build IntrinsicGeometryTests # geometry tests
ninja -C build Sandbox                # application

# Fast-profile targeted build example
ninja -C build-fast IntrinsicRuntime

# Run
./build/bin/Sandbox

# Benchmark mode (run 300 frames after 30 warmup, output JSON)
./build/bin/Sandbox --benchmark 300 --warmup 30 --out benchmark.json

# Check for performance regressions
./tools/check_perf_regression.sh benchmark.json --avg-ms 16.67 --p99-ms 33.33 --min-fps 60
```

Common configure options for compile-time control:

- `-DINTRINSIC_BUILD_TESTS=OFF` skips `tests/` targets.
- `-DINTRINSIC_BUILD_SANDBOX=OFF` skips `Sandbox` app target.
- `-DINTRINSIC_ENABLE_SANITIZERS=OFF` removes ASan/UBSan instrumentation overhead in dev loops.

#### Offline dependency cache workflow

When using `-DINTRINSIC_OFFLINE_DEPS=ON`, configure never performs network fetches.
Prepare one source tree per dependency under `external/cache/<name>-src` (for example `external/cache/glm-src`, `external/cache/googletest-src`, `external/cache/stb-src`).

Practical options:
- Run one online configure once to populate `external/cache`, then archive/sync that folder for offline reuse.
- Mirror each dependency in your own artifact store and map specific dependencies with `-DFETCHCONTENT_SOURCE_DIR_<DEP>=/mirror/path`.

If any required source directory is missing or empty, CMake now fails fast with a clear diagnostic.

**Sanitizers:** ASan + UBSan are auto-detected in Debug builds. If `libclang_rt.asan` is not installed, sanitizers are automatically disabled.

---

## Test Suite

Four test targets with clear GPU/no-GPU boundaries:

| Target | Dependencies | Scope | Tests |
|---|---|---|---|
| `IntrinsicCoreTests` | Core only | Memory, tasks, handles, frame graph, DAG scheduler | ~80 |
| `IntrinsicGeometryTests` | Core + Geometry | DEC, mesh operations, collision, graphs, all geometry operators | ~240 |
| `IntrinsicECSTests` | Core + ECS | FrameGraph system integration | ~15 |
| `IntrinsicTests` | Full Runtime | Graphics, I/O, rendering, integration | ~270 |

### Architecture SLOs

Concrete service-level objectives for engine orchestration, verified by CI:

**FrameGraph (CPU DAG):**
- p99 `FrameGraphCompileTimeNs < 350,000 ns` (0.35 ms) at 2,000 nodes.
- p95 `FrameGraphExecuteTimeNs < 1,500,000 ns` (1.5 ms) at 2,000 nodes.
- p95 `FrameGraphCriticalPathTimeNs < 900,000 ns` (0.9 ms) at 2,000 nodes.

**Task scheduler contention and tail behavior:**
- `0.20 <= TaskStealSuccessRatio <= 0.65` under saturated synthetic load.
- p95 `TaskQueueContentionCount < 4,096` lock misses / frame on 16-worker stress profile.
- p95 `TaskIdleWaitTotalNs < 700,000 ns` (0.7 ms) during active gameplay frames.
- p99 `TaskUnparkP99Ns < 80,000 ns` (80 us).

**Telemetry:** Per-frame export for contention, steal ratio, idle wait, compile/execute/critical-path times. Performance panel surfaces rolling p95/p99 PASS/ALERT status. CI runs `ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes` and `ArchitectureSLO.TaskSchedulerContentionAndWakeLatencyBudgets`.

```bash
# Run all geometry tests
./build/bin/IntrinsicGeometryTests

# Run specific test groups
./build/bin/IntrinsicGeometryTests --gtest_filter="CatmullClark*"
./build/bin/IntrinsicGeometryTests --gtest_filter="NormalEstimation*"
./build/bin/IntrinsicGeometryTests --gtest_filter="MeshRepair*"
./build/bin/IntrinsicGeometryTests --gtest_filter="MarchingCubes*"
./build/bin/IntrinsicGeometryTests --gtest_filter="SurfaceReconstruction*"
./build/bin/IntrinsicGeometryTests --gtest_filter="DEC_*"
./build/bin/IntrinsicGeometryTests --gtest_filter="ConvexHull*"
```

---

## Controls

- **Left Click + Drag:** Orbit camera / interact with transform gizmo.
- **Right Click + WASD:** Fly camera mode.
- **Drag & Drop:** Drop `.glb`, `.gltf`, `.ply`, `.obj`, `.xyz`, `.pcd`, or `.tgf` files to load asynchronously.
- **W / E / R:** Switch gizmo mode (Translate / Rotate / Scale).
- **X:** Toggle gizmo space (World / Local).
- **F:** Focus camera on selected entity.
- **Q:** Reset camera to defaults.
- **ImGui Panels:**
  - **Hierarchy:** View and select entities.
  - **Inspector:** Modify transforms, view mesh stats.
  - **Viewport Toolbar:** Transform gizmo mode, space, pivot, and snap configuration.
  - **Assets:** Monitor async loading queues and asset states.
  - **Performance:** Real-time telemetry graphs, per-pass GPU/CPU timings table, and SLO alert indicators.
  - **View Settings:** Post-processing controls (tone mapping, bloom, anti-aliasing [None/FXAA/SMAA], color grading, luminance histogram), selection outline styling, and spatial debug overlays.
  - **Render Target Viewer:** Debug visualization of render graph resources plus live renderer internals — frame/swapchain state, frame recipe flags, pipeline feature enablement, selection outline internals, post-process internals, per-pass attachments with format/load/store tooltips, resource lifetime tables with per-resource memory estimates, visual lifetime timeline (color-coded bars showing alive/write ranges per pass), and click-to-select for viewport preview.

---

## Debug Visualization

Intrinsic includes an immediate-mode debug visualization layer (`Graphics::DebugDraw`) rendered by `LinePass` and `PointPass` transient paths.

### Spatial Debug Overlays

The Sandbox app can visualize selected `MeshCollider` acceleration/bounds data. Static-ish spatial overlays are now cached as retained `ECS::Line::Component` geometry so they behave like wireframe instead of repacking transient `DebugDraw` data every frame.

- **Octree overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider Octree`
- **Octree source:** selected entity’s `ECS::MeshCollider::Component::CollisionRef->LocalOctree`
- **Octree knobs:** max depth, leaf-only, occupied-only, overlay vs depth-tested, depth-based color ramp, alpha
- **Retained overlay path:** Sandbox octree visualization now builds a cached retained `ECS::Line::Component` overlay instead of re-emitting all octree boxes through transient `DebugDraw` every frame. The line geometry is rebuilt only when the selected collider, octree settings, or selected transform change, which keeps the steady-state frame cost much closer to mesh wireframe than to transient debug overlays.

- **Bounds overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider Bounds`
- **Bounds variants:** world AABB, world OBB, conservative bounding sphere (independent toggles)
- **Bounds knobs:** overlay vs depth-tested, alpha, per-primitive colors
- **Retained overlay path:** bounds visualization is captured once into retained line geometry and rebuilt only when the selected collider, bounds settings, or selected transform/local bounds change.

**KD-tree overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider KD-Tree`
- **KD-tree source:** built lazily from selected entity's `ECS::MeshCollider::Component::CollisionRef->Positions`, then cached per selected collider
- **KD-tree knobs:** leaf/internal visibility, split-plane overlay, max depth, overlay vs. depth-tested, alpha, per-category colors
- **Retained overlay path:** KD-tree lines are emitted through the existing debug-draw helper into an off-screen capture and uploaded as retained line geometry only when the selected collider, KD-tree settings, or selected transform change.

- **BVH overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider BVH`
- **BVH source:** transient build from selected collider triangles for visualization only
- **BVH knobs:** leaf/internal visibility, leaf triangle budget, max depth, overlay vs depth-tested, alpha, per-category colors
- **Retained overlay path:** the BVH visualization capture is rebuilt only when the selected collider source, BVH settings, selected transform, or mesh index/vertex counts change.

- **Convex hull overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider Convex Hull`
- **Convex hull source:** cached hull mesh derived from the selected collider positions
- **Convex hull knobs:** overlay vs depth-tested, alpha, line color
- **Retained overlay path:** convex hull edges are cached as retained line geometry and rebuilt only when the selected collider, hull settings, or selected transform change.

- **Contact manifold overlay UI:** `View Settings` → `Spatial Debug` → `Draw Contact Manifolds`
- **Transient path note:** contact manifolds still use immediate-mode `DebugDraw` because they are derived from pairwise collider state each frame and include short-lived point/normal instrumentation.

**Performance note:** drawing deep octrees/KD-trees can generate thousands of line segments. Use `Max Depth` and `Leaf Only` to cap cost. `Graphics::DebugDraw` now enforces a per-frame line budget (32,768 segments by default), and the octree/KD-tree emitters stop traversal once that budget is exhausted, so overlay cost is bounded by $O(\min(N, B))$ emitted segments instead of unbounded $O(N)$ growth for pathological trees.

---

### KD-Tree Spatial Queries

`Geometry::KDTree` provides an Octree-inspired, axis-aligned BVH-style accelerator over element AABBs for analysis operators and debug overlays (via `Graphics::DrawKDTree`).

- **Build:** median split on max-extent axis, leaf-size and max-depth bounded, with `MinSplitExtent` guarding degenerate splits.
- **Input modes:**
  - `Build(span<const AABB>)` / `Build(vector<AABB>&&)` for general volumetric elements (e.g., triangles via their bounds).
  - `BuildFromPoints(span<const glm::vec3>)` convenience path for point clouds.
- **Queries:**
  - `Query(shape, out)` (`AABB`, `Sphere`, `Ray`, or any `SpatialQueryShape`) for overlap filtering.
  - `QueryKnn(p, k, outIndices)` — exact top-`k` nearest elements under AABB distance using branch-and-bound.
  - `QueryRadius(p, r, outIndices)` — exact elements with AABB distance within radius `r`.
- **Robustness:** build/query reject degenerate parameters (`LeafSize == 0`, `k == 0`, negative/NaN radius) and remain stable for coincident elements.

Complexity (typical, well-distributed points):
- Build: $O(n \log n)$ time, $O(n)$ extra index storage.
- kNN query: $O(\log n + k)$ expected, $O(n)$ worst-case.
- Radius query: $O(\log n + m)$ expected where $m$ is returned neighbors, $O(n)$ worst-case.

## Module Structure

```
src/
  Core/           Foundation: memory, tasks, assets, telemetry, frame graph, I/O
  Runtime/
    Geometry/     Math kernel: collision, halfedge mesh, DEC, all operators
    RHI/          Vulkan 1.3 abstraction: device, swapchain, pipeline, transfer
    Graphics/     Engine layer: render graph, model loading, render system, debug draw
    ECS/          Entity-component system: scene, components, systems (EnTT-based)
  Apps/
    Sandbox/      Application: engine configuration and main loop
tests/            GTest suite: 34 test files across 4 targets
assets/           Sample geometry and texture files
```

---

## Forward Rendering Pipeline

Three conceptual stages:

1. **Stage 1 (Instance Resolve):** Collects renderables, resolves materials/texture IDs.
2. **Stage 2 (CPU Indirect Build):** Builds batched `VkDrawIndexedIndirectCommand` streams on the CPU.
3. **Stage 3 (GPU Culling / Indirect Build):** Uses persistent `GPUScene` SSBOs, frustum-culls on the GPU, produces compacted indirect stream.

Only one path renders per frame (Stage 2 CPU-driven OR Stage 3 GPU-driven). No double-draw, no double-clear.

**GPUScene lifecycle:** Retained-mode instance table. Slots allocated/freed independently of ECS iteration order. Loading new models never causes previously loaded ones to vanish.

---

## Troubleshooting

**`CMake Error: Could not find clang-scan-deps`** — Install `clang-tools-20` and pass `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20`.

**`fatal error: 'format' file not found`** — Install `libstdc++-14-dev`.

**Asset Loading Fails** — Check logs (`[ERR]`). Ensure `assets/` is adjacent to the binary or in the project root.

---

## Architecture Documentation

- **`TODO.md`** — Active architecture backlog (open TODOs only, with priorities and remediation notes).
- **`ROADMAP.md`** — Feature roadmap with dependency-ordered phases and the long-term rendering modality redesign vision.
- **`docs/architecture/rendering-three-pass.md`** — Canonical three-pass rendering architecture spec (pass contracts, data contracts, invariants).
- **`PLAN.md`** — Archival index for the completed rendering refactor.
- **`CLAUDE.md`** — Development conventions, build workflows, architectural invariants, C++23 adoption policy.
- **Git history** — Historical architecture notes and completion summaries for closed backlog items.

Backlog hygiene is CI-enforced: pull requests fail if `TODO.md` contains completed/historical markers instead of active unfinished items.

---

## Editor UI (ImGui)

The sandbox app ships with a lightweight editor UI built on Dear ImGui.
Panels are registered via `Interface::GUI::RegisterPanel()`.

### Menus

- `File` → `Save Scene` / `Save Scene As...` / `Load Scene` — JSON scene serialization with dirty-state warning.

### Panels

- `Features` — browse and toggle `Core::FeatureRegistry` categories (RenderFeatures, Systems, Panels, GeometryOperators).
- `Frame Graph` — inspect the current `Core::FrameGraph` execution layers (pass names grouped by parallel layer).
- `Selection` — configure selection backend (CPU/GPU), mouse button, and clear selection.

### Extending

Common editor panels live in `src/Runtime/EditorUI/` and are registered from the app via:

- `Runtime::EditorUI::RegisterDefaultPanels(engine)`

Add a new panel by calling `Interface::GUI::RegisterPanel("My Panel", []{ ... });` from `Runtime.EditorUI`.

## Shared-Buffer Multi-Topology Rendering (BDA Vertex Pulling)

One device-local vertex buffer on the GPU, multiple index buffers with different topologies referencing into it. This is the core rendering architecture for all geometry types — not just meshes.

**Data sharing works via buffer device addresses (BDA), not `vkCmdBindVertexBuffers`.** The engine uses programmable vertex pulling throughout — `SurfacePass` reads positions/normals via `GL_EXT_buffer_reference` pointers passed in push constants. Each topology view gets its own `VkPipeline` with its own vertex shader that reads from the shared buffer via the same BDA pointer:

| View | Pipeline | Vertex shader reads via BDA | Index buffer |
|------|----------|-----------------------------|-------------|
| Surface mesh | `SurfacePass` | `positions[gl_VertexIndex]` | Triangle indices |
| Wireframe | `LinePass` | `positions[edgeIdx]` → expand to quad (6v/seg) | Unique edge pairs |
| Vertex visualization | `PointPass` | `positions[pointID]` → expand to billboard (6v/pt) | Identity / direct draw |
| kNN graph | `LinePass` | Same line shader | Neighbor edge pairs |
| Standalone point cloud | `PointPass` | Own buffer via BDA | Direct draw |

Zero vertex duplication for mesh-derived views — same `std::shared_ptr<VulkanBuffer>`, same device address. Each topology requires a separate shader pipeline because thick lines and billboard points need vertex-shader expansion (6 verts/primitive); `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`/`POINT_LIST` alone produce only 1px primitives.

Each view owns its own `GeometryHandle`, `GPUScene` slot, and participates in frustum culling independently. The `GeometryPool`/`GPUScene` retained-mode system is topology-agnostic. Only `DebugDraw` content (octree, bounds, contact manifold overlays) uses per-frame transient SSBO uploads — everything else is retained.

Retained-mode BDA rendering is the sole path for all geometry types: wireframe edges (`LinePass`), vertex visualization and point clouds (`PointPass` with FlatDisc/Surfel/EWA modes), and graph geometry. Standalone point clouds (`.xyz`/`.pcd`/`.ply`) and preloaded point-topology meshes are first-class retained-mode renderables via `ECS::PointCloud::Data` + `PointCloudGeometrySyncSystem` (cloud-backed or preloaded-geometry path), rendered via BDA with zero per-frame vertex upload cost. Transient `DebugDraw` lines and points are rendered by `LinePass` and `PointPass` internal transient paths. `MeshViewLifecycleSystem` automates GPU geometry view creation for mesh-derived edge and vertex views via `MeshEdgeView::Component` / `MeshVertexView::Component`, with GPUScene slot management and EnTT destroy hooks.

### Key API

- `Graphics::GeometryUploadRequest`:
  - `Geometry::GeometryHandle ReuseVertexBuffersFrom` (optional)
    - If valid, the upload request **reuses the vertex buffer (same `std::shared_ptr<VulkanBuffer>`, same device address)** from the referenced geometry.
    - When set, `Positions/Normals/Aux` spans are ignored.
    - `Indices` are **always uploaded** and remain unique per view.
    - The view's vertex shader reads from the shared buffer via BDA — the same `uint64_t` device address as the source mesh.

- `Graphics::GeometryGpuData::CreateAsync(...)`:
  - Adds an optional `const Graphics::GeometryPool* existingPool` argument.
  - When `ReuseVertexBuffersFrom` is valid, `existingPool` must be provided so the source geometry can be looked up.

### Example: Create a wireframe view from an existing mesh

```cpp
// CPU side: create the view with shared vertex buffer
Graphics::GeometryUploadRequest req;
req.ReuseVertexBuffersFrom = sourceMeshHandle;
req.Indices = edgeIndexPairs;  // unique edges, extracted once
req.Topology = Graphics::PrimitiveTopology::Lines;

auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
    device, transferManager, req, &geometryPool);

auto wireframeHandle = geometryPool.Add(std::move(gpuData));

// GPU side: the line vertex shader reads positions via BDA
// (same device address as SurfacePass uses for the mesh):
//   PosBuf pBuf = PosBuf(push.ptrPos);  // GL_EXT_buffer_reference
//   vec3 p0 = pBuf.v[edgeIndices[lineID * 2 + 0]];
//   vec3 p1 = pBuf.v[edgeIndices[lineID * 2 + 1]];
//   // ... expand to screen-space quad (6 verts/segment)
```

### Lifetime semantics

Because the vertex/index buffers are stored as `std::shared_ptr<RHI::VulkanBuffer>` inside `GeometryGpuData`, GPU memory remains alive until **all** views using that buffer are destroyed. The BDA pointer remains valid as long as the underlying `VulkanBuffer` is alive.
