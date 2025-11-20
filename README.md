Here is a comprehensive `README.md` tailored to your project's specific architecture and bleeding-edge toolchain.

***

# Intrinsic Engine

**Intrinsic** is a Next-Gen Research & Rendering Engine built with Modern C++ (C++23) and Vulkan. It features a Data-Oriented architecture, a custom fiber-based task system, and a hybrid rendering graph.

## 🏗 Architecture

*   **Core:** Zero-overhead abstractions (C++20 Modules, Concepts).
*   **Memory:** Linear Arena Allocators (No `malloc` in the hot path).
*   **Tasks:** Fiber-based parallel job system.
*   **RHI:** Vulkan 1.3 (via `volk` meta-loader) with dynamic binding.

## 🚀 Prerequisites (Linux / Ubuntu)

This engine uses **C++23 Modules**, which requires a very recent compiler and build system.

### 1. System Tools & Build System
You need CMake 3.28+ and Ninja.

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git
```

### 2. Compiler (Bleeding Edge)
**Strict Requirement:** Clang 17+ or GCC 14+.
*Recommended Environment:* Clang 20+ (nightly) or GCC 14.

If using LLVM/Clang, you **must** install the matching `clang-tools` package to get `clang-scan-deps` (required for module scanning).

```bash
# Example for LLVM (Adjust version number as needed, e.g., -18, -19, -22)
sudo apt install clang-18 clang-tools-18 libstdc++-12-dev
```

### 3. Vulkan SDK
Required for the Render Hardware Interface.

```bash
# Install Vulkan loader, utils, and validation layers
sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev spirv-tools
```

### 4. Windowing Dependencies (GLFW)
Although CMake fetches GLFW automatically, you need the system headers for it to compile on Linux.

```bash
# X11 and Wayland dependencies
sudo apt install libwayland-dev libxkbcommon-dev xorg-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

---

## 🛠 Building the Engine

1.  **Clone the Repository:**
    ```bash
    git clone https://github.com/YourUsername/IntrinsicEngine.git
    cd IntrinsicEngine
    ```

2.  **Configure (CMake):**
    We strongly recommend using `Ninja` and explicit compilers if your system has multiple versions.

    ```bash
    # Create build directory
    mkdir build && cd build

    # Configure Debug build (Substitute clang++-22 with your version)
    cmake -G "Ninja" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=clang-22 \
        -DCMAKE_CXX_COMPILER=clang++-22 \
        ..
    ```

3.  **Build:**
    ```bash
    ninja
    ```

### Running the Sandbox
The build output is located in `build/bin`.

```bash
cd bin
./Sandbox
```

---

## 🧩 Troubleshooting

### "Permission denied" / `clang-scan-deps` not found
CMake needs to scan C++ modules to build the dependency graph. If it cannot find the scanner:

1.  Ensure `clang-tools-XX` is installed.
2.  Check where the binary is: `ls /usr/bin/clang-scan-deps*`
3.  Force CMake to use it:
    ```bash
    cmake -G "Ninja" -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22 ..
    ```

### Vulkan Initialization Errors
If the engine logs `[Vulkan Error]`, ensure you have a GPU driver that supports Vulkan 1.3.
*   **NVIDIA:** `sudo apt install nvidia-driver-535` (or newer)
*   **AMD/Intel:** `sudo apt install mesa-vulkan-drivers`

### CLion Setup
1.  **Toolchain:** Set "Generator" to **Ninja**.
2.  **CMake Options:** Add `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-XX` to the CMake options in Settings.
3.  **Environment:** Ensure `CC` and `CXX` point to your modern compiler.

---

## 📦 Third-Party Libraries
The engine automatically fetches and compiles these dependencies (no manual installation required):
*   **GLFW:** Windowing & Input.
*   **GLM:** Mathematics (SIMD optimized).
*   **Volk:** Meta-loader for Vulkan.
*   **GoogleTest:** Unit Testing framework.

## Codex
put into .bashrc or .zshrc for easy access:
alias architect="codex run --model gpt-5.1-codex-max --auto-fix --verification-command 'ninja -C cmake-build-debug'"