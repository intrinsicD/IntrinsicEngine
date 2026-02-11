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

**Context:** `GraphicsBackend` has been extracted (see Git history). The remaining subsystems still live inline in `Engine`.

**Remaining extractions (one PR per subsystem):**

- `AssetPipeline` (AssetManager + transfer polling + texture/material upload orchestration + `PendingLoad` tracking + `RunOnMainThread` queue)
- `SceneManager` (ECS scene composition + lifetime + EnTT hooks)
- `RenderOrchestrator` (RenderSystem + RenderGraph + GPUScene + PipelineLibrary integration)

**Tests to add/extend:**
- "Headless Engine" smoke test that runs one frame with minimal subsystems

**Complexity:** Medium per subsystem (mechanical extractions following the `GraphicsBackend` pattern).

---

## 2. Prioritized Roadmap

### Tier A (Next)
1. **`AssetPipeline` extraction** — bundles `m_AssetManager`, `m_PendingLoads`, `m_LoadMutex`, `m_MainThreadQueue`, `m_MainThreadQueueMutex`, `m_LoadedMaterials`, `ProcessUploads()`, `ProcessMainThreadQueue()`, `RegisterAssetLoad()`, `RunOnMainThread()`, `LoadDroppedAsset()` into a coherent subsystem.
2. **`SceneManager` extraction** — bundles `m_Scene`, ECS hooks, entity lifetime into a subsystem.
3. **`RenderOrchestrator` extraction** — bundles `m_RenderSystem`, `m_GpuScene`, `m_PipelineLibrary`, `m_ShaderRegistry`, `m_FrameGraph`, `InitPipeline()` into a subsystem.

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
