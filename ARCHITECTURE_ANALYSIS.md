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

## 8. Detailed Refactoring Plan

This section provides concrete, file-level refactoring plans with before/after code examples, migration strategies, and risk assessments.

---

### Phase 1: Critical Data-Race Fixes (1–2 days)

These fixes are independent of each other and can be done in parallel. Each is small, localized, and low-risk.

#### 1.1 Fix `m_GraphicsTimelineValue` Data Race

**File:** `src/Runtime/RHI/RHI.Device.cppm` (line 170), `RHI.Device.cpp` (lines 157–205)

**Root cause:** `m_GraphicsTimelineValue` is a plain `uint64_t` written by the render thread in `SignalGraphicsTimeline()` and read by any thread in `SafeDestroy()`.

**Before (`RHI.Device.cppm`, line 170):**
```cpp
uint64_t m_GraphicsTimelineValue = 0;
```

**After:**
```cpp
std::atomic<uint64_t> m_GraphicsTimelineValue{0};
```

**Before (`RHI.Device.cpp`, `SignalGraphicsTimeline`):**
```cpp
uint64_t VulkanDevice::SignalGraphicsTimeline()
{
    const uint64_t value = m_GraphicsTimelineNextValue.fetch_add(1, std::memory_order_relaxed);
    m_GraphicsTimelineValue = value;  // non-atomic store
    return value;
}
```

**After:**
```cpp
uint64_t VulkanDevice::SignalGraphicsTimeline()
{
    const uint64_t value = m_GraphicsTimelineNextValue.fetch_add(1, std::memory_order_relaxed);
    m_GraphicsTimelineValue.store(value, std::memory_order_release);
    return value;
}
```

**Before (`RHI.Device.cpp`, `SafeDestroy`):**
```cpp
void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
{
    const uint64_t target = (m_GraphicsTimelineValue > 0)
        ? (m_GraphicsTimelineValue + 1) : 1;
    SafeDestroyAfter(target, std::move(deleteFn));
}
```

**After:**
```cpp
void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
{
    const uint64_t current = m_GraphicsTimelineValue.load(std::memory_order_acquire);
    const uint64_t target = (current > 0) ? (current + 1) : 1;
    SafeDestroyAfter(target, std::move(deleteFn));
}
```

**Before (`RHI.Device.cppm`, `GetGraphicsTimelineValue`):**
```cpp
[[nodiscard]] uint64_t GetGraphicsTimelineValue() const { return m_GraphicsTimelineValue; }
```

**After:**
```cpp
[[nodiscard]] uint64_t GetGraphicsTimelineValue() const
{
    return m_GraphicsTimelineValue.load(std::memory_order_acquire);
}
```

**Risk:** Minimal. Atomic stores/loads are drop-in replacements. The release/acquire pair ensures that any thread reading the timeline value sees all writes that happened before the signal.

**Validation:** Run existing tests. Add a stress test that calls `SafeDestroy()` from multiple threads concurrently while the main thread calls `SignalGraphicsTimeline()`.

---

#### 1.2 Fix `AssetsUiPanel()` Data Race

**File:** `src/Core/Core.Assets.cpp` (lines 210–377)

**Root cause:** `AssetsUiPanel()` iterates `m_Registry.view<AssetInfo>()` without holding `m_Mutex`, while background threads modify the registry under lock in `FinalizeLoad()` and `Reload()`.

**Strategy: Snapshot under lock, iterate snapshot outside lock.**

**Before (line ~215):**
```cpp
void AssetManager::AssetsUiPanel()
{
    int countReady = 0, countLoading = 0, countFailed = 0;
    auto view = m_Registry.view<AssetInfo>();
    for (auto [entity, info] : view.each())
    {
        // ... count and render rows directly from registry ...
    }
}
```

**After:**
```cpp
void AssetManager::AssetsUiPanel()
{
    // Snapshot asset state under lock to avoid racing with background loaders.
    struct AssetSnapshot { entt::entity ID; AssetInfo Info; };
    std::vector<AssetSnapshot> snapshot;
    {
        std::shared_lock lock(m_Mutex);
        auto view = m_Registry.view<AssetInfo>();
        snapshot.reserve(view.size_hint());
        for (auto [entity, info] : view.each())
            snapshot.push_back({entity, info});
    }

    int countReady = 0, countLoading = 0, countFailed = 0;
    for (const auto& [id, info] : snapshot)
    {
        // ... count and render rows from snapshot ...
    }
}
```

**Caveat:** `m_Mutex` is currently `std::mutex`, not `std::shared_mutex`. If other read-only paths also need concurrent access, upgrade to `std::shared_mutex` as part of this change. Otherwise a plain `std::unique_lock` suffices (the UI panel runs on the main thread and is not performance-critical).

**Risk:** Low. Only affects the debug UI path. The snapshot adds a per-frame allocation but only when the panel is open.

---

#### 1.3 Fix Coroutine Handle Lifetime

