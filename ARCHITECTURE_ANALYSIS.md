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

**Context:** `GraphicsBackend`, `AssetPipeline`, and `SceneManager` have been extracted (see Git history). The remaining render subsystem still lives inline in `Engine`.

**Remaining extractions (one PR per subsystem):**

- `RenderOrchestrator` (RenderSystem + RenderGraph + GPUScene + PipelineLibrary integration)

**Tests to add/extend:**
- "Headless Engine" smoke test that runs one frame with minimal subsystems

**Complexity:** Medium (mechanical extraction following the `GraphicsBackend` / `AssetPipeline` / `SceneManager` pattern).

---

## 2. Prioritized Roadmap

### Tier A (Next)
1. **`RenderOrchestrator` extraction** — bundles `m_RenderSystem`, `m_GpuScene`, `m_PipelineLibrary`, `m_ShaderRegistry`, `m_FrameGraph`, `InitPipeline()` into a subsystem.

### Tier B (After A)
2. **`Core::DAGScheduler` extraction** — factor the shared DAG scheduling algorithm (adjacency building from R/W hazards, Kahn's topological sort, parallel-layer execution) into a reusable `Core::DAGScheduler<NodeT, ResourceIdT>` template. FrameGraph and RenderGraph both instantiate it with their own node/resource types, eliminating duplicated graph logic while keeping execution semantics domain-specific. (See §4.3.)

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

### 4.3 FrameGraph vs RenderGraph: keep separate, share the algorithm

**Decision: Keep them separate.** They share the same 3-phase design (Setup → Compile → Execute) and Kahn's-algorithm topological sort, but operate on fundamentally different domains:

| | **FrameGraph** (Core) | **RenderGraph** (Graphics) |
|---|---|---|
| **Domain** | CPU-side ECS system scheduling | GPU-side render pass orchestration |
| **Dependencies on** | Component type tokens (compile-time) | Named GPU resources (images, buffers) |
| **Execution** | `Tasks::Scheduler` thread pool | Secondary command buffers + Vulkan barriers |
| **Barrier model** | Just ordering (layer waits) | `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` + layout transitions |
| **Lives in** | `Core/` (no GPU deps) | `Runtime/Graphics/` (Vulkan-specific) |
| **Testable without Vulkan** | Yes | No |

**Rationale:**

1. **Execution domains are genuinely different.** CPU work is "run this lambda on a thread pool." GPU work is "record into a secondary command buffer, compute Vulkan barriers, manage image layouts." Unifying means every node carries the union of both concerns.
2. **Dependency models differ.** FrameGraph tracks `Read<Transform>` / `Write<WorldMatrix>` via compile-time type identity. RenderGraph tracks `Read(depthBuffer, FRAGMENT_SHADER, SAMPLED_READ)` with Vulkan stage/access flags. A unified graph would need a polymorphic resource concept that doesn't simplify anything.
3. **Layering stays clean.** `Core::FrameGraph` has zero GPU dependencies, keeping `IntrinsicCoreTests` and `IntrinsicECSTests` Vulkan-free.
4. **Producer-consumer relationship is already simple.** Per frame: `FrameGraph.Execute()` → CPU state ready → `RenderGraph.Execute()` → GPU work submitted. One graph doesn't need to see the other's internal passes.

**TODO: Extract shared scheduling algorithm** into a reusable `Core::DAGScheduler<NodeT, ResourceIdT>` template:
- DAG adjacency building from Read/Write hazards (RAW, WAW, WAR)
- Kahn's algorithm topological sort into parallel layers
- Layer-wise execution with a barrier between layers

FrameGraph instantiates with type tokens; RenderGraph with GPU resource IDs. Scheduling is shared, execution and barrier semantics stay domain-specific.

**When to revisit:** If cross-domain dependencies arise (e.g., GPU compute pass must finish before a CPU system reads back results). That would typically be handled with fences/timeline semaphores at the boundary rather than merging graphs.
