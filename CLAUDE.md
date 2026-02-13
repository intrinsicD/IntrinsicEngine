# IntrinsicEngine — Claude Code Guide

## Build System

- **Dependencies Setup**: SessionStart hook in `.claude/setup.sh`
- **Generator:** Ninja (required for C++20 modules — never use Unix Makefiles)
- **Compiler:** Clang 18+ with `clang-scan-deps-18` for module dependency scanning
- **Standard:** C++23 (`std::expected`, `std::format`, modules)
- **Standard library:** libstdc++-14 (for C++23 feature support with Clang)

### Quick Reference

```bash
# The SessionStart hook (.claude/setup.sh) installs deps, configures, and builds automatically.
# After session start, the build directory is ready at ./build/

# Rebuild everything
cmake --build build --parallel $(nproc)

# Build a specific target
cmake --build build --target IntrinsicRuntime
cmake --build build --target IntrinsicTests
cmake --build build --target IntrinsicCoreTests
cmake --build build --target IntrinsicECSTests
cmake --build build --target Sandbox

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test suite
ctest --test-dir build -R "SceneManager"

# Reconfigure (only needed if CMakeLists.txt changed)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 .
```

### Important: Do NOT

- Use `Unix Makefiles` generator — modules require Ninja
- Delete directories under `external/cache/` — CMake FetchContent state lives there
- Run `rm -rf build` unless you intend a full reconfigure (takes ~3 minutes)

### Important: Do

- Before implementing a new feature, scan for existing functionality in the codebase
- Refactor existing code only if necessary (e.g. to improve performance, reduce code duplication, create a good abstraction, a new feature requires it, or to extract a subsystem into its own module)
- Make sure the code is tested well
- Care about code quality, performance and maintainability of the codebase
- Care about state of the art methods are used (e.g. for rendering, data structures, algorithms, etc.)

## Project Structure

```
src/
  Core/          → IntrinsicCore    (no GPU deps: logging, memory, tasks, assets, FrameGraph)
  Runtime/
    *.cppm/cpp   → IntrinsicRuntime (Engine, GraphicsBackend, AssetPipeline, SceneManager, Selection)
    ECS/         → IntrinsicECS     (EnTT components, systems, Scene)
    Graphics/    → IntrinsicGraphics(RenderSystem, GPUScene, PipelineLibrary, Materials, Shaders)
    Geometry/    → IntrinsicGeometry (GeometryPool, collision, mesh data)
    Interface/   → IntrinsicInterface(ImGui panels, GUI helpers)
    RHI/         → IntrinsicRHI     (Vulkan abstraction: device, swapchain, buffers, images)
  Apps/Sandbox/  → Sandbox executable (demo app inheriting Engine)
tests/           → GTest suites (three targets: IntrinsicTests, IntrinsicCoreTests, IntrinsicECSTests)
external/cache/  → FetchContent download cache (do not delete individual subdirs)
```

## Module System (C++20)

- Module interface: `ModuleName.cppm` (e.g., `Runtime.SceneManager.cppm`)
- Module implementation: `ModuleName.cpp` (e.g., `Runtime.SceneManager.cpp`)
- Naming convention: `Namespace.ComponentName` (e.g., `Runtime.GraphicsBackend`, `Graphics.RenderSystem`)
- When adding a new module, update the parent `CMakeLists.txt`:
  - `.cppm` goes under `FILE_SET CXX_MODULES TYPE CXX_MODULES FILES`
  - `.cpp` goes under `PRIVATE`

## Subsystem Extraction Pattern

When extracting a subsystem from Engine, follow the established pattern (see GraphicsBackend, AssetPipeline, SceneManager):

1. **Module interface (.cppm):** Non-copyable, non-movable class. Dependencies via constructor. Accessors for owned resources.
2. **Module implementation (.cpp):** Constructor logs init, destructor logs shutdown and cleans up.
3. **Engine integration:** `std::unique_ptr<Subsystem>` member, accessor methods, delegate existing Engine methods.
4. **Tests:** Compile-time contract tests (not copyable, not movable) + functional tests. Add to `IntrinsicTests` target.
5. **ARCHITECTURE_ANALYSIS.md:** Remove completed items, keep only remaining work.

## Test Targets

| Target | Links | Needs Vulkan? | Purpose |
|---|---|---|---|
| `IntrinsicTests` | IntrinsicRuntime + GTest | Yes (headless) | Full integration tests |
| `IntrinsicCoreTests` | IntrinsicCore + GTest | No | Core-only (memory, tasks, hash, FrameGraph) |
| `IntrinsicECSTests` | IntrinsicCore + IntrinsicECS + GTest | No | ECS + FrameGraph system integration |

## Architecture Notes

- **No abstract base classes** for subsystems — concrete types with dependency injection
- **EnTT** for ECS (entity-component-system)
- **FrameGraph** schedules ECS systems with explicit dependencies
- **Retained-mode GPUScene** with persistent SSBOs; entities allocate slots via MeshRendererLifecycle system
- **Thread model:** Main thread owns Scene/GPU; worker threads for asset loading; mutex-protected queues for cross-thread communication