**File:** `src/Core/Core.Tasks.cppm` (lines 120–158), `Core.Tasks.cpp` (lines 167–195)

**Root cause:** When `Dispatch(Job&&)` is called, the coroutine handle is extracted from the `Job` and scheduled as a `LocalTask`. If the `LocalTask` executes after another code path destroys the coroutine frame, `h.resume()` is UB. Additionally, the null check `if (!h) return;` is meaningless for `std::coroutine_handle<>`.

**Strategy:** Use an `std::shared_ptr<std::atomic<bool>>` as a cancellation token shared between the `Job` destructor and the scheduled task.

**Before (`Core.Tasks.cpp`, `Reschedule`):**
```cpp
void Scheduler::Reschedule(std::coroutine_handle<> h)
{
    if (!s_Ctx) return;
    if (!h) return;

    DispatchInternal(LocalTask([h]() mutable {
        if (!h) return;
        h.resume();
        if (h.done()) h.destroy();
    }));
}
```

**After:**
```cpp
void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive)
{
    if (!s_Ctx) return;

    DispatchInternal(LocalTask([h, alive = std::move(alive)]() mutable {
        // If the owning Job was destroyed, the coroutine frame is gone.
        if (alive && !alive->load(std::memory_order_acquire))
            return;

        h.resume();
        if (h.done())
            h.destroy();
    }));
}
```

**Job class additions:**
```cpp
class Job
{
public:
    ~Job()
    {
        if (m_Alive)
            m_Alive->store(false, std::memory_order_release);
        if (m_Handle && !m_Handle.done())
            m_Handle.destroy();
    }

    // Move constructor transfers alive token
    Job(Job&& other) noexcept
        : m_Handle(other.m_Handle), m_Alive(std::move(other.m_Alive))
    {
        other.m_Handle = {};
    }

private:
    std::coroutine_handle<promise_type> m_Handle{};
    std::shared_ptr<std::atomic<bool>> m_Alive = std::make_shared<std::atomic<bool>>(true);
    friend class Scheduler;
};
```

**Dispatch update:**
```cpp
void Scheduler::Dispatch(Job&& job)
{
    if (!s_Ctx) return;
    if (!job.Valid()) return;

    auto h = job.m_Handle;
    auto alive = job.m_Alive;  // Share lifetime token
    job.m_Handle = {};
    job.m_Alive = nullptr;     // Transfer ownership to scheduler

    Reschedule(h, std::move(alive));
}
```

**Risk:** Medium. The `shared_ptr` adds a heap allocation per `Job` but Jobs are created infrequently. The `YieldAwaiter::await_suspend` also needs access to the alive token — thread it through the promise type or store it in a thread-local.

**Alternative (simpler):** Document that `Job` objects must outlive all pending reschedules and add a debug-mode assertion. This is viable if Jobs are always stored in long-lived containers.

---

#### 1.4 Unify Write-Access Mask in RenderGraph

**File:** `src/Runtime/Graphics/Graphics.RenderGraph.cpp`

**Root cause:** Three separate write-access checks exist with different flag sets.

**Before (3 locations):**

Image barriers (line 542):
```cpp
const bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT |
    VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
```

Buffer barriers (line 678):
```cpp
bool prevWrite = (res.LastUsageAccess & (VK_ACCESS_2_MEMORY_WRITE_BIT |
    VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
```

Inline function (line 715):
```cpp
inline bool IsWriteAccess(VkAccessFlags2 a) noexcept
{
    return (a & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                 VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                 VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)) != 0;
}
```

**After (single source of truth):**

Move the function to the top of the file (before `Compile()`), and replace all inline checks:

```cpp
namespace
{
    constexpr VkAccessFlags2 kWriteAccessMask =
        VK_ACCESS_2_MEMORY_WRITE_BIT |
        VK_ACCESS_2_SHADER_WRITE_BIT |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_TRANSFER_WRITE_BIT |
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

    [[nodiscard]] constexpr bool IsWriteAccess(VkAccessFlags2 a) noexcept
    {
        return (a & kWriteAccessMask) != 0;
    }
}
```

Then replace all 3 inline checks:
```cpp
// Image barriers (was line 542)
const bool prevWrite = IsWriteAccess(res.LastUsageAccess);
const bool currWrite = IsWriteAccess(dstAccess);

// Buffer barriers (was line 678)
bool prevWrite = IsWriteAccess(res.LastUsageAccess);
bool currWrite = IsWriteAccess(node->Access);
```

**Risk:** Minimal. Pure mechanical replacement with strictly wider coverage (buffer barriers gain the missing attachment write bits).

---

### Phase 2: Engine Decomposition (1–2 weeks)

This is the largest refactoring. It should be done incrementally — one subsystem extraction per PR — to keep reviews manageable.

#### 2.1 Step 1: Extract `GraphicsBackend`

**New file:** `src/Runtime/Runtime.GraphicsBackend.cppm`

