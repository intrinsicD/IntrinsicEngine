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
- **Lock-Free Telemetry:** Ring-buffered telemetry system for real-time CPU frame times, draw calls, and triangle counts.
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

### 3. Rendering (Vulkan 1.3)

- **Bindless Architecture:** Full `VK_EXT_descriptor_indexing` for bindless textures.
- **Render Graph:**
  - Automatic dependency tracking and barrier injection (VK_KHR_synchronization2).
  - Transient resource aliasing (memory reuse).
  - Lambda-based pass declaration: `AddPass<Data>(setup, execute)`.
- **Async Transfer System:**
  - Staging Belt: Persistent ring-buffer allocator for CPU-to-GPU streaming.
  - Timeline Semaphore synchronization for async asset uploads.
  - No loader thread ever calls `vkWaitForFences` for texture uploads.
- **GPUScene:** Retained-mode instance table with independent slot allocation/deallocation.
- **Dynamic Rendering:** No `VkRenderPass` or `VkFramebuffer`; fully dynamic attachment binding.
- **Line/Graph Rendering:** Retained-mode line rendering (`RetainedLineRenderPass`) via BDA shared vertex buffers with persistent per-entity edge index buffers. Wireframe edges share mesh vertex buffers (same device address) with edge topology view. `line_retained.vert` expands segments to screen-space quads (6 verts/segment) with anti-aliasing. Graph entities (`ECS::Graph::Data`) hold `shared_ptr<Geometry::Graph>` with PropertySet-backed data authority, CPU-side layout algorithms (force-directed, spectral, hierarchical), and persistent GPU buffers managed by `GraphGeometrySyncSystem`. Both retained passes do CPU-side frustum culling.
- **Point Cloud Rendering:** Retained-mode point rendering (`RetainedPointCloudRenderPass`) via BDA shared vertex buffers. `point_retained.vert` expands points to billboard quads (6 verts/point). Three render modes: FlatDisc (camera-facing billboard), Surfel (normal-oriented disc with Lambertian shading), and EWA splatting (Zwicker et al. 2001 — perspective-correct elliptical Gaussian splats via per-surfel Jacobian projection with 1px² low-pass anti-aliasing). Transient per-frame point submission also supported via `PointCloudRenderPass` SSBO path. The `Geometry.PointCloud` CPU module (data structures, downsampling, statistics) remains functional.
- **DebugDraw:** Immediate-mode transient overlay for debug visualization (octree, KD-tree, bounds, contact manifolds, convex hulls) via per-frame SSBO upload + `LineRenderPass`. Depth-tested and overlay sub-passes. Comprehensive test coverage.
- **Graph Processing (CPU):** Halfedge-based graph topology with Octree-accelerated kNN construction, force-directed 2D layout, spectral embedding (combinatorial/symmetric-normalized Laplacian), hierarchical layered layout with crossing diagnostics and diameter-aware auto-rooting.
- **Selection Outlines:** Post-process contour highlight for selected/hovered entities.

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

Drag-and-drop file loading with automatic format detection via `IORegistry`. Export pipeline with symmetric `IORegistry::Export()` API.

---

## Architecture Backlog

For the current, living list of *remaining* architectural work, see `TODO.md`. Completed milestones are intentionally removed from that document to keep it focused (Git history is the source of truth for what used to be on the list).

C++23 style/convergence guidance for contributors is tracked in `docs/CXX23_ADOPTION_POLICY.md` (monadic `std::expected` usage + explicit object parameter adoption rules).

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

# Run
./build/bin/Sandbox
```

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

- **Left Click + Drag:** Orbit camera.
- **Right Click + WASD:** Fly camera mode.
- **Drag & Drop:** Drop `.glb`, `.gltf`, `.ply`, `.obj`, `.xyz`, `.pcd`, or `.tgf` files to load asynchronously.
- **ImGui Panels:**
  - **Hierarchy:** View and select entities.
  - **Inspector:** Modify transforms, view mesh stats.
  - **Assets:** Monitor async loading queues and asset states.
  - **Performance:** Real-time telemetry graphs.

---

## Debug Visualization

Intrinsic includes an immediate-mode debug visualization layer (`Graphics::DebugDraw`) rendered by `LineRenderPass`.

### Spatial Debug Overlays

The Sandbox app can visualize selected `MeshCollider` acceleration/bounds data using `Graphics::DebugDraw` + `LineRenderPass`.

- **Octree overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider Octree`
- **Octree source:** selected entity’s `ECS::MeshCollider::Component::CollisionRef->LocalOctree`
- **Octree knobs:** max depth, leaf-only, occupied-only, overlay vs depth-tested, depth-based color ramp, alpha

