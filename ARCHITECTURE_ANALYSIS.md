# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks **what's left to do** in IntrinsicEngine's architecture.

**Policy:** If something is fixed/refactored, it should **not** remain as an "issue" here. We rely on Git history for the past.

---

## 0. Scope & Success Criteria

**Goal:** a data-oriented, testable, modular engine architecture with:

- Deterministic per-frame orchestration (CPU + GPU), explicit dependencies.
- Robust multithreading contracts in Core/RHI.
- Minimal "god objects"; subsystems testable in isolation.

---

## 1. Open TODOs (What's left)

### 1.1 Continue `Engine` Decomposition

**Context:** `GraphicsBackend`, `AssetPipeline`, `SceneManager`, and `RenderOrchestrator` have been extracted (see Git history). The `Engine` class is now a thin shell that owns the four subsystems, a window, and a selection module.

**Remaining work:**
- "Headless Engine" smoke test that runs one frame with minimal subsystems

**Complexity:** Low.

---

### 1.2 Pre-existing Build Environment Issues

The following issues exist in this CI/development environment and should be tracked:

- **Clang 18 `__cpp_concepts` mismatch:** Clang 18 reports `__cpp_concepts` as `201907L` but GCC 14's `<expected>` header guards on `>= 202002L`. Workaround applied in `CMakeLists.txt` (`-D__cpp_concepts=202002L`). Will be unnecessary with Clang 19+.
- **C++20 module partition visibility:** Many `.cpp` module implementation partition units were missing explicit `import` statements for types used from sibling partitions. Clang 18 enforces strict module visibility. All known cases fixed; new ones may surface.
- **`DefaultPipeline` vtable linkage:** The `Graphics::DefaultPipeline` class (defined in `:Pipelines` partition, implemented in `:Pipelines.Impl`) has a linker vtable error. Root cause is likely clang-18's handling of vtables across module partition implementation units.

---

## 2. Prioritized Roadmap

### Tier A (Next)
_(No items — `Core::DAGScheduler` extraction is complete. See Git history.)_

### Tier B (Future)
1. **Port-based testing boundaries** — introduce type-erased "port" interfaces for filesystem, windowing, and time so that subsystems can be tested with fakes without Vulkan. (See §4.1.)
2. **`Core::InplaceFunction`** — small-buffer owning callable to replace remaining `std::function` in hot paths. (See §4.2.)

---

## 3. Non-goals (For this doc)

- Historical "fixed" issues (see Git history / PR descriptions).
- Implementation-level refactoring playbooks for already-landed fixes.

---

## 4. Open Questions (Callouts)

### 4.1 Subsystems: interfaces vs. concrete types

**Long-term answer:** **Concrete-by-default**, with dependency injection at construction and **interfaces/type-erased "ports" only at boundaries**.

- Use **concrete types** for hot-path subsystems (FrameGraph, scheduler/tasking, render graph execution, ECS update plumbing) to preserve inlining and keep inner loops vtable-free.
- Use **ports/adapters** (pure virtual or type-erased) for boundary dependencies: filesystem, file watching, OS window/surface creation, time, telemetry sinks, etc.

**Testing model:** instantiate concrete subsystems with fake ports (test doubles) rather than making the whole engine "virtual".

---

### 4.2 `std::function` in hot loops

**Long-term answer:** **Yes — ban `std::function` in hot loops.**

**Blessed alternatives (pick a small set and standardize):**
1. **Thunk + context pointer**: `{ void(*Fn)(void*), void* Ctx }` (or `const void*` as appropriate)
   - Zero allocations, fully predictable, ideal for FrameGraph nodes / scheduled tasks.
2. **Small-buffer owning callable** for non-trivial capture ownership without heap:
   - e.g. `Core::InplaceFunction<R(Args...), N>` where `N` is chosen by profiling (64B is a common starting point).
3. **Arena-backed closures** for per-frame graphs:
   - allocate capture payload out of `ScopeStack` / per-frame arena and store only thunk+ctx.

**Policy:** `std::function` is acceptable in cold paths (editor UI, startup config, tooling) but not in per-frame/per-entity loops.

---

### 4.3 FrameGraph vs RenderGraph: shared algorithm extracted

**Decision: Keep them separate.** They share the same 3-phase design (Setup → Compile → Execute) and Kahn's-algorithm topological sort, but operate on fundamentally different domains:

| | **FrameGraph** (Core) | **RenderGraph** (Graphics) |
|---|---|---|
| **Domain** | CPU-side ECS system scheduling | GPU-side render pass orchestration |
| **Dependencies on** | Component type tokens (compile-time) | Named GPU resources (images, buffers) |
| **Execution** | `Tasks::Scheduler` thread pool | Secondary command buffers + Vulkan barriers |
| **Barrier model** | Just ordering (layer waits) | `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` + layout transitions |
| **Lives in** | `Core/` (no GPU deps) | `Runtime/Graphics/` (Vulkan-specific) |
| **Testable without Vulkan** | Yes | No |

**Status: DONE.** The shared scheduling algorithm has been extracted into `Core::DAGScheduler`. Both FrameGraph and RenderGraph now delegate to it internally:

- **`Core::DAGScheduler`** (`Core.DAGScheduler.cppm/.cpp`) encapsulates:
  - Resource state tracking (last writer, current readers per resource key)
  - Automatic edge insertion from R/W hazards (RAW, WAW, WAR)
  - `DeclareWeakRead` for ordering-only dependencies (label `WaitFor` semantics)
  - Kahn's algorithm topological sort into parallel execution layers
  - Direct edge insertion and deduplication
- **FrameGraph** maps TypeTokens and labels (via MSB-tagged keys) to DAGScheduler resource keys.
- **RenderGraph** maps Vulkan resource IDs and access flags to DAGScheduler `DeclareRead/DeclareWrite` calls.
- Scheduling is shared; execution and barrier semantics remain domain-specific.
- 18 dedicated `DAGScheduler` tests cover all hazard types, edge cases, and multi-frame reuse.

**When to revisit:** If cross-domain dependencies arise (e.g., GPU compute pass must finish before a CPU system reads back results). That would typically be handled with fences/timeline semaphores at the boundary rather than merging graphs.