**Moves from Engine:**
```
m_Context            → GraphicsBackend::m_Context
m_Device             → GraphicsBackend::m_Device
m_Surface            → GraphicsBackend::m_Surface
m_Swapchain          → GraphicsBackend::m_Swapchain
m_Renderer           → GraphicsBackend::m_Renderer
m_DescriptorLayout   → GraphicsBackend::m_DescriptorLayout
m_DescriptorPool     → GraphicsBackend::m_DescriptorPool
m_BindlessSystem     → GraphicsBackend::m_BindlessSystem
m_DefaultTexture     → GraphicsBackend::m_DefaultTexture
m_DefaultTextureIndex→ GraphicsBackend::m_DefaultTextureIndex
```

**Interface:**
```cpp
export class GraphicsBackend
{
public:
    struct Config
    {
        std::string AppName;
        bool EnableValidation = true;
    };

    explicit GraphicsBackend(Core::Windowing::Window& window, const Config& config);
    ~GraphicsBackend();

    // Non-copyable, non-movable (owns Vulkan handles)
    GraphicsBackend(const GraphicsBackend&) = delete;
    GraphicsBackend& operator=(const GraphicsBackend&) = delete;

    // Accessors (replace Engine's protected getters)
    [[nodiscard]] std::shared_ptr<RHI::VulkanDevice> GetDevice() const;
    [[nodiscard]] RHI::VulkanSwapchain& GetSwapchain();
    [[nodiscard]] RHI::SimpleRenderer& GetRenderer();
    [[nodiscard]] RHI::DescriptorAllocator& GetDescriptorPool();
    [[nodiscard]] RHI::DescriptorLayout& GetDescriptorLayout();
    [[nodiscard]] RHI::BindlessDescriptorSystem& GetBindlessSystem();
    [[nodiscard]] uint32_t GetDefaultTextureIndex() const;

private:
    std::unique_ptr<RHI::VulkanContext>            m_Context;
    std::shared_ptr<RHI::VulkanDevice>             m_Device;
    VkSurfaceKHR                                   m_Surface = VK_NULL_HANDLE;
    std::unique_ptr<RHI::VulkanSwapchain>          m_Swapchain;
    std::unique_ptr<RHI::SimpleRenderer>           m_Renderer;
    std::unique_ptr<RHI::DescriptorLayout>         m_DescriptorLayout;
    std::unique_ptr<RHI::DescriptorAllocator>      m_DescriptorPool;
    std::unique_ptr<RHI::BindlessDescriptorSystem> m_BindlessSystem;
    std::shared_ptr<RHI::Texture>                  m_DefaultTexture;
    uint32_t                                       m_DefaultTextureIndex = 0;
};
```

**Constructor takes** Engine's current init steps 5–7 (Vulkan context, device, descriptors, bindless, default texture). The `Window&` reference replaces the need for Engine to own both.

**Engine after extraction:**
```cpp
class Engine
{
protected:
    std::unique_ptr<Core::Windowing::Window> m_Window;
    GraphicsBackend m_Graphics;       // Replaces 10 individual members
    // ... remaining members unchanged ...
};
```

**Migration for Sandbox:**
```cpp
// Before:
GetDevice()->SomeCall();
// After:
m_Graphics.GetDevice()->SomeCall();
```

This is a mechanical find-and-replace in Sandbox. No behavioral change.

---

#### 2.2 Step 2: Extract `AssetPipeline`

**New file:** `src/Runtime/Runtime.AssetPipeline.cppm`

**Moves from Engine:**
```
m_AssetManager      → AssetPipeline (already public, wrap it)
m_TransferManager   → AssetPipeline::m_TransferManager
m_TextureSystem     → AssetPipeline::m_TextureSystem
m_LoadMutex         → AssetPipeline::m_LoadMutex
m_PendingLoads      → AssetPipeline::m_PendingLoads
m_LoadedMaterials   → AssetPipeline::m_LoadedMaterials
ProcessUploads()    → AssetPipeline::ProcessUploads()
RegisterAssetLoad() → AssetPipeline::RegisterLoad()
```

**Interface:**
```cpp
export class AssetPipeline
{
public:
    AssetPipeline(RHI::VulkanDevice& device, RHI::BindlessDescriptorSystem& bindless);
    ~AssetPipeline();

    // Public asset manager (existing interface preserved)
    [[nodiscard]] Core::Assets::AssetManager& Assets() { return m_AssetManager; }

    // Transfer management
    [[nodiscard]] RHI::TransferManager& GetTransferManager() { return *m_TransferManager; }
    [[nodiscard]] RHI::TextureSystem& GetTextureSystem() { return *m_TextureSystem; }

    // Async load registration (replaces Engine::RegisterAssetLoad)
    void RegisterLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token);

    template <typename F>
    void RegisterLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token, F&& onComplete);

    // Per-frame processing (called from Engine::Run)
    void ProcessUploads();
    void ProcessDeletions(uint64_t currentFrame);
    void GarbageCollect();

private:
    Core::Assets::AssetManager                  m_AssetManager;
    std::unique_ptr<RHI::TransferManager>       m_TransferManager;
    std::unique_ptr<RHI::TextureSystem>         m_TextureSystem;
    std::mutex                                  m_LoadMutex;
    std::vector<PendingLoad>                    m_PendingLoads;
    std::vector<Core::Assets::AssetHandle>      m_LoadedMaterials;
};
```