- **Bounds overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider Bounds`
- **Bounds variants:** world AABB, world OBB, conservative bounding sphere (independent toggles)
- **Bounds knobs:** overlay vs depth-tested, alpha, per-primitive colors

**KD-tree overlay UI:** `View Settings` → `Spatial Debug` → `Draw Selected MeshCollider KD-Tree`
- **KD-tree source:** built lazily from selected entity's `ECS::MeshCollider::Component::CollisionRef->Positions`, then cached per selected collider
- **KD-tree knobs:** leaf/internal visibility, split-plane overlay, max depth, overlay vs depth-tested, alpha, per-category colors

**Performance note:** drawing deep octrees/KD-trees can generate thousands of line segments. Use `Max Depth` and `Leaf Only` to cap cost.

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
- **`ROADMAP.md`** — Feature roadmap, dependency-ordered phases, and long-horizon planning notes.
- **Git history** — Historical architecture notes and completion summaries for closed backlog items.
- **`CLAUDE.md`** — Development conventions, build workflows, architectural invariants.
- **`docs/RENDERING_MODALITY_REDESIGN_PLAN.md`** — Thorough architecture plan for first-class mesh/graph/point rendering approaches and mode toggling.

Backlog hygiene is CI-enforced: pull requests fail if `TODO.md` contains completed/historical markers instead of active unfinished items.

---

## Editor UI (ImGui)

The sandbox app ships with a lightweight editor UI built on Dear ImGui.
Panels are registered via `Interface::GUI::RegisterPanel()`.

### New panels

- `Features` — browse and toggle `Core::FeatureRegistry` categories (RenderFeatures, Systems, Panels, GeometryOperators).
- `Frame Graph` — inspect the current `Core::FrameGraph` execution layers (pass names grouped by parallel layer).
- `Selection` — configure selection backend (CPU/GPU), mouse button, and clear selection.

### Extending

Common editor panels live in `src/Runtime/EditorUI/` and are registered from the app via:

- `Runtime::EditorUI::RegisterDefaultPanels(engine)`

Add a new panel by calling `Interface::GUI::RegisterPanel("My Panel", []{ ... });` from `Runtime.EditorUI`.

## Shared-Buffer Multi-Topology Rendering (BDA Vertex Pulling)

One device-local vertex buffer on the GPU, multiple index buffers with different topologies referencing into it. This is the core rendering architecture for all geometry types — not just meshes.

**Data sharing works via buffer device addresses (BDA), not `vkCmdBindVertexBuffers`.** The engine uses programmable vertex pulling throughout — `ForwardPass` reads positions/normals via `GL_EXT_buffer_reference` pointers passed in push constants. Each topology view gets its own `VkPipeline` with its own vertex shader that reads from the shared buffer via the same BDA pointer:

| View | Pipeline | Vertex shader reads via BDA | Index buffer |
|------|----------|-----------------------------|-------------|
| Surface mesh | `ForwardPass` | `positions[gl_VertexIndex]` | Triangle indices |
| Wireframe | `LineRenderPass` (retained) | `positions[edgeIdx]` → expand to quad (6v/seg) | Unique edge pairs |
| Vertex visualization | `PointCloudRenderPass` | `positions[pointID]` → expand to billboard (6v/pt) | Identity / direct draw |
| kNN graph | `LineRenderPass` (retained) | Same line shader | Neighbor edge pairs |
| Standalone point cloud | `PointCloudRenderPass` | Own buffer via BDA | Direct draw |

Zero vertex duplication for mesh-derived views — same `std::shared_ptr<VulkanBuffer>`, same device address. Each topology requires a separate shader pipeline because thick lines and billboard points need vertex-shader expansion (6 verts/primitive); `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`/`POINT_LIST` alone produce only 1px primitives.

Each view owns its own `GeometryHandle`, `GPUScene` slot, and participates in frustum culling independently. The `GeometryPool`/`GPUScene` retained-mode system is topology-agnostic. Only `DebugDraw` content (octree, bounds, contact manifold overlays) uses per-frame transient SSBO uploads — everything else is retained.

**Current status:** Retained-mode BDA rendering is operational for wireframe (`RetainedLineRenderPass`), vertex visualization (`RetainedPointCloudRenderPass` with FlatDisc/Surfel/EWA modes), and graph geometry. Transient `DebugDraw` → `LineRenderPass` overlay path is functional. Standalone point cloud retained-mode upload (`.xyz`/`.pcd`/`.ply` without mesh vertex buffer) is pending — see `TODO.md`.

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
// (same device address as ForwardPass uses for the mesh):
//   PosBuf pBuf = PosBuf(push.ptrPos);  // GL_EXT_buffer_reference
//   vec3 p0 = pBuf.v[edgeIndices[lineID * 2 + 0]];
//   vec3 p1 = pBuf.v[edgeIndices[lineID * 2 + 1]];
//   // ... expand to screen-space quad (6 verts/segment)
```

### Lifetime semantics

Because the vertex/index buffers are stored as `std::shared_ptr<RHI::VulkanBuffer>` inside `GeometryGpuData`, GPU memory remains alive until **all** views using that buffer are destroyed. The BDA pointer remains valid as long as the underlying `VulkanBuffer` is alive.
