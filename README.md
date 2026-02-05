# Intrinsic Engine

**Intrinsic** is a state-of-the-art Research & Rendering Engine designed for **High-Performance Geometry Processing** and **Real-Time Rendering**. It bridges the gap between rigorous mathematical formalism and "close-to-the-metal" engine architecture.

Built primarily on **C++23 Modules**, it features a **Vulkan 1.3** bindless rendering pipeline, a coroutine-based job system, and a custom mathematical kernel for collision and spatial queries.

---

## 🚀 Architectural Pillars

### 1. 🏗 Core Systems & Concurrency
*   **C++23 Modular Design**: Strict interface boundaries using `.cppm` partitions and `std::expected` for monadic error handling.
*   **Zero-Overhead Memory**:
    *   `LinearArena`: O(1) monotonic frame allocators.
    *   `ScopeStack`: LIFO allocator with destructor support for complex per-frame objects.
*   **Coroutine Task Scheduler**:
    *   Replaces traditional fibers with **C++20 `std::coroutine`**.
    *   Work-stealing queues.
    *   `Job` and `Yield()` support for cooperative multitasking.
*   **Telemetry & Profiling**:
    *   Lock-free ring-buffered telemetry system.
    *   Real-time tracking of CPU frame times, draw calls, and triangle counts.
*   **AssetManager Read Phases**:
    *   `AssetManager::Update()` is the single-writer phase on the main thread.
    *   Parallel systems that use `AssetManager::TryGetFast()` must be bracketed by `BeginReadPhase()` / `EndReadPhase()`.
    *   For long-lived access across reloads, use `AssetManager::AcquireLease()`.

### 2. 📐 Geometry & Spatial Processing
A "Distinguished Scientist" grade geometry kernel located in `Runtime.Geometry`:
*   **Primitives**: Comprehensive support for Spheres, AABBs, OBBs, Capsules, Cylinders (with pre-computed basis), and Convex Hulls.
*   **Intersection Solvers**:
    *   **Analytic**: Optimized SAT (Separating Axis Theorem) and algebraic solvers for primitive pairs.
    *   **GJK (Gilbert-Johnson-Keerthi)**: Generic convex collision detection fallback.
    *   **SDF (Signed Distance Fields)**: Contact manifold generation using gradient descent on SDFs.
*   **Spatial Acceleration**:
    *   **Linear Octree**: Cache-friendly spatial partitioning with configurable split strategies (Mean/Median/Center) and tight bounds.
    *   **Bounded Heaps**: For fast K-Nearest Neighbor (KNN) queries.

### 3. 🎨 Rendering (Vulkan 1.3)
*   **Bindless Architecture**: Full `descriptor_indexing` support for bindless textures.
*   **Frame Graph (Render Graph)**:
    *   Automatic dependency tracking and barrier injection (Sync2).
    *   Transient resource aliasing (memory reuse).
    *   Lambda-based pass declaration (`AddPass<Data>(setup, execute)`).
    *   Read-after-read layout transitions are enforced when layouts differ; reader stages/access are accumulated for later writer sync.
*   **Async Transfer System**:
    *   **Staging Belt**: Persistent ring-buffer allocator for high-throughput CPU-to-GPU streaming (`RHI.StagingBelt`).
    *   Timeline Semaphore synchronization for async asset uploads.
*   **Dynamic Rendering**: No `VkRenderPass` or `VkFramebuffer` objects; fully dynamic attachment binding.

---

## 🐧 Ubuntu Setup Guide

**Requirement**: This engine uses bleeding-edge C++23 features. You must use **Clang 18+** and **CMake 3.28+**.

### 1. Install Dependencies
```bash
# Build Tools & CMake
sudo apt update
sudo apt install build-essential cmake ninja-build git

# Vulkan SDK & Drivers
sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev spirv-tools glslc

# Windowing (GLFW) & System
sudo apt install libwayland-dev libxkbcommon-dev xorg-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### 2. Install Clang 18+ & Tools
C++ Modules support requires specific Clang tools for dependency scanning.

```bash
# 1. Install LLVM/Clang 18
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18

# 2. Install GCC Toolchain (for C++23 stdlib headers like <expected> and <format>)
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt update
sudo apt install libstdc++-14-dev

# 3. Install Clang Tools (Crucial for CMake module scanning)
sudo apt install clang-tools-18
```

---

## 🛠 Building & Running

### 1. Configure
We must explicitly point CMake to the `clang-scan-deps` utility matching our compiler version.

```bash
mkdir build && cd build

cmake -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18 \
    ..