**Engine after extraction:**
```cpp
class Engine
{
public:
    AssetPipeline m_Assets;  // Replaces 6 members + 2 methods
    // ...
};
```

**Migration for Sandbox:**
```cpp
// Before:
m_AssetManager.Load<Model>(path, loader);
auto result = Graphics::TextureLoader::LoadAsync(path, *GetDevice(), *m_TransferManager, *m_TextureSystem);
RegisterAssetLoad(handle, token);

// After:
m_Assets.Assets().Load<Model>(path, loader);
auto result = Graphics::TextureLoader::LoadAsync(path, m_Assets.GetDevice(), m_Assets.GetTransferManager(), m_Assets.GetTextureSystem());
m_Assets.RegisterLoad(handle, token);
```

---

#### 2.3 Step 3: Extract Main-Thread Dispatch Queue

**Moves from Engine:**
```
m_MainThreadQueueMutex  → MainThreadDispatcher::m_Mutex
m_MainThreadQueue       → MainThreadDispatcher::m_Queue
ProcessMainThreadQueue() → MainThreadDispatcher::Process()
RunOnMainThread()        → MainThreadDispatcher::Enqueue()
```

This is a small, self-contained utility class:

```cpp
export class MainThreadDispatcher
{
public:
    template <typename F>
    void Enqueue(F&& task)
    {
        std::lock_guard lock(m_Mutex);
        m_Queue.emplace_back(std::forward<F>(task));
    }

    void Process()
    {
        std::vector<Core::Tasks::LocalTask> batch;
        {
            std::lock_guard lock(m_Mutex);
            batch.swap(m_Queue);
        }
        for (auto& task : batch)
        {
            if (task.Valid()) task();
        }
    }

private:
    std::mutex m_Mutex;
    std::vector<Core::Tasks::LocalTask> m_Queue;
};
```

---

#### 2.4 Decomposition Summary

After all extractions, the Engine class becomes:

```cpp
export class Engine
{
public:
    explicit Engine(const EngineConfig& config);
    virtual ~Engine();
    void Run();

    // Pure virtuals for application
    virtual void OnStart() = 0;
    virtual void OnUpdate(float deltaTime) = 0;
    virtual void OnRender() = 0;

    // Entity spawning
    entt::entity SpawnModel(Core::Assets::AssetHandle model,
                            Core::Assets::AssetHandle material,
                            glm::vec3 position,
                            glm::vec3 scale = glm::vec3(1.0f));

    // Subsystem access
    [[nodiscard]] SelectionModule& GetSelection();

    // Public subsystems (application-facing)
    ECS::Scene                                    m_Scene;
    GraphicsBackend                               m_Graphics;
    AssetPipeline                                 m_Assets;
    Graphics::GeometryPool                        m_GeometryStorage;
    std::unique_ptr<Graphics::RenderSystem>       m_RenderSystem;
    std::unique_ptr<Graphics::MaterialSystem>     m_MaterialSystem;
    Core::Memory::LinearArena                     m_FrameArena;
    Core::Memory::ScopeStack                      m_FrameScope;
    SelectionModule                               m_Selection;

protected:
    std::unique_ptr<Core::Windowing::Window>      m_Window;

private:
    MainThreadDispatcher                          m_Dispatcher;
    Graphics::ShaderRegistry                      m_ShaderRegistry;
    std::unique_ptr<Graphics::PipelineLibrary>    m_PipelineLibrary;
    std::unique_ptr<Graphics::GPUScene>           m_GpuScene;
    bool                                          m_Running = true;
    bool                                          m_FramebufferResized = false;

    void InitPipeline();
    void LoadDroppedAsset(const std::string& path);
};
```

**Result:** Engine drops from ~30 members to ~18, with clear subsystem boundaries. The constructor shrinks from ~220 lines to ~80 (delegating to subsystem constructors).

---

### Phase 3: System Scheduler (3–5 days)

#### 3.1 New Module: `ECS.SystemScheduler`

**File:** `src/Runtime/ECS/ECS.SystemScheduler.cppm`

**Design goals:**
- Declarative dependency specification
- Topological sort at build time
- Runtime validation that execution order matches declared dependencies
- Zero overhead in release (validation stripped)

