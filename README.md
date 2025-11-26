# Intrinsic Engine

**Intrinsic** is a Next-Gen Research & Rendering Engine built with **Modern C++ (C++23 Modules)** and **Vulkan 1.3**. It features a Data-Oriented architecture, a custom fiber-based task system, and a frame-graph based rendering pipeline.

This project serves as a testbed for high-performance graphics concepts, utilizing **Clang 18+** to leverage the latest C++ standards including Modules (`.cppm`), Concepts, and `std::expected`.

---

## 🚀 Key Features

### 🏗 Core Architecture
*   **C++23 Modules**: Zero-overhead build times and strict interface boundaries using `.cppm` files.
*   **No-Exception / No-RTTI**: Designed for maximum performance and stability (`-fno-exceptions`, `-fno-rtti`).
*   **Task System**: Fiber-based, work-stealing scheduler (`Core.Tasks`) for high-throughput parallel processing.
*   **Memory Management**: Custom `LinearArena` allocators for per-frame temporary allocations with O(1) reset cost.

### 🎨 Rendering & Graphics
*   **Vulkan 1.3 Native**: Utilizes Dynamic Rendering and Synchronization2 (no Render Passes or Framebuffers required).
*   **Render Graph (Frame Graph)**: 
    *   Automatic dependency tracking and barrier injection (Sync2).
    *   Transient resource aliasing (reuses memory for resources that don't overlap in time).
    *   Automatic layout transitions.
*   **Async Asset System**: Multi-threaded loading (Textures/Models) with lock-free synchronization for main-thread callbacks.
*   **glTF 2.0 Support**: Asynchronous loading of geometry and materials via TinyGLTF.
*   **ECS**: Entity-Component-System architecture powered by **EnTT**.

---

## 🐧 Ubuntu Setup Guide

**Warning:** This engine uses bleeding-edge C++ features. Standard repository compilers (GCC 11/12 or Clang 14) **will not work**. You must use Clang 18+ and a standard library that supports C++23.

### 1. Install Prerequisites
You need **CMake 3.28+**, **Ninja**, and the **Vulkan SDK**.

```bash
# Basic Build Tools
sudo apt update
sudo apt install build-essential cmake ninja-build git

# Vulkan SDK & Drivers
sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev spirv-tools

# Windowing Dependencies (GLFW)
sudo apt install libwayland-dev libxkbcommon-dev xorg-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### 2. Install Bleeding Edge Compiler (LLVM/Clang)
We require **Clang 18+** for proper C++ Module scanning support.

```bash
# 1. Add LLVM repository (automated script)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18  # Or 19/20 if available

# 2. Add GCC Toolchain PPA (Required for C++23 STL headers like <format> and <expected>)
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt update
sudo apt install libstdc++-14-dev

# 3. Install Clang Tools (Critical for CMake Module Scanning)
sudo apt install clang-tools-18
```

---

## 🛠 Building the Engine

### 1. Clone
```bash
git clone https://github.com/YourUsername/IntrinsicEngine.git
cd IntrinsicEngine
```

### 2. Configure
We must explicitly tell CMake to use Clang and point it to the specific **Dependency Scanner** (`clang-scan-deps`).

```bash
mkdir build && cd build

# Replace 'clang++-18' and 'clang-scan-deps-18' with your specific installed version
cmake -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18 \
    ..
```

### 3. Build & Run
```bash
ninja
./bin/Sandbox
```

---

## 🎮 Controls (Sandbox)

The default **Sandbox** application loads a glTF model and sets up an orbit camera.

*   **Left Click + Drag**: Rotate Camera (Orbit).
*   **Drag & Drop**: Drop `.glb` or `.gltf` files onto the window to load them dynamically.
*   **ImGui**: Use the overlay to adjust Sun Direction or view performance stats.

---

## 📂 Project Structure

*   **`src/Core`**: Low-level systems.
    *   `Core.Memory`: Linear Arena allocators.
    *   `Core.Tasks`: Thread pool and scheduler.
    *   `Core.Assets`: Async asset manager.
*   **`src/Runtime`**: The Engine layer.
    *   `Runtime.RHI`: Thin abstraction over Vulkan (Device, Swapchain, CommandBuffers).
    *   `Runtime.RenderGraph`: High-level rendering orchestration (Passes, Resources, Barriers).
    *   `Runtime.ECS`: Scene and Component management.
*   **`src/Apps`**: Application entry points (Sandbox).
*   **`assets/`**: Shaders (`.vert`, `.frag`), default textures, and models.

---

## 🧩 Common Issues

### `fatal error: 'format' file not found`
**Cause:** Your system's `libstdc++` is too old.
**Fix:** Install `libstdc++-14-dev` via the ubuntu-toolchain-r PPA.

### `CMake Error: Could not find clang-scan-deps`
**Cause:** CMake needs the scanner to parse C++20 Modules.
**Fix:** Ensure you installed `clang-tools-18` and passed `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` during configuration.

### `Validation Error: Dynamic viewport/scissor...`
**Cause:** Vulkan Dynamic State not set correctly.
**Fix:** Ensure the renderer calls `SetViewport` and `SetScissor` within the recording command buffer loop.

---

## 📦 Third-Party Libraries
Dependencies are automatically managed via CMake `FetchContent`:
*   **GLFW**: Windowing.
*   **Volk**: Vulkan Meta-Loader.
*   **VMA**: Vulkan Memory Allocator.
*   **GLM**: Math.
*   **EnTT**: ECS.
*   **TinyGLTF**: Model Loading.
*   **ImGui**: UI (Docking Branch).
*   **GoogleTest**: Testing Framework.