```

### 2. Build
```bash
ninja
```

### 3. Run
```bash
./bin/Sandbox
```

---

## 🎮 Controls

*   **Left Click + Drag**: Orbit Camera.
*   **Right Click + WASD**: Fly Camera Mode.
*   **Drag & Drop**: Drop `.glb`, `.gltf`, `.ply`, or `.obj` files into the window to load them asynchronously.
*   **ImGui**:
    *   **Hierarchy**: View and select entities.
    *   **Inspector**: Modify Transforms (Position/Rotation/Scale) and view Mesh stats.
    *   **Assets**: Monitor async loading queues and asset states.
    *   **Performance**: Real-time telemetry graphs.

---

## 📂 Module Structure

*   **`Core`**: Foundation. `LinearArena`, `Scheduler` (Coroutines), `Telemetry`, `Filesystem`.
*   **`Geometry`**: The Math Kernel. `SDF`, `GJK`, `Octree`, `AABB`, `Contact`.
*   **`RHI`**: Vulkan Abstraction. `Device`, `Swapchain`, `Pipeline`, `TransferManager`.
*   **`Graphics`**: Engine Layer. `RenderGraph`, `ModelLoader`, `RenderSystem`, `Camera`.
*   **`ECS`**: Entity Component System. `Scene`, `Components`.
*   **`Interface`**: UI. `GUI` (ImGui backend).

---

## Forward Rendering (Stages 1/2/3)

The forward renderer has three conceptual stages:

- **Stage 1 (Instance Resolve):** Collects renderables and resolves materials/texture IDs.
- **Stage 2 (CPU Indirect Build):** Builds batched per-geometry `VkDrawIndexedIndirectCommand` streams on the CPU.
- **Stage 3 (GPU Culling / Indirect Build):** Uses the persistent `GPUScene` SSBOs, frustum-culls on the GPU, and produces a compacted indirect stream.

### Important contract

All instance data shared between CPU and GPU uses a std430-compatible layout and includes:

- `Model` matrix
- `TextureID`
- `EntityID`
- `GeometryID` (**new**): a stable per-geometry identifier used for GPU-driven batching/culling

**Stage 3 indirection (critical):** GPU culling writes a *packed visibility remap* per geometry.

- Culling output: `VisibleRemap[geomBase + drawIndex] = instanceSlot`
- Indirect output: `firstInstance = drawIndex`
- Vertex shader: `slot = VisibleRemap[geomBase + gl_InstanceIndex]`

This ensures `gl_InstanceIndex` is never treated as a global instance slot (it’s draw-local).

### GPUScene lifecycle

`GPUScene` is a retained-mode instance table. Slots are allocated/freed independently of ECS iteration order.

- **Allocation & spawn packet:** `Graphics::Systems::MeshRendererLifecycle::OnUpdate(...)` allocates `MeshRenderer::GpuSlot` and queues the initial instance+bounds packet.
- **Incremental updates:** `Graphics::Systems::GPUSceneSync::OnUpdate(...)` refreshes transforms and material TextureID when material revisions change.

### Why this matters

Loading multiple models should never cause previously loaded models to vanish. A previous bug came from mixing Stage 1/2 drawing with Stage 3 drawing (and clearing) in the same frame.

The forward pass is now structured so that only one path renders per frame:

- If GPU-driven culling is active and supported, Stage 3 is used.
- Otherwise the CPU-driven Stage 2 path renders.

This makes it difficult to accidentally double-draw or clear the backbuffer twice.

---

## 🧩 Troubleshooting

**`CMake Error: Could not find clang-scan-deps`**
Ensure you installed `clang-tools-18` and passed `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` correctly.

**`fatal error: 'format' file not found`**
Your `libstdc++` is too old. Ensure you installed `libstdc++-14-dev`.

**Asset Loading Fails**
Check the logs (`[ERR]`). Ensure the `assets/` folder is adjacent to the binary or in the project root. The engine attempts to resolve paths relative to the executable.

---

## Async Texture Uploads (Phase 1)

Texture streaming must not block worker threads on GPU work.

**Old (removed):** creating `RHI::Texture` from CPU pixel data implicitly recorded commands and waited on a fence (`vkWaitForFences`) via `RHI::CommandUtils::EndSingleTimeCommands`.

**New (current):** `Graphics::TextureLoader::LoadAsync(...)` decodes on CPU, allocates staging via `RHI::TransferManager` (timeline semaphore), records `vkCmdCopyBufferToImage` on the transfer queue, and returns a `RHI::TransferToken`.

**Bindless update policy:** descriptor writes are deferred via `RHI::BindlessDescriptorSystem::EnqueueUpdate(...)` and flushed once per frame at the start of `Graphics::RenderSystem::OnUpdate`.

While the upload is in flight, the texture’s bindless slot is bound to the engine default texture (bindless slot 0). When the token completes, the `AssetManager` transitions the asset to `Ready`.

**Key APIs:**
- `Graphics::TextureLoader::LoadAsync(path, device, transferManager, textureSystem, isSRGB)`
- `RHI::TextureSystem::CreatePending(width, height, format)`
- `Runtime::Engine::ProcessUploads()` polls `TransferManager::IsCompleted(token)`

**Guarantee:** no loader thread calls `vkWaitForFences` for texture uploads.