```cpp
export module ECS.SystemScheduler;

import Core.Hash;
import Core.Logging;

export namespace ECS
{
    // Type-erased system wrapper
    class ISystem
    {
    public:
        virtual ~ISystem() = default;
        virtual void Execute(entt::registry& reg) = 0;
        [[nodiscard]] virtual Core::StringID GetID() const = 0;
    };

    // Typed system wrapper — adapts free functions to ISystem
    template <auto Fn, Core::StringID ID>
    class FreeSystem final : public ISystem
    {
    public:
        void Execute(entt::registry& reg) override { Fn(reg); }
        [[nodiscard]] Core::StringID GetID() const override { return ID; }
    };

    // Builder returned by Register() for chaining .Before()/.After()
    class SystemHandle
    {
    public:
        SystemHandle& Before(Core::StringID other);
        SystemHandle& After(Core::StringID other);
    private:
        friend class SystemScheduler;
        Core::StringID m_ID;
        SystemScheduler* m_Owner;
    };

    class SystemScheduler
    {
    public:
        // Register a system with explicit ID
        SystemHandle Register(Core::StringID id, std::unique_ptr<ISystem> system);

        // Register a free function as a system
        template <auto Fn>
        SystemHandle Register(Core::StringID id)
        {
            // Wrap the free function into FreeSystem<Fn, id>
            // ...
        }

        // Build execution order (topological sort). Call once after all Register() calls.
        // Returns false if a cycle is detected.
        [[nodiscard]] bool Build();

        // Execute all systems in sorted order.
        void Execute(entt::registry& registry);

        // Debug: print execution order
        void DumpOrder() const;

    private:
        struct SystemNode
        {
            Core::StringID              ID;
            std::unique_ptr<ISystem>    System;
            std::vector<Core::StringID> RunsBefore;  // This system runs before these
            std::vector<Core::StringID> RunsAfter;   // This system runs after these
        };

        std::vector<SystemNode>         m_Systems;
        std::vector<size_t>             m_ExecutionOrder;  // Indices into m_Systems
        bool                            m_Built = false;
    };
}
```

#### 3.2 Registration in Engine

**Before (in `Engine::Run`, physics loop):**
```cpp
ECS::Systems::Transform::OnUpdate(m_Scene.GetRegistry());
Graphics::Systems::MeshRendererLifecycle::OnUpdate(
    m_Scene.GetRegistry(), *m_GpuScene, m_AssetManager,
    *m_MaterialSystem, m_GeometryStorage, m_DefaultTextureIndex);
Graphics::Systems::GPUSceneSync::OnUpdate(
    m_Scene.GetRegistry(), *m_GpuScene, m_AssetManager,
    *m_MaterialSystem, m_DefaultTextureIndex);
```

**After (in `Engine::InitPipeline`):**
```cpp
m_Scheduler.Register("MeshLifecycle"_id,
    std::make_unique<MeshRendererLifecycleSystem>(
        *m_GpuScene, m_Assets.Assets(), *m_MaterialSystem,
        m_GeometryStorage, m_Graphics.GetDefaultTextureIndex()));

m_Scheduler.Register("Transform"_id,
    std::make_unique<TransformSystem>())
    .After("MeshLifecycle"_id);

m_Scheduler.Register("GPUSceneSync"_id,
    std::make_unique<GPUSceneSyncSystem>(
        *m_GpuScene, m_Assets.Assets(), *m_MaterialSystem,
        m_Graphics.GetDefaultTextureIndex()))
    .After("Transform"_id);

m_Scheduler.Build();  // Topological sort, detect cycles
```

**After (in `Engine::Run`, physics loop):**
```cpp
m_Scheduler.Execute(m_Scene.GetRegistry());
```

**Systems need to be adapted** from free functions with many parameters to classes that capture their dependencies at construction:

```cpp
class GPUSceneSyncSystem final : public ECS::ISystem
{
public:
    GPUSceneSyncSystem(Graphics::GPUScene& gpuScene,
                       Core::Assets::AssetManager& assets,
                       Graphics::MaterialSystem& materials,
                       uint32_t defaultTextureIndex)
        : m_GpuScene(gpuScene), m_Assets(assets),
          m_Materials(materials), m_DefaultTexIdx(defaultTextureIndex) {}

    void Execute(entt::registry& reg) override
    {
        Graphics::Systems::GPUSceneSync::OnUpdate(
            reg, m_GpuScene, m_Assets, m_Materials, m_DefaultTexIdx);
    }

    [[nodiscard]] Core::StringID GetID() const override { return "GPUSceneSync"_id; }

private:
    Graphics::GPUScene& m_GpuScene;
    Core::Assets::AssetManager& m_Assets;
    Graphics::MaterialSystem& m_Materials;
    uint32_t m_DefaultTexIdx;
};
```

This preserves the existing free-function implementations (no rewrite needed) while adding order enforcement.

---

### Phase 4: RenderGraph Function Decomposition (2–3 days)

#### 4.1 Break Down `Compile()` (225 lines → 4 functions)

**Current structure of `Compile()` (line 487–711):**
```
Lines 491–522:  Resource resolution (pool allocation, transient binding)
Lines 525–665:  Image barrier calculation
Lines 667–705:  Buffer barrier calculation
Lines 708–710:  Adjacency list construction
```

**Proposed extraction:**

