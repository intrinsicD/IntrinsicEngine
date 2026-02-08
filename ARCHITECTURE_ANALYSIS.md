# IntrinsicEngine — Architecture Analysis & Improvement Proposals

## Executive Summary

IntrinsicEngine is a C++23 modules-first Vulkan game engine with a modern, data-oriented design. It demonstrates strong technical ambition: GPU-driven rendering, bindless textures, coroutine-based job scheduling, a render graph with automatic barrier generation, and custom zero-overhead allocators. The codebase is well-structured at the module level, with clear namespace conventions and a layered dependency graph.

However, the analysis reveals several architectural concerns — most critically around the `Engine` class acting as a God Object, thread-safety gaps in core systems, inconsistent error handling, and a few potential data races in the Vulkan synchronization layer. Below is a detailed breakdown organized by severity.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Critical Issues (Must Fix)](#2-critical-issues)
3. [Major Design Concerns (Should Fix)](#3-major-design-concerns)
4. [Moderate Issues (Consider Fixing)](#4-moderate-issues)
5. [Strengths Worth Preserving](#5-strengths-worth-preserving)
6. [Proposed Refactorings](#6-proposed-refactorings)
7. [Prioritized Roadmap](#7-prioritized-roadmap)

---

## 1. Architecture Overview

### Module Dependency Graph

```
Core  (no deps)
  ├── Memory: LinearArena, ScopeStack (zero-overhead frame allocators)
  ├── Tasks: coroutine-based work-stealing scheduler
  ├── Assets: async asset lifecycle with generational handles
  ├── Logging, Error, Filesystem, Hash, Input, Telemetry, Profiling
  └── Utils: LockFreeQueue, BoundedHeap, RingBuffer

RHI  (depends on Core)
  ├── Vulkan 1.3 abstraction: Device, Swapchain, Renderer
  ├── Resource management: Buffer, Image, Texture, TextureSystem
  ├── Synchronization: timeline semaphores, deferred deletion
  ├── Transfer: async GPU uploads with staging belt
  └── Bindless: descriptor array with dynamic updates

Geometry  (depends on Core)
  ├── Primitives: AABB, OBB, Sphere, Capsule, ConvexHull
  ├── Collision: GJK, EPA, SDF, contact manifolds
  ├── Spatial: Octree, Raycast, Containment, Overlap
  └── Mesh: HalfedgeMesh, MeshUtils, Validation

ECS  (depends on Core, uses EnTT)
  ├── Scene: entity registry wrapper
  ├── Components: Transform, Hierarchy, NameTag, Selection, AxisRotator
  └── Systems: Transform (hierarchy update), AxisRotator

Graphics  (depends on Core, RHI, Geometry, ECS)
  ├── RenderSystem: orchestrates frame rendering
  ├── RenderGraph: frame graph with automatic Vulkan barriers
  ├── GPUScene: retained-mode instance data (SSBOs + compute scatter)
  ├── Materials: pool with hot-reload and revision tracking
  ├── ModelLoader / TextureLoader: async with transfer tokens
  ├── Passes: Forward (GPU-driven culling), Picking, DebugView, ImGui
  └── PipelineLibrary: named PSO caching

Runtime  (depends on all above)
  ├── Engine: application base class (main loop, subsystem ownership)
  └── Selection: GPU picking, entity selection

Apps/Sandbox  (depends on Runtime)
  └── Example application: demo scene, UI panels
```

### Key Design Decisions

| Decision | Rationale | Assessment |
|----------|-----------|------------|
| C++23 modules (`.cppm`) | Compile-time isolation, no header pollution | **Excellent** — forward-looking, clean boundaries |
| `-fno-exceptions -fno-rtti` | Zero-overhead, deterministic error paths | **Good** — consistent with `std::expected` error model |
| LinearArena + ScopeStack | O(1) frame allocation, no fragmentation | **Excellent** — well-implemented custom allocators |
| EnTT for ECS | Mature, cache-friendly, archetype storage | **Good** — pragmatic choice, avoids NIH |
| Vulkan 1.3 + dynamic rendering | Modern API, no renderpass objects needed | **Good** — simplifies render graph integration |
| Bindless descriptors | Eliminates per-draw descriptor updates | **Excellent** — GPU-driven design |
| Timeline semaphores | Unified GPU/CPU synchronization | **Good** — but has implementation issues (see §2) |

---

## 2. Critical Issues

### 2.1 Data Race in `VulkanDevice::SignalGraphicsTimeline()`

**Location:** `src/Runtime/RHI/RHI.Device.cpp`

```cpp
uint64_t VulkanDevice::SignalGraphicsTimeline()
{
    const uint64_t value = m_GraphicsTimelineNextValue.fetch_add(1, std::memory_order_relaxed);
    m_GraphicsTimelineValue = value;  // NON-ATOMIC WRITE
    return value;
}

void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
{
    const uint64_t target = (m_GraphicsTimelineValue > 0)  // UNSYNCHRONIZED READ
        ? (m_GraphicsTimelineValue + 1) : 1;
    SafeDestroyAfter(target, std::move(deleteFn));
}
```

`m_GraphicsTimelineValue` is written from the render thread in `SignalGraphicsTimeline()` and read from any thread (including asset loader threads) in `SafeDestroy()`. This is a data race that could cause deferred deletions to execute too early (use-after-free on GPU) or too late (memory leak).

**Fix:** Make `m_GraphicsTimelineValue` an `std::atomic<uint64_t>`, or protect both read and write paths with `m_DeletionMutex`.

### 2.2 Data Race in `AssetManager::AssetsUiPanel()`

**Location:** `src/Core/Core.Assets.cpp`

The `AssetsUiPanel()` method iterates the EnTT registry (`m_Registry.view<AssetInfo>()`) without acquiring `m_Mutex`. Meanwhile, background loading threads modify the registry under lock via `Reload()` and `FinalizeLoad()`. This can cause iterator invalidation, use-after-free, or torn reads of `AssetInfo` state.

**Fix:** Acquire `std::shared_lock(m_Mutex)` for the entire UI panel iteration, or snapshot the data under lock and iterate the snapshot.

### 2.3 Coroutine Handle Lifetime Unsafety

**Location:** `src/Core/Core.Tasks.cpp`, `Reschedule()`

```cpp
void Scheduler::Reschedule(std::coroutine_handle<> h)
{
    DispatchInternal(LocalTask([h]() mutable {
        if (!h) return;   // std::coroutine_handle cannot be null; check is no-op
        h.resume();
        if (h.done()) h.destroy();
    }));
}
```

The null check on `std::coroutine_handle<>` is always false — it's not a nullable type. More critically, if the `Job` owning the coroutine frame is destroyed before this task executes, the handle becomes a dangling pointer and `h.resume()` is undefined behavior.

**Fix:** Use `std::shared_ptr` to share coroutine frame ownership between the `Job` object and any pending reschedule tasks, or enforce single-ownership with clear documentation that `Job` must outlive all pending reschedules.

### 2.4 Write-Access Mask Inconsistency in RenderGraph Barriers

**Location:** `src/Runtime/Graphics/Graphics.RenderGraph.cpp`

The inline write-access detection in the barrier generation lambda omits `VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR`, while the standalone `IsWriteAccess()` function includes it. If ray-tracing or acceleration structures are ever used, image barriers would miss write-after-write hazards, causing GPU corruption.

**Fix:** Extract write-access detection into a single shared function and use it consistently in all barrier paths.

---

## 3. Major Design Concerns

### 3.1 Engine Class Is a God Object

**Location:** `src/Runtime/Runtime.Engine.cppm`, `Runtime.Engine.cpp`

The `Engine` class owns and directly exposes **30+ public/protected members** spanning:

- Window management
- Vulkan context, device, and surface initialization
- Swapchain, renderer, GPU scene, pipeline library
- Frame arenas, descriptor pools, staging buffers
- Asset loading, texture system, material system, transfer queue
- ECS scene, entity spawning, hierarchy
- Task scheduler, main-thread dispatch queue
- Selection module, GUI initialization

The constructor alone is ~220 lines and performs 11 distinct initialization phases. The `Sandbox` application accesses protected members like `m_TransferManager`, `m_BindlessSystem`, and `m_TextureSystem` directly, creating deep coupling.

**Impact:** Cannot test subsystems in isolation. Cannot swap implementations. Any internal refactoring risks breaking application code.

**Proposed refactoring:** See §6.1.

### 3.2 No System Execution Order Enforcement

**Location:** ECS systems in `src/Runtime/ECS/Systems/` and `src/Runtime/Graphics/Systems/`

The engine requires a specific execution order:

```
MeshRendererLifecycle → Transform::OnUpdate → GPUSceneSync → RenderGraph
```

This ordering is enforced only by convention (comments in source). Nothing prevents a user from calling systems in the wrong order, which would cause silent data corruption (e.g., stale `WorldUpdatedTag` consumed before transforms are updated).

**Fix:** Introduce an explicit `SystemScheduler` that declares system dependencies and topologically sorts execution, or at minimum add runtime assertions that validate execution order.

### 3.3 `AssetManager::Clear()` Anti-Pattern

**Location:** `src/Core/Core.Assets.cpp`

```cpp
void AssetManager::Clear()
{
    {
        std::unique_lock lock(m_Mutex);
        m_Lookup.clear();
    }  // RELEASES LOCK
    m_Registry.clear();  // Triggers destructors WITHOUT holding m_Mutex
    // Comment: "Unlisten() can safely re-acquire it"
}
```

This relies on the assumption that destructors triggered by `m_Registry.clear()` will only call `Unlisten()`. If a destructor performs other operations that depend on `m_Mutex` state, deadlock or data corruption can occur. The pattern is fragile.

**Fix:** Use a deferred deletion queue. Collect entities to delete under lock, release lock, then destroy.

### 3.4 `AssetLease` PinCount Uses Relaxed Memory Ordering

**Location:** `src/Core/Core.Assets.cppm`

```cpp
m_Slot->PinCount.fetch_sub(1, std::memory_order_relaxed);  // In Reset()
m_Slot->PinCount.fetch_add(1, std::memory_order_relaxed);  // In PinIfValid()
```

Relaxed ordering provides no ordering guarantees relative to the actual resource pointer access. During concurrent reload, one thread could observe the pin count as non-zero while reading a stale (or partially-replaced) resource pointer.

**Fix:** Use `std::memory_order_acquire` on the add (pin) and `std::memory_order_release` on the sub (unpin) to establish happens-before relationship with the resource replacement.

### 3.5 Silent Hierarchy Cycle Detection Failure

**Location:** `src/Runtime/ECS/Components/ECS.Components.Hierarchy.cppm`

```cpp
if (Detail::IsDescendant(registry, child, newParent))
{
    return;  // Silent failure — no error logged, no error returned
}
```

When `Attach()` detects a cycle, it silently returns without notifying the caller. Client code cannot know the operation failed. Additionally, `glm::inverse()` is called during reparenting without checking for singular matrices (scale=0), which could produce NaN values.

**Fix:** Return `std::expected<void, ErrorCode>` from `Attach()`. Validate matrix before decomposition.

---

## 4. Moderate Issues

### 4.1 RenderGraph Functions Are Too Long

`Compile()` (~225 lines) handles resource resolution, barrier calculation, and dependency analysis in a single function. `Execute()` (~170 lines) mixes raster info precomputation, secondary buffer recording, and primary buffer recording. `RenderSystem::OnUpdate()` (~155 lines) combines interaction processing, global resource updates, render graph setup, pipeline execution, and presentation.

**Fix:** Extract into focused sub-functions: `ResolveResources()`, `CalculateBarriers()`, `BuildDependencyGraph()` for Compile; `PrepareRasterInfo()`, `RecordSecondaryBuffers()`, `RecordPrimaryBuffer()` for Execute.

### 4.2 Magic Numbers Throughout GPU Code

Examples:
- `GPUScene` constructor: `maxSets=64, storageBufferCount=64*3` — undocumented
- `GPUScene::EnsurePersistentBuffers`: `std::max<VkDeviceSize>(sceneBytes, 4)` — why 4?
- Forward pass compute: `const uint32_t wg = 64` — workgroup size not configurable
- `ImageCacheKeyHash`: `0x9e3779b9` used without naming

**Fix:** Extract all magic numbers into named constants with comments explaining the rationale.

### 4.3 Inconsistent Error Handling Strategy

The codebase mixes several error patterns:
- `std::expected<T, ErrorCode>` (Core allocators, filesystem)
- `nullptr` return + optional logging (asset queries)
- Silent `continue` in ECS system loops (GPU scene sync, mesh lifecycle)
- `VK_CHECK` macro for Vulkan calls (crashes on failure)
- `std::exit()` for unrecoverable errors (shader not found)

**Fix:** Establish a documented error strategy:
- Use `Expected<T>` for all fallible operations
- Log warnings for recoverable skip-and-continue cases
- Reserve `VK_CHECK` / abort only for truly unrecoverable GPU state corruption

### 4.4 `LinearArena` Thread Safety Check Disabled in Release

**Location:** `src/Core/Core.Memory.cpp`

```cpp
if (m_OwningThread != std::this_thread::get_id())
{
    assert(false && "LinearArena is not thread-safe...");  // gone in release
    return std::unexpected(AllocatorError::ThreadViolation);
}
```

The `assert` is stripped in release builds, and if the caller ignores the `Expected` error, silent memory corruption follows.

**Fix:** Always perform the thread check. Consider a compile-time flag for eliding the check in shipping builds rather than relying on `NDEBUG`.

### 4.5 No Entity Destruction Hooks

**Location:** `src/Runtime/Graphics/Systems/`

A comment in the code acknowledges:
```
// TODO(next): register on_destroy hooks in Engine/Scene setup for O(1) reclaim.
```

Currently, GPU slot cleanup for destroyed entities happens one frame late via a `view<MeshRenderer>(exclude<WorldMatrix>)` pattern. This means the GPU scene contains stale data for one frame and relies on radius=0 culling.

**Fix:** Register EnTT `on_destroy<MeshRenderer>` signal to immediately free GPU slots.

### 4.6 `ArenaAllocator` Lifetime Escape

**Location:** `src/Core/Core.Memory.cppm`

```cpp
class ArenaAllocator {
    LinearArena* m_Arena = nullptr;  // raw pointer, no lifetime enforcement
};
```

If a container using `ArenaAllocator` outlives the backing `LinearArena`, any subsequent allocation is use-after-free. No compile-time or runtime enforcement exists.

**Fix:** Document this clearly in the API. Consider a debug-mode check that validates arena liveness.

### 4.7 Timeline Deletion Queue Unbounded Growth

**Location:** `src/Runtime/RHI/RHI.Device.cpp`

`m_TimelineDeletionQueue` is an unbounded vector. If `CollectGarbage()` is not called regularly (e.g., in headless mode or during stalls), the queue grows indefinitely.

**Fix:** Add a maximum size with an assertion or backpressure mechanism that forces a flush when the queue exceeds a threshold.

### 4.8 Loader Capture Footgun in AssetManager

**Location:** `src/Core/Core.Assets.cppm`

The user-provided loader lambda is captured by value in a file watcher callback for hot-reload. If the loader captures stack references, they become dangling when the original scope exits.

**Fix:** Add documentation and consider enforcing that loaders are `std::is_nothrow_move_constructible` and do not hold references via a concept check.

---

## 5. Strengths Worth Preserving

### 5.1 Module Architecture
The C++23 module hierarchy is clean, with proper partition-based isolation. Namespace conventions mirror module paths consistently. No circular module dependencies exist. This is a strong foundation.

### 5.2 Memory System
LinearArena and ScopeStack are well-designed: cache-aligned, thread-ownership tracked, zero-overhead for POD types. The ScopeStack's intrusive destructor list (stored in the arena itself) is elegant and avoids heap allocation for metadata.

### 5.3 Render Graph
The RenderGraph with automatic Vulkan 1.3 synchronization2 barriers, topological sorting, resource aliasing, and transient allocation injection is a solid design. The attachment-ordering logic (processing attachments before explicit accesses) is correct for dynamic rendering constraints.

### 5.4 GPU-Driven Pipeline
The 3-stage forward pass with GPU frustum culling, bindless textures, compute-based scene scatter updates, and retained-mode GPU scene (SSBOs) is well-architected for scalability. The revision-based dirty tracking for materials avoids unnecessary GPU uploads.

### 5.5 Generational Handle System
The `ResourcePool<T, Handle>` with generation counters prevents use-after-free at the handle level. Frame-deferred deletion with soft/hard delete phases is correct for GPU resource lifecycles.

### 5.6 Async Asset Pipeline
The AssetManager's state machine (Unloaded → Loading → Processing → Ready/Failed) with pin-based leasing and listener callbacks is well-designed for concurrent access patterns.

### 5.7 Data-Oriented ECS Components
Tag-based dirty tracking (zero-size `IsDirtyTag`, `WorldUpdatedTag`) is cache-friendly and zero-overhead. The hierarchy component uses intrusive linked-list pointers rather than dynamic arrays, maintaining good memory locality.

---

## 6. Proposed Refactorings

### 6.1 Decompose the Engine God Object

**Current state:** Engine owns ~30 subsystems directly and exposes them via public/protected members.

**Proposed architecture:**

```
Engine (thin coordinator)
  ├── owns WindowSystem         → Window, Input
  ├── owns GraphicsBackend      → VulkanContext, Device, Swapchain, Renderer
  ├── owns AssetPipeline        → AssetManager, TransferManager, TextureSystem
  ├── owns SceneManager         → ECS::Scene, GPUScene, MaterialSystem
  ├── owns RenderOrchestrator   → RenderSystem, RenderGraph, PipelineLibrary
  └── exposes ServiceLocator    → typed access to subsystem interfaces
```

Each subsystem group becomes a self-contained class with:
- Its own constructor handling internal initialization
- A clean public interface (not raw member access)
- Independently testable lifecycle

The `ServiceLocator` pattern (or a typed context object) replaces direct member access:

```cpp
// Before:
auto result = Graphics::TextureLoader::LoadAsync(
    path, *GetDevice(), *m_TransferManager, *m_TextureSystem);

// After:
auto result = m_Assets.LoadTexture(path);  // AssetPipeline handles internals
```

### 6.2 Introduce a SystemScheduler

Replace manual system invocation with a declarative scheduler:

```cpp
SystemScheduler scheduler;
scheduler.Register<TransformSystem>()
    .Before<GPUSceneSyncSystem>();
scheduler.Register<MeshRendererLifecycleSystem>()
    .Before<TransformSystem>();
scheduler.Register<GPUSceneSyncSystem>()
    .Before<RenderGraph>();

scheduler.Execute(registry);  // Topological sort, then execute in order
```

This makes ordering dependencies explicit, verifiable, and debuggable.

### 6.3 Unify Error Handling

Define a project-wide error strategy:

| Context | Pattern |
|---------|---------|
| Fallible operations | `Expected<T>` (`std::expected<T, ErrorCode>`) |
| Recoverable skips | `Log::Warn()` + continue |
| Missing optional data | `std::optional<T>` or `nullptr` |
| Invariant violations | `assert()` (debug) or custom `INTRINSIC_ASSERT` (always-on) |
| Unrecoverable GPU errors | `VK_CHECK` → abort with diagnostic |

Apply consistently across all modules.

### 6.4 Extract RenderGraph Sub-Functions

Break `Compile()` into:
- `ResolveResources()` — allocate from pools, bind transient memory
- `CalculateBarriers()` — generate synchronization2 barriers per pass
- `BuildExecutionLayers()` — topological sort into execution layers

Break `Execute()` into:
- `PreparePassRasterInfo()` — compute rendering attachment setup
- `RecordPassCommands()` — secondary command buffer recording per layer
- `SubmitLayers()` — primary command buffer with barriers

### 6.5 Add Serialization Layer

The engine lacks serialization for scene/entity state. For editor workflows and save/load:

- Add a reflection-based serialization system for ECS components
- Use EnTT's snapshot/loader facilities or a custom visitor pattern
- Support JSON (for debugging) and binary (for shipping) formats

### 6.6 Formalize Thread-Safety Contracts

Every class should document its thread-safety guarantees:

```cpp
/// @thread_safety: NOT thread-safe. Must be accessed from owning thread only.
class LinearArena { ... };

/// @thread_safety: The following methods are thread-safe: Load(), GetState().
///                 UI methods (AssetsUiPanel) must be called from main thread.
class AssetManager { ... };
```

---

## 7. Prioritized Roadmap

### Tier 1 — Critical Bug Fixes (Immediate)

| # | Issue | Location | Effort |
|---|-------|----------|--------|
| 1 | Fix `m_GraphicsTimelineValue` data race | RHI.Device.cpp | Small |
| 2 | Fix `AssetsUiPanel()` data race | Core.Assets.cpp | Small |
| 3 | Fix coroutine handle lifetime UB | Core.Tasks.cpp | Medium |
| 4 | Unify write-access masks in RenderGraph barriers | Graphics.RenderGraph.cpp | Small |

### Tier 2 — Architectural Improvements (Next Sprint)

| # | Issue | Location | Effort |
|---|-------|----------|--------|
| 5 | Begin Engine decomposition (extract AssetPipeline) | Runtime.Engine | Large |
| 6 | Add SystemScheduler with dependency declarations | New module | Medium |
| 7 | Fix `AssetLease` PinCount memory ordering | Core.Assets.cppm | Small |
| 8 | Fix `AssetManager::Clear()` lock pattern | Core.Assets.cpp | Small |
| 9 | Register EnTT `on_destroy` hooks for GPU slots | Graphics.Systems | Small |

### Tier 3 — Code Quality (Ongoing)

| # | Issue | Location | Effort |
|---|-------|----------|--------|
| 10 | Extract RenderGraph sub-functions | Graphics.RenderGraph.cpp | Medium |
| 11 | Replace magic numbers with named constants | GPUScene, Forward pass, etc. | Small |
| 12 | Unify error handling patterns | All modules | Medium |
| 13 | Add thread-safety documentation | All public classes | Small |
| 14 | Bound timeline deletion queue growth | RHI.Device.cpp | Small |
| 15 | Always-on LinearArena thread check | Core.Memory.cpp | Small |

### Tier 4 — Feature Gaps (Future)

| # | Issue | Location | Effort |
|---|-------|----------|--------|
| 16 | Add scene serialization | New module | Large |
| 17 | Add configuration file system | Core | Medium |
| 18 | Complete Engine decomposition | Runtime | Large |

---

## Methodology

This analysis was performed by reading every module interface (`.cppm`) and implementation (`.cpp`) file across Core, RHI, Graphics, ECS, Geometry, Interface, and Runtime modules. Specific attention was paid to:

- Thread-safety and synchronization correctness
- Vulkan resource lifecycle and barrier management
- Memory management and allocation patterns
- Module coupling and dependency direction
- Error handling consistency
- Code complexity and function length

Total files analyzed: 91 module files + implementations + tests + build system.
