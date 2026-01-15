# Intrinsic Engine

**Intrinsic** is a state-of-the-art Research & Rendering Engine designed for **High-Performance Geometry Processing** and **Real-Time Rendering**. It bridges the gap between rigorous mathematical formalism and "close-to-the-metal" engine architecture.

Built strictly on **C++23 Modules**, it features a **Vulkan 1.3** bindless rendering pipeline, a coroutine-based job system, and a custom mathematical kernel for collision and spatial queries.

---

## 🚀 Architectural Pillars

### 1. 🏗 Core Systems & Concurrency
*   **C++23 Modular Design**: Strict interface boundaries using `.cppm` partitions and `std::expected` for monadic error handling.
*   **Zero-Overhead Memory**: 
    *   `LinearArena`: O(1) monotonic frame allocators.
    *   `ScopeStack`: LIFO allocator with destructor support for complex per-frame objects.
*   **Coroutine Task Scheduler**: 
    *   Replaces traditional fibers with **C++20 `std::coroutine`**.
    *   Wait-free work-stealing queues.
    *   `Job` and `Yield()` support for cooperative multitasking.
*   **Telemetry & Profiling**: 
    *   Lock-free ring-buffered telemetry system.
    *   Real-time tracking of CPU frame times, draw calls, and triangle counts.

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

## 🧩 Troubleshooting

**`CMake Error: Could not find clang-scan-deps`**
Ensure you installed `clang-tools-18` and passed `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` correctly.

**`fatal error: 'format' file not found`**
Your `libstdc++` is too old. Ensure you installed `libstdc++-14-dev`.

**Asset Loading Fails**
Check the logs (`[ERR]`). Ensure the `assets/` folder is adjacent to the binary or in the project root. The engine attempts to resolve paths relative to the executable.