```cpp
void RenderGraph::Compile(/* params */)
{
    ResolveResources(frameIndex, transientPool);
    CalculateImageBarriers();
    CalculateBufferBarriers();
    BuildAdjacencyList();
}
```

Each function operates on `m_Passes` and `m_ResourceNodes` (already member state), so no parameter explosion occurs.

**`ResolveResources()` (lines 491–522):**
```cpp
void RenderGraph::ResolveResources(uint32_t frameIndex,
                                   RHI::TransientAttachmentPool* transientPool)
{
    for (auto& [id, res] : m_ResourceNodes)
    {
        // Existing pool allocation logic (lines 491-522)
        // ...
    }
}
```

**`CalculateImageBarriers()` — the largest block (lines 525–665):**

This block contains a 59-line lambda `pushImageBarrier`. Extract it as a private method:

```cpp
// Private method replacing the lambda
void RenderGraph::EmitImageBarrier(ResourceNode& res,
                                   VkPipelineStageFlags2 dstStage,
                                   VkAccessFlags2 dstAccess,
                                   VkImageLayout targetLayout,
                                   PassNode& pass,
                                   VkImageMemoryBarrier2*& imgStart,
                                   uint32_t& imgCount)
{
    // Existing lambda body (lines 533-591)
    // Replace all captured references with parameters
}
```

Then `CalculateImageBarriers()` becomes a straightforward loop:
```cpp
void RenderGraph::CalculateImageBarriers()
{
    for (auto& pass : m_Passes)
    {
        VkImageMemoryBarrier2* imgStart = nullptr;
        uint32_t imgCount = 0;

        // Process attachments first (existing lines 600-640)
        for (auto& att : pass.Attachments)
            EmitImageBarrier(/* ... */);

        // Then explicit accesses (existing lines 645-660)
        for (auto& acc : pass.Accesses)
            if (acc.Type == ResourceType::Image)
                EmitImageBarrier(/* ... */);

        pass.ImageBarriers = imgStart;
        pass.ImageBarrierCount = imgCount;
    }
}
```

**Risk:** Low. These are pure structural extractions with no behavioral change.

---

#### 4.2 Break Down `Execute()` (172 lines → 3 functions)

**Proposed:**
```cpp
void RenderGraph::Execute(VkCommandBuffer cmd, /* params */)
{
    if (!m_Compiled) Compile(/* ... */);

    PrepareRasterInfo();
    DispatchSecondaryRecording();
    RecordPrimaryCommandBuffer(cmd);
}
```

**`PrepareRasterInfo()` (lines 867–899):**
Pre-computes `VkRenderingInfo` for each pass that uses dynamic rendering. Currently done inline in the execution loop.

**`DispatchSecondaryRecording()` (lines 901–937):**
Dispatches tasks to the scheduler for parallel secondary command buffer recording.

**`RecordPrimaryCommandBuffer()` (lines 939–1017):**
Records barriers and executes secondary buffers into the primary buffer.

---

#### 4.3 Break Down `RenderSystem::OnUpdate()` (155 lines → 5 functions)

**Proposed:**
```cpp
void RenderSystem::OnUpdate(ECS::Scene& scene, const CameraComponent& camera,
                            Core::Assets::AssetManager& assetManager)
{
    BeginFrame();                           // GC, bindless flush, readbacks
    if (!m_Presentation.BeginFrame())       // WSI acquire
    {
        Interface::GUI::EndFrame();
        return;
    }
    UpdateGlobalResources(camera);          // UBO, view/proj matrices
    BuildRenderGraph(scene, assetManager);  // Add passes, compile, execute
    EndFrame();                             // Present
}
```

Each sub-function maps directly to existing contiguous blocks in the current implementation.

---

### Phase 5: Error Handling Unification (1 week, ongoing)

#### 5.1 Establish `INTRINSIC_VERIFY` Macro

Replace the pattern of `assert()` + `return error` with a single macro that is always active:

```cpp
// In Core.Error.cppm:

// Always-on verification. Logs and returns error in all builds.
#define INTRINSIC_VERIFY(condition, error_code)                       \
    do {                                                              \
        if (!(condition)) [[unlikely]] {                              \
            Core::Log::Error("Verification failed: {} ({}:{})",       \
                             #condition, __FILE__, __LINE__);         \
            return std::unexpected(error_code);                       \
        }                                                             \
    } while (0)

// Debug-only assertion. Crashes in debug, no-op in release.
#define INTRINSIC_ASSERT(condition)                                   \
    do {                                                              \
        if (!(condition)) [[unlikely]] {                              \
            Core::Log::Error("Assertion failed: {} ({}:{})",          \
                             #condition, __FILE__, __LINE__);         \
            assert(false);                                            \
        }                                                             \
    } while (0)
```

#### 5.2 Apply to `LinearArena` Thread Check

**Before (`Core.Memory.cpp`):**
```cpp
if (m_OwningThread != std::this_thread::get_id())
{
    assert(false && "LinearArena is not thread-safe...");
    return std::unexpected(AllocatorError::ThreadViolation);
}
```

