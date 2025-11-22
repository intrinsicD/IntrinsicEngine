# Repository Review

## Overview
The Intrinsic Engine prototype provides a Vulkan-based rendering pipeline using C++23 modules, a simple ECS, and GLTF asset loading. The current codebase demonstrates core concepts (windowing, rendering, tasks, memory arenas) but several architectural and robustness gaps limit reliability and maintainability.

## Findings

### Core::Memory::LinearArena
* `std::aligned_alloc` requires the allocation size to be a multiple of the alignment, but `LinearArena` forwards the requested `sizeBytes` directly without rounding, invoking undefined behavior for non–cache-line-sized arenas (e.g., the 128-byte arena in tests). The constructor also ignores allocation failure and later dereferences `start_` unconditionally.【F:src/Core/Core.Memory.cpp†L13-L31】
* The allocator does not guard against overflow when computing `alignedPtr` and `padding`, which can wrap for large sizes because calculations occur on `uintptr_t` without bounds checks.【F:src/Core/Core.Memory.cpp†L33-L47】

### Core::Tasks::Scheduler
* `Dispatch` and `WaitForAll` assume `Initialize` has been called; invoking them beforehand will dereference `s_Ctx` and crash. There is no assertion or fallback path to protect callers.【F:src/Core/Core.Tasks.cpp†L39-L61】【F:src/Core/Core.Tasks.cpp†L63-L73】
* Shutdown only toggles `isRunning` and joins threads; it never drains pending work before returning. Tasks pushed immediately before `Shutdown` can be dropped silently because `isRunning` is cleared under the queue lock and worker threads exit once the queue becomes empty.【F:src/Core/Core.Tasks.cpp†L23-L37】【F:src/Core/Core.Tasks.cpp†L75-L117】
* Task submission and completion counters share a single global queue mutex, creating contention and limiting throughput. A lock-free or per-thread queue model would better align with the engine’s data-oriented goals.【F:src/Core/Core.Tasks.cpp†L13-L29】【F:src/Core/Core.Tasks.cpp†L78-L117】

### Runtime::Graphics::RenderSystem
* The global camera UBO is allocated with `new` and freed manually rather than via RAII, and mapping/unmapping every frame risks expensive driver synchronization; persistent mapping with smart-pointer ownership would be safer and faster.【F:src/Runtime/Graphics/Graphics.RenderSystem.cpp†L22-L53】【F:src/Runtime/Graphics/Graphics.RenderSystem.cpp†L58-L100】
* Descriptor updates for the camera buffer rely on external callers; if materials are created before the global UBO exists, bindings may dangle. Centralizing descriptor writes in the render system would reduce ordering hazards.【F:src/Runtime/Graphics/Graphics.RenderSystem.cpp†L68-L98】

### Runtime::Graphics::ModelLoader
* Attribute parsing assumes float components and contiguous buffer views; it never validates accessor types, component counts, or buffer bounds. Malformed or non-float GLTF files could cause out-of-bounds reads or incorrect vertex data.【F:src/Runtime/Graphics/Graphics.ModelLoader.cpp†L12-L98】
* Index decoding covers only unsigned byte/short/int but omits signed variants and stride handling; unsupported component types are silently skipped, producing incomplete meshes without surfacing errors to the caller.【F:src/Runtime/Graphics/Graphics.ModelLoader.cpp†L99-L139】

### Runtime::Engine lifecycle
* The engine never calls the user’s `OnRender` hook; rendering is hard-wired into `OnUpdate`, reducing flexibility for future render-graph work and making it easy to misorder simulation and rendering steps.【F:src/Runtime/Runtime.Engine.cpp†L69-L101】
* Shutdown waits for the device to become idle but does not handle swapchain recreation or window resize events, so a resize during execution likely invalidates the swapchain without recovery logic.【F:src/Runtime/Runtime.Engine.cpp†L7-L67】

## Recommendations
* Harden `LinearArena` by aligning allocation sizes, checking allocation failures, and guarding arithmetic overflow; expose a status so callers can detect construction errors.
* Add initialization guards (or throw) to the scheduler API, drain outstanding tasks during shutdown, and consider per-thread queues with work-stealing to reduce contention.
* Convert GPU resources to RAII types and persistently map the camera buffer; ensure descriptor writes happen once per material creation and are owned by the render system.
* Validate GLTF accessors (component type, stride, bounds) and propagate detailed errors; reject unsupported layouts explicitly.
* Invoke `OnRender` in the main loop, and introduce resize handling with swapchain recreation to keep Vulkan objects valid during window events.
