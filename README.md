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
- **Coroutine Task Scheduler:** C++20 `std::coroutine` with work-stealing queues. `Job` and `Yield()` for cooperative multitasking.
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

**Halfedge Mesh (PMP-style):**
- Full halfedge data structure with `VertexHandle`, `EdgeHandle`, `FaceHandle`, `HalfedgeHandle`.
- Euler operations: `EdgeCollapse` (Dey-Edelsbrunner link condition), `EdgeFlip`, `EdgeSplit`.
- Arbitrary polygon support: `AddTriangle`, `AddQuad`, `AddFace(span<VertexHandle>)`.
- Property system with typed per-element storage and garbage collection.

**Graph Processing Operators:**
- **kNN Graph Builders:** `Geometry::Graph::BuildKNNGraph()` (Octree-accelerated neighbor discovery, exact kNN) and `Geometry::Graph::BuildKNNGraphFromIndices()` (manual graph assembly from precomputed kNN index lists) both support Union/Mutual connectivity and epsilon-based coincident-point rejection for degenerate robustness.
- **Graph Layouts:** Fruchterman-Reingold force-directed embedding (`ComputeForceDirectedLayout`), spectral embedding (`ComputeSpectralLayout`) with both combinatorial and symmetric-normalized Laplacian variants, and deterministic hierarchical layering (`ComputeHierarchicalLayout`) for stable DAG/tree-style 2D graph visualization.

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
| **Surface Reconstruction** | Point cloud → mesh via signed distance field + Marching Cubes | Hoppe et al. 1992 |

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
- **DebugDraw:** Immediate-mode line/shape rendering with screen-space thick-line expansion (SSBO-based, no geometry shader). Depth-tested and overlay variants.
- **Graph Processing:** Halfedge-based graph topology with robust Octree-accelerated kNN construction, force-directed 2D layout (`ComputeForceDirectedLayout`), spectral embedding (`ComputeSpectralLayout`) with combinatorial or symmetric-normalized Laplacian iteration, and hierarchical layered layout (`ComputeHierarchicalLayout`) for connectivity visualization workflows.
- **Selection Outlines:** Post-process contour highlight for selected/hovered entities.

### 4. Data I/O

Two-layer architecture: I/O backend (byte transport) separated from format loaders (parsing).

**Supported formats:**
| Category | Formats |
|---|---|
| **Mesh** | glTF 2.0 / GLB, OBJ, PLY (ASCII + binary) |
| **Point Cloud** | XYZ, PLY |
| **Graph** | TGF |
| **Texture** | PNG, JPG, KTX (via stb/KTX loaders) |

Drag-and-drop file loading with automatic format detection via `IORegistry`.

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

# Build everything
ninja -C build

# Build specific targets (faster incremental rebuilds)
ninja -C build IntrinsicGeometry      # geometry library only
ninja -C build IntrinsicGeometryTests # geometry tests
ninja -C build Sandbox                # application

# Run
./build/bin/Sandbox
```

**Sanitizers:** ASan + UBSan are auto-detected in Debug builds. If `libclang_rt.asan` is not installed, sanitizers are automatically disabled.

---

## Test Suite

Four test targets with clear GPU/no-GPU boundaries:

| Target | Dependencies | Scope | Tests |
|---|---|---|---|
| `IntrinsicCoreTests` | Core only | Memory, tasks, handles, frame graph, DAG scheduler | ~80 |
| `IntrinsicGeometryTests` | Core + Geometry | DEC, mesh operations, collision, graphs, all geometry operators | ~200 |
| `IntrinsicECSTests` | Core + ECS | FrameGraph system integration | ~15 |
| `IntrinsicTests` | Full Runtime | Graphics, I/O, rendering, integration | ~230 |

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
```

---

## Controls

- **Left Click + Drag:** Orbit camera.
- **Right Click + WASD:** Fly camera mode.
- **Drag & Drop:** Drop `.glb`, `.gltf`, `.ply`, `.obj`, `.xyz`, or `.tgf` files to load asynchronously.
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

**Performance note:** drawing deep octrees can generate thousands of line segments. Use `Max Depth` and `Leaf Only` to cap cost.

---

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
tests/            GTest suite: 33 test files across 4 targets
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

- **`ARCHITECTURE_ANALYSIS.md`** — Living roadmap: completed features, open TODOs, dependency graph, prioritized phases.
- **`CLAUDE.md`** — Development conventions, build workflows, architectural invariants.
