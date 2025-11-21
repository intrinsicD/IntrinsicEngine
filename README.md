# Intrinsic Engine

**Intrinsic** is a Next-Gen Research & Rendering Engine built with **Modern C++ (C++23 Modules)** and **Vulkan 1.3**. It features a Data-Oriented architecture, a custom fiber-based task system, and a hybrid rendering graph.

## 🏗 Architecture

*   **Core:** Zero-overhead abstractions (C++20 Modules, Concepts).
*   **Memory:** Linear Arena Allocators & VMA integration.
*   **RHI:** Vulkan 1.3 Dynamic Rendering (via `volk` & `vk_mem_alloc`).
*   **Pipeline:** Descriptor-based bindless-ready architecture.

---

## 🐧 Ubuntu Setup Guide

This engine uses **Bleeding Edge** C++ features (Modules, `std::format`, `std::expected`). The default compilers in Ubuntu 22.04/24.04 are often too old.

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
We require **Clang 18+** (tested on Clang 22). We also need the **GCC 14 Standard Library** because Clang uses the system's STL, and older versions lack `<format>`.

```bash
# 1. Add LLVM repository (automated script)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18  # Or 19/20/21/22 if available

# 2. Add GCC Toolchain PPA (Required for C++23 STL headers like <format>)
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt update
sudo apt install libstdc++-14-dev

# 3. Install Clang Tools (Critical for C++ Module Scanning)
# Replace '18' with your installed version
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
We must explicitly tell CMake which compiler and **Dependency Scanner** to use.

```bash
mkdir build && cd build

# Replace 'clang++-18' and 'clang-scan-deps-18' with your specific version
cmake -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18 \
    ..
```

*Note: If you are using CLion, go to **Settings -> Build, Execution, Deployment -> CMake** and add `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-18` to the "CMake options" field.*

### 3. Build & Run
```bash
ninja
./bin/Sandbox
```

---

## 🧩 Common Issues

### `fatal error: 'format' file not found`
**Cause:** Your `libstdc++` is too old (GCC 11/12).
**Fix:** Install `libstdc++-14-dev`.

### `/bin/sh: 1: : Permission denied`
**Cause:** CMake cannot find `clang-scan-deps`.
**Fix:** Ensure you installed `clang-tools-XX` and passed the `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` flag pointing to it.

### `Validation Error: Dynamic viewport/scissor...`
**Cause:** The renderer failed to set dynamic state.
**Fix:** Ensure `renderer.SetViewport()` is called every frame inside the render loop.

---

## 📦 Third-Party Libraries
The engine automatically fetches and compiles these dependencies via CMake FetchContent:
*   **GLFW:** Windowing & Input.
*   **GLM:** Mathematics.
*   **Volk:** Vulkan Meta-Loader.
*   **VulkanMemoryAllocator (VMA):** GPU Memory Management.
*   **GoogleTest:** Unit Testing.