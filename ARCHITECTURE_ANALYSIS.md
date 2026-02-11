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

### 1.1 Decompose the `Engine` God Object (Highest priority)

**Problem:** `Runtime::Engine` still aggregates too many responsibilities and exposes too much internal state, causing tight coupling (especially for Apps/Sandbox) and making isolated testing hard.

**Desired end-state:** `Engine` becomes a thin coordinator that owns a small number of subsystem objects with narrow interfaces.

**Proposed decomposition (incremental, one PR per subsystem):**

- `WindowSystem` (window creation, input, resize events)
- `GraphicsBackend` (Vulkan context/device/swapchain/descriptor plumbing)
- `AssetPipeline` (AssetManager + transfer + texture/material upload orchestration)
- `SceneManager` (ECS scene composition + lifetime)
- `RenderOrchestrator` (RenderSystem + RenderGraph integration)

**Tests to add/extend:**
- Construction/destruction order tests (especially GPU resource teardown vs. callbacks)
- "Headless Engine" smoke test that runs one frame with minimal subsystems

**Complexity:** Large (but can be made safe via mechanical, incremental extractions).

---

## 2. Prioritized Roadmap

### Tier A (Next)
1. **Engine decomposition** (start with `GraphicsBackend` or `AssetPipeline` extraction)

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