**After:**
```cpp
INTRINSIC_VERIFY(m_OwningThread == std::this_thread::get_id(),
                 AllocatorError::ThreadViolation);
```

#### 5.3 Apply to `Hierarchy::Attach()`

**Before:**
```cpp
void Attach(entt::registry& registry, entt::entity child, entt::entity newParent)
{
    if (!registry.valid(child) || child == newParent) return;
    // ...
    if (Detail::IsDescendant(registry, child, newParent))
    {
        return;  // Silent failure
    }
```

**After:**
```cpp
Expected<void> Attach(entt::registry& registry, entt::entity child, entt::entity newParent)
{
    if (!registry.valid(child) || child == newParent)
        return std::unexpected(ErrorCode::InvalidArgument);

    if (Detail::IsDescendant(registry, child, newParent))
    {
        Log::Warn("Hierarchy::Attach — cycle detected: cannot attach entity {} to descendant {}",
                  static_cast<uint32_t>(child), static_cast<uint32_t>(newParent));
        return std::unexpected(ErrorCode::InvalidOperation);
    }
```

Add matrix validation after decomposition:
```cpp
    glm::mat4 invParent = glm::inverse(parentWorld.Matrix);
    glm::mat4 newLocalMat = invParent * childWorld.Matrix;

    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(newLocalMat, childLocal.Scale, childLocal.Rotation,
                   childLocal.Position, skew, perspective);

    // Validate decomposition result
    if (glm::any(glm::isnan(childLocal.Position)) ||
        glm::any(glm::isnan(glm::vec3(childLocal.Scale))) ||
        glm::any(glm::isnan(glm::vec4(childLocal.Rotation))))
    {
        Log::Warn("Hierarchy::Attach — matrix decomposition produced NaN (singular parent matrix?)");
        return std::unexpected(ErrorCode::InvalidState);
    }
```

#### 5.4 Add Logging to ECS System Skip-and-Continue Paths

**Before (`GPUSceneSync.cpp`):**
```cpp
if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
    continue;
```

**After:**
```cpp
if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
{
    Log::Debug("GPUSceneSync: Entity {} has no GPU slot, skipping",
               static_cast<uint32_t>(entity));
    continue;
}
```

Use `Log::Debug` (not `Warn`) for these — they're expected during initialization before the lifecycle system has allocated slots.

---

### Phase 6: AssetLease Memory Ordering Fix (< 1 day)

#### 6.1 Fix PinCount Ordering

**Before (`Core.Assets.cppm`):**
```cpp
void PinIfValid()
{
    if (m_Slot)
        m_Slot->PinCount.fetch_add(1, std::memory_order_relaxed);
}

void Reset()
{
    if (m_Slot)
    {
        m_Slot->PinCount.fetch_sub(1, std::memory_order_relaxed);
        m_Slot.reset();
    }
}
```

**After:**
```cpp
void PinIfValid()
{
    if (m_Slot)
        m_Slot->PinCount.fetch_add(1, std::memory_order_acquire);
}

void Reset()
{
    if (m_Slot)
    {
        m_Slot->PinCount.fetch_sub(1, std::memory_order_release);
        m_Slot.reset();
    }
}
```

**Rationale:** The `acquire` on pin ensures that subsequent reads of the resource pointer see any writes made by the thread that published the resource. The `release` on unpin ensures that the thread doing the reload sees all the accesses that happened before the unpin.

---

### Phase 7: AssetManager::Clear() Robustness (< 1 day)

**Before (`Core.Assets.cpp`):**
```cpp
void AssetManager::Clear()
{
    {
        std::unique_lock lock(m_Mutex);
        m_Lookup.clear();
        m_OneShotListeners.clear();
        m_PersistentListeners.clear();
    }
    m_Registry.clear();  // Destructors fire WITHOUT lock held
    {
        std::lock_guard qLock(m_EventQueueMutex);
        m_ReadyQueue.clear();
    }
}
```

**After:**
```cpp
void AssetManager::Clear()
{
    // 1. Snapshot entities to destroy under lock
    std::vector<entt::entity> toDestroy;
    {
        std::unique_lock lock(m_Mutex);
        m_Lookup.clear();
        m_OneShotListeners.clear();
        m_PersistentListeners.clear();

        auto view = m_Registry.view<AssetInfo>();
        toDestroy.reserve(view.size_hint());
        for (auto entity : view)
            toDestroy.push_back(entity);
    }

    // 2. Destroy entities without holding lock (destructors may call Unlisten)
    for (auto entity : toDestroy)
    {
        if (m_Registry.valid(entity))
            m_Registry.destroy(entity);
    }

    // 3. Final registry cleanup (should be empty now, but ensures consistency)
    m_Registry.clear();

    // 4. Drain event queue
    {
        std::lock_guard qLock(m_EventQueueMutex);
        m_ReadyQueue.clear();
    }
}
```

**Improvement:** Destructors now fire one-at-a-time with the lock available for re-acquisition. If a destructor calls `Unlisten()`, it can safely acquire `m_Mutex` without deadlocking (since we released it before destroying).

---

### Phase 8: Entity Destruction Hooks (< 1 day)

**File:** `src/Runtime/Runtime.Engine.cpp` (constructor or `InitPipeline()`)

**Add EnTT signal registration:**
```cpp
void Engine::InitPipeline()
{
    // ... existing pipeline setup ...

    // Register destruction hooks for immediate GPU slot cleanup
    m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
        .connect<&OnMeshRendererDestroyed>(*m_GpuScene);
}
```

**New free function:**
```cpp
void OnMeshRendererDestroyed(entt::registry& registry, entt::entity entity)
{
    auto& mr = registry.get<ECS::MeshRenderer::Component>(entity);
    if (mr.GpuSlot != ECS::MeshRenderer::Component::kInvalidSlot)
    {
        // Mark as inactive + free in GPUScene
        // The slot will be recycled after the current frame-in-flight completes
        // (GPUScene's internal deferred deletion handles this)
        m_GpuScene.DeactivateSlot(mr.GpuSlot);
        m_GpuScene.FreeSlot(mr.GpuSlot);
        mr.GpuSlot = ECS::MeshRenderer::Component::kInvalidSlot;
    }
}
```

**Remove the orphan-view sweep** from `MeshRendererLifecycle::OnUpdate()` (lines 106–124), as it's no longer needed.

---

### Phase 9: Deletion Queue Bounds (< 1 day)

**File:** `src/Runtime/RHI/RHI.Device.cpp`

**Add a high-water mark with a forced flush:**

```cpp
void VulkanDevice::SafeDestroyAfter(uint64_t value, std::function<void()>&& deleteFn)
{
    std::lock_guard lock(m_DeletionMutex);
    m_TimelineDeletionQueue.push_back(DeferredDelete{.Value = value, .Fn = std::move(deleteFn)});

    // Backpressure: if the queue grows excessively, force a GPU sync and flush.
    static constexpr size_t kMaxDeferredDeletions = 8192;
    if (m_TimelineDeletionQueue.size() > kMaxDeferredDeletions)
    {
        Log::Warn("Deletion queue exceeded {} entries, forcing GPU sync", kMaxDeferredDeletions);
        // Release lock, wait for GPU, re-acquire and collect
        lock.unlock();
        vkDeviceWaitIdle(m_LogicalDevice);
        CollectGarbage();
    }
}
```

---

### Phase 10: Magic Number Extraction (1–2 days, ongoing)

**File:** `src/Runtime/Graphics/Graphics.GPUScene.cppm` or a new constants file

```cpp
namespace Graphics::Constants
{
    // GPU Scene
    constexpr uint32_t kMaxGPUSceneInstances    = 100'000;
    constexpr uint32_t kGPUSceneMaxDescSets     = 64;
    constexpr uint32_t kGPUSceneStorageBindings  = kGPUSceneMaxDescSets * 3; // instance + bounds + indirect

    // Minimum SSBO size (Vulkan requires at least 4 bytes for buffer bindings)
    constexpr VkDeviceSize kMinSSBOSize = 4;

    // Compute dispatch
    constexpr uint32_t kCullWorkgroupSize = 64;

    // Hash constants
    constexpr uint32_t kGoldenRatio32 = 0x9e3779b9u;  // Knuth's multiplicative hash constant

    // Bounding sphere defaults
    constexpr float kDefaultBoundingSphereRadius = 10'000.0f;  // Conservative "always visible" fallback
    constexpr float kMinBoundingSphereRadius     = 1e-3f;      // Epsilon for degenerate geometry
}
```

---

## Summary: Effort Estimates by Phase

| Phase | Description | Effort | Risk | Dependencies |
|-------|-------------|--------|------|--------------|
| **1** | Critical data-race fixes | 1–2 days | Low | None |
| **2** | Engine decomposition | 1–2 weeks | Medium | Phase 1 |
| **3** | System scheduler | 3–5 days | Low | Phase 2 (step 1 only) |
| **4** | RenderGraph decomposition | 2–3 days | Low | Phase 1.4 |
| **5** | Error handling unification | 1 week (ongoing) | Low | None |
| **6** | AssetLease memory ordering | < 1 day | Low | None |
| **7** | AssetManager::Clear() fix | < 1 day | Low | None |
| **8** | Entity destruction hooks | < 1 day | Low | Phase 2 (step 2) |
| **9** | Deletion queue bounds | < 1 day | Low | Phase 1.1 |
| **10** | Magic number extraction | 1–2 days | None | None |

**Total estimated effort: ~4–5 weeks** (phases can overlap significantly).

**Recommended execution order:** Phases 1, 6, 7 first (all < 1 day, no dependencies). Then Phase 4 and 5 in parallel. Then Phase 2 incrementally. Phases 3, 8, 9, 10 can slot in opportunistically.

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
