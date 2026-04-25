# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** harden the frame pipeline, close the remaining rendering architecture gaps (GPU-driven submission), advance the GPU compute backend, and wire all implemented backend features to the editor UI — without painting the engine into a corner for hybrid, transparency, or material-system work.

**Success criteria:**

- Deterministic frame construction with explicit pass/resource dependencies.
- Canonical render targets with validated producer/consumer contracts.
- Selection, post, and debug visualization decoupled from a single lighting path.
- Only recipe-required intermediate resources allocated per frame.
- Migration hardened by graph compile tests, contract tests, and at least one integration test.
- At least one shadow-casting light type with validated resource contracts.
- All implemented geometry operators reachable from the editor UI.
- Undo/redo wired to transform, inspector, and geometry operator actions.
- Lighting and camera properties editable from the View Settings panel.

---

## 1. Related Documents

- `docs/architecture/src_new-rendering-architecture.md` — target rendering architecture for the `src_new/Graphics` reimplementation (deferred-by-default, GPU-driven, BufferManager-managed geometry).
- `docs/architecture/src_new_module_inventory.md` — auto-generated inventory of every `src_new` module (regenerate via `tools/generate_src_new_module_inventory.py`).
- `docs/architecture/rendering-three-pass.md` — canonical legacy `src/` rendering architecture spec (pass contracts, data contracts, invariants).
- `docs/architecture/frame-loop-rollback-strategy.md` — concrete rollback toggle, shim, and pass/fail gates for legacy frame-loop migration phases.
- `docs/architecture/runtime-subsystem-boundaries.md` — legacy runtime ownership map, dependency directions, and startup/per-frame/shutdown lifecycle.
- `docs/architecture/post-merge-audit-checklist.md` — required stabilization gate for architecture-touching PRs (contracts, telemetry, graph ownership, config ownership, UI churn checks).
- `docs/architecture/adr-o2-pragmatic-medium-runtime-refactor.md` — ratified default runtime migration path (legacy tree).
- `PLAN.md` — archival index for the completed three-pass migration.
- `ROADMAP.md` — medium/long-horizon feature roadmap and phase ordering.
- `README.md` — user-facing architecture summary, build/test entry points, and SLOs.
- `CLAUDE.md` — contributor conventions, C++23 policy, `src_new` migration contract, and markdown sync contract.
- `PATTERNS.md` — reusable patterns catalog with canonical examples and usage guidance.
- `docs/architecture/gpu-driven-modular-rendering-pipeline-plan.md` — GPU-driven modular rendering pipeline plan (code-aware reuse + gap audit). Refines and implements C4 and C9; cross-references B1–B5.

---

## 1a. `src_new/` Reimplementation — Top-Level Milestones

The engine is being reimplemented in `src_new/` with stricter modular boundaries. Geometry is reused from `src/` as-is. See `CLAUDE.md` → "Active Effort: `src_new/` Reimplementation" for the migration contract, including the **module partitions — internal structure contract** (umbrella interface re-exporting one partition per internal concern, public vs. private partition split, README + module-inventory synchronization) that applies to every subsystem below.

- [ ] **Core parity.** Bring `src_new/Core` to feature parity with `src/Core`: memory (arena, scope, polymorphic, telemetry), tasks (scheduler, job, counter-event, local-task), filesystem, logging, config, handles, error types. Add focused tests under `CoreTestObjs`.
- [ ] **Assets parity.** Bring `src_new/Assets` (`Extrinsic.Asset.*`) to parity with `src/Asset`: registry, payload store, load pipeline with GPU fence waits, event bus, path index, read-phase protocol. Keep `Assets` dependent on `Core` only.
- [ ] **ECS parity.** Bring `src_new/ECS` to parity with `src/ECS`: scene registry, scene handles, components (Transform, Hierarchy, MetaData, CpuGeometry, RenderGeometry), systems (TransformHierarchy, RenderSync). No direct knowledge of `Graphics` internals.
- [ ] **Platform subsystem.** Implement `src_new/Platform` (`Extrinsic.Platform.IWindow`, `Extrinsic.Platform.Input`) plus the `LinuxGlfwVulkan` backend. Platform must be pluggable behind a port-style interface so headless tests can run without GLFW.
- [ ] **RHI + Vulkan backend.** Build out `src_new/Graphics/RHI` (`Device`, `CommandContext`, `BufferManager`, `TextureManager`, `SamplerManager`, `PipelineManager`, `Bindless`, `Transfer`, `Profiler`, `FrameHandle`) with `Backends/Vulkan` as the first implementation. Keep RHI free of scene/ECS knowledge.
- [ ] **Graphics renderer.** Implement `src_new/Graphics/Graphics.Renderer` following `docs/architecture/src_new-rendering-architecture.md` (GpuWorld, deferred-by-default, per-entity `Surface`/`Line`/`Point` component switches, managed geometry buffers, picking, render graph, default pipeline).
- [ ] **`GpuAssetCache` bridge.** Implement the Assets ↔ Graphics bridge (`CLAUDE.md` → "Assets ↔ Graphics boundary"): Graphics-owned side table keyed by `AssetId`, per-asset state machine (`NotRequested → CpuPending → GpuUploading → Ready` / `Failed`), synchronous `Request` + non-blocking `TryGet`, upload submission via `RHI::TransferManager` (no second staging queue), `AssetEventBus` subscription wired by `Runtime`, hot-reload atomic swap with old-view preservation, no GPU writeback into `AssetRegistry`. ECS components must store `AssetId` only.
- [ ] **Runtime composition root.** Implement `src_new/Runtime/Runtime.Engine` as the composition root: explicit subsystem instantiation order, `begin_frame → extract_render_world → prepare_frame → execute_frame → end_frame` lifecycle, deterministic shutdown.
- [ ] **Sandbox app.** Implement `src_new/App/Sandbox` as the reference integration target. Must build, launch, render a triangle, and exercise the asset load → render loop.
- [ ] **Tests.** Each `src_new` subsystem gets at least one focused test file (`Test_Extrinsic<Subsystem>_<Topic>.cpp`) in the matching `tests/CMakeLists.txt` OBJECT library.
- [ ] **Legacy retirement.** When a `src_new` subsystem reaches parity, remove its `src/` counterpart in a dedicated commit (do not leave dead code paths).

---

## 2. Next (P1) — Near-Term Priorities

P1 items are active development targets with concrete deliverables and test requirements.

### B. Frame Pipeline Hardening (O2 ADR Continuation)

Continue the staged frame-pipeline refactor from `docs/architecture/adr-o2-pragmatic-medium-runtime-refactor.md`. These items harden existing seams rather than adding new features.

O2 remains the default migration path unless future benchmark/test evidence overturns it.

**Architectural constraints (preserve during all B-section work):**
- Keep the three-pass/deferred/post/overlay behavior expressed as renderer-owned render-graph composition rather than top-level loop branching.
- Keep the main loop aware only of broad phases (platform, simulation, extraction, render, maintenance) — not pass-level detail.
- Preserve headless/testable paths by isolating platform and swapchain specifics from simulation, extraction, and maintenance logic.

**Dependency graph:**

```
B1 (Render Prep) ──→ B4a (Task Graph + Barriers)
B2 (Submission)  ──→ B5 (Queue Model + Lifetime)
B3 (Frame-Context Ownership) ──→ B4a (Task Graph + Barriers)
B4a ──→ B4b (Incremental Parallelization)
```

#### B1.0 Reference main loop (canonical — implemented in `src_new/Runtime/Runtime.Engine.cpp`)

The loop below is the authoritative implementation shape. It lives in
`Engine::RunFrame()`. **Do not change the phase ordering or add logic between
phases without updating this block and the Runtime README.**

```
// ════════════════════════════════════════════════════════════════════════
// THREE-GRAPH ARCHITECTURE — where each graph enters RunFrame
//
//  ① CPU Task Graph (fiber-based, work-stealing)
//       Drives Phase 2 (sim tick job-graph) and Phase 7 (render-prep jobs).
//       Phase 2 ends with a world.commit_tick() barrier — world is stable
//       before Phase 4 snapshots it.  Phase 7 ends with a cull-done barrier
//       before Phase 8 submits to the GPU.
//
//  ② GPU Frame Graph (transient DAG, Vulkan 1.3 Sync2)
//       Lives entirely inside Phase 8 (ExecuteFrame).
//       Declares virtual resources, resolves barriers and aliasing,
//       schedules async compute passes, records and submits command buffers.
//       RunFrame never touches a VkImage, VkBuffer, or pipeline barrier.
//
//  ③ Async Streaming Graph (background priority queues)
//       Runs continuously — no frame affinity.
//       Results surface at Phase 6 (GpuAssetCache::TryGet — non-blocking)
//       and are retired at Phase 10 (CollectCompletedTransfers advances the
//       NotRequested → CpuPending → GpuUploading → Ready state machine).
// ════════════════════════════════════════════════════════════════════════
```

```cpp
void Engine::RunFrame()
{
    // ── Phase 1: Platform ─────────────────────────────────────────────────
    // Pump OS events before measuring frame time so resize/close are visible
    // before we commit to a frame.

    m_Window->PollEvents();
    m_FrameClock.BeginFrame();

    if (m_Window->IsMinimized())
    {
        // Sleep until an OS event arrives, then resample so the sleep duration
        // does not inflate the next frame's fixed-step delta.
        m_Window->WaitForEventsTimeout(kIdleSleepSeconds);
        m_FrameClock.Resample();
        return;
    }

    // Swapchain resize: drain GPU, resize resources, acknowledge.
    if (m_Window->WasResized())
    {
        const auto extent = m_Window->GetFramebufferExtent();
        if (extent.Width > 0 && extent.Height > 0)
        {
            m_Device->WaitIdle();
            m_Device->Resize(extent.Width, extent.Height);
            m_Renderer->Resize(extent.Width, extent.Height);
        }
        m_Window->AcknowledgeResize();
    }

    // ── Phase 2: Fixed-step simulation  [① CPU Task Graph] ───────────────
    // The task graph schedules one job-graph per tick:
    //   input → sim → physics → anim → world.commit_tick()  ← barrier
    // world.commit_tick() is the phase boundary: world state is authoritative
    // and immutable from this point until the next Phase 2.
    // Clamp prevents spiral-of-death. MaxSubSteps caps catch-up work.

    m_Accumulator += m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);

    int substeps = 0;
    while (m_Accumulator >= m_FixedDt && substeps < m_MaxSubSteps)
    {
        m_Application->OnSimTick(*this, m_FixedDt);  // schedules & awaits one tick graph
        m_Accumulator -= m_FixedDt;
        ++substeps;
    }

    // alpha ∈ [0,1): interpolation blend between last committed tick and next.
    const double alpha = m_Accumulator / m_FixedDt;

    // ── Phase 3: Variable tick ────────────────────────────────────────────
    // Camera, UI, input processing — once per rendered frame, outside sim.

    m_Application->OnVariableTick(*this, alpha, m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta));

    // ── Phase 4: Build render snapshot ───────────────────────────────────
    // Immutable input to the renderer.  Reads the stable world committed in
    // Phase 2.  No mutable ECS/asset refs escape this struct.
    // Grows as WorldSnapshot, InputSnapshot, and CameraParams are wired in.

    const Graphics::RenderFrameInput renderInput{
        .Alpha    = alpha,
        .Viewport = m_Window->GetFramebufferExtent(),
    };

    // ── Phase 5: Renderer — BeginFrame ───────────────────────────────────
    // Acquire swapchain image, open command contexts.
    // Returns false → out-of-date / device-lost; skip this frame.

    RHI::FrameHandle frame{};
    if (!m_Renderer->BeginFrame(frame))
    {
        m_FrameClock.EndFrame();
        return;
    }

    // ── Phase 6: Renderer — ExtractRenderWorld  [③ Async Streaming] ──────
    // Snapshot committed world state into an immutable RenderWorld.
    // GpuAssetCache::TryGet(assetId) is called here — non-blocking.
    // Assets whose state machine has reached Ready supply a BufferView.
    // Assets still uploading (GpuUploading) return nullopt → draw skipped
    // or placeholder substituted this frame.
    // Engine must not touch ECS/asset state after this call.

    Graphics::RenderWorld renderWorld = m_Renderer->ExtractRenderWorld(renderInput);

    // ── Phase 7: Renderer — PrepareFrame  [① CPU Task Graph] ─────────────
    // Job-graph: frustum cull → draw-packet sort → LOD selection → staging.
    // Reads renderWorld (stable after Phase 6).  Barrier at end ensures all
    // prep jobs complete before Phase 8 submits to the GPU.
    // Future: explicit job-graph node replacing the sequential call (B1, B4a).

    m_Renderer->PrepareFrame(renderWorld);

    // ── Phase 8: Renderer — ExecuteFrame  [② GPU Frame Graph] ────────────
    // The GPU frame graph runs entirely here:
    //   declare virtual resources → compile DAG → alias transient memory
    //   → resolve Sync2 barriers → record passes in topological order:
    //       DepthPrepass / ShadowPass (async compute) / GBuffer
    //       / Lighting / Surface+Line+Point / PostProcess / Present
    //   → submit command buffers to graphics + async compute queues.
    // RunFrame never sees a VkImage, VkBuffer, or pipeline barrier.
    // Future: secondary command buffer recording per pass node (B4b).

    m_Renderer->ExecuteFrame(frame, renderWorld);

    // ── Phase 9: Renderer — EndFrame + Present ───────────────────────────
    // Release frame context back to the in-flight ring.
    // completedGpuValue = timeline value of the oldest frame now retired.

    const std::uint64_t completedGpuValue = m_Renderer->EndFrame(frame);
    m_Device->Present(frame);

    // ── Phase 10: Maintenance  [③ Async Streaming rendezvous] ────────────
    // CollectCompletedTransfers() advances the asset state machine:
    //   GpuUploading → Ready  (staging allocation retired, BufferView live)
    // completedGpuValue gates deferred deletion so GPU resources are only
    // reclaimed after the GPU has finished consuming them.
    // Future: m_MaintenanceService->CollectGarbage(completedGpuValue); (B6)

    m_Device->CollectCompletedTransfers();

    // ── Phase 11: Clock EndFrame ──────────────────────────────────────────
    m_FrameClock.EndFrame();   // records LastRawDelta() for telemetry
}
```

**Ownership map** (`src_new/` types):

| B1.0 concept | `src_new/` owner |
|---|---|
| `platform.pump_events` / minimize / resize | `Platform::IWindow` (`PollEvents`, `WaitForEventsTimeout`, `WasResized`) |
| `clock` | `Runtime::FrameClock` (`Runtime.FrameClock` module) |
| fixed-step sim loop | `IApplication::OnSimTick(engine, fixedDt)` — 0..N per frame |
| variable tick | `IApplication::OnVariableTick(engine, alpha, dt)` — once per frame |
| `RenderFrameInput` | `Graphics::RenderFrameInput` (`Graphics.RenderFrameInput` module) |
| `RenderWorld` | `Graphics::RenderWorld` (`Graphics.RenderWorld` module) |
| `renderer.*` | `Graphics::IRenderer` (5-phase interface in `Graphics.Renderer` module) |
| `resource_system.collect_garbage` | `IDevice::CollectCompletedTransfers()` now; `IMaintenanceService::CollectGarbage(completedGpuValue)` when B6 lands in `src_new/` |

**Extension points** (do not add inline logic — extend via the named seams):

- Phase 2 sim content → implement in `IApplication::OnSimTick`.
- Phase 3 variable content (camera, UI) → implement in `IApplication::OnVariableTick`.
- Phase 4 snapshot fields → expand `Graphics::RenderFrameInput` when `WorldSnapshot`/`CameraParams` land.
- Phase 7 cull parallelism → convert `PrepareFrame` to a job-graph node (B1, B4a).
- Phase 8 command parallelism → secondary command buffers inside `ExecuteFrame` (B4b).
- Phase 10 GC → inject `IMaintenanceService` into `Engine` and call `CollectGarbage(completedGpuValue)` (B6).

#### B1. Render Preparation as Job-Scheduled Work

Centralize CPU frustum culling into a render-preparation step that consumes immutable `RenderWorld` state and produces filtered draw lists consumed by passes. Structure draw packets so they are consumable by both CPU and GPU cull paths (forward reference: P2 C9).

- [ ] Extract `RenderDriver::PrepareFrame()` rendering preparation into a schedulable unit (job-graph node) rather than inline sequential call.
- [ ] Emit draw packets and scheduling metadata rather than immediate live-state callbacks.

#### B2. GPU Submission Stage Hardening

- [ ] Make the renderer follow the explicit-API rhythm: wait frame-context availability -> acquire -> reset per-frame allocators -> record -> submit -> present.
- [ ] Keep swapchain acquire/present and final submit on the main thread; push all other practical work to jobs.
- ~~Abstract queue submission behind a `QueueDomain` enum (Graphics/Compute/Transfer) in `RHI`.~~ Done: `RHI::QueueDomain` enum and `RHI::QueueSubmitter` in `RHI.QueueDomain` module. Migrate existing callers (SimpleRenderer, TransferManager) as a follow-up.
- [ ] Verify resize/out-of-date/minimized handling does not corrupt in-flight frame contexts. If gaps remain after the timeline-semaphore drain work, add robustness fixes here.
- [ ] Add integration test: submission rhythm is correct under swapchain resize.

#### B3. Frame-Context Transient Ownership

- [ ] Move per-frame transient ownership under `FrameContext`:
  - [ ] command allocator pools
  - [ ] upload arenas / staging allocators
  - [ ] descriptor arenas
  - [ ] per-frame render graph or graph-execution cache
- [ ] Audit existing systems and migrate any frame-temporary resource keyed by swapchain image count to frame-in-flight ownership unless image affinity is truly required.
- [ ] Add integration test: per-frame ownership is correctly isolated across frame-context ring slots.

#### B4. Job System + Multi-Threaded Command Recording

Split into two sub-phases:

**B4a — Task graph infrastructure + per-phase barriers (depends on B1, B3):**
- [ ] Treat the CPU task graph as the execution substrate for simulation, extraction, and render-prep jobs.
- [ ] Define per-phase barriers so extraction observes a stable world state and render prep observes a stable `RenderWorld`.
- [ ] Add integration test: phase barriers prevent render prep from observing in-flight world mutations.

**B4b — Incremental parallelization (depends on B4a):**
- [ ] Parallelize suitable workloads:
  - [ ] animation / transform propagation
  - [ ] broadphase / AI / script jobs
  - [ ] visibility / culling / LOD lists
  - [ ] draw-packet building / sorting
  - [ ] upload staging
- [ ] Keep the main thread as a conductor, not a worker.
- [ ] Multi-threaded command recording of heavy passes via secondary command buffers per `ExecutionPacket` layer. Use existing `Core::Tasks::Scheduler` with `CounterEvent` barrier. Shadow cascade rendering (A2) is the first candidate for multi-view parallel command recording.

#### B5. Queue Model, Synchronization, Frame Pacing, and Resource Lifetime

**Depends on B2** (timeline fences and resource retirement need the explicit submission rhythm in place first).

- [ ] Track queue domains conceptually as graphics / compute / transfer, even if the first implementation remains mostly single-queue.
- [ ] Promote GPU completion tracking to a first-class timeline/fence abstraction shared by upload retirement, deferred deletion, and readback readiness.
- [ ] Add timeline-based resource retirement instead of immediate GPU resource destruction from gameplay/editor code.
- [ ] Move swapchain acquire to after render-graph recording completes, gated by a feature toggle, and measure present-to-display latency delta.
- [ ] Frame-pacing modes deferred to a later item: editor-throttled, background-throttled, and uncapped modes are out of scope for now. Track remaining modes when the submission stage is stable.
- [ ] Add integration test: resource retirement correctly reclaims GPU resources after timeline completion.

#### B6. Maintenance Lane Completion — Complete

Audit confirmed the centralized maintenance lane (from commit `25d5bd2`) covers all B6 concerns:

- GPU garbage collection: timeline-based `CollectGarbage()`, transfer GC, texture/material deletions.
- GPU readback processing: `ProcessCompletedReadbacks()` handles pick readbacks after GPU completion.
- Profiler rollup: GPU profiler data in render lane's `BeginFrame()`; CPU telemetry (simulation stats, framegraph timings, task scheduler stats, memory budgets) in maintenance lane's `CaptureFrameTelemetry()`.
- Hot-reload bookkeeping: `BookkeepHotReloads()` covers shader hot-reload (the only implemented hot-reload type).
- Integration tests: `Test_MaintenanceLane.cpp` — 2 CPU-only coordinator contract tests (multi-frame call sequence verification, telemetry isolation) + 4 GPU integration tests (deferred deletion lifecycle, timeline ordering, real buffer retirement, multi-frame retirement cycle). GPU tests skip gracefully when no Vulkan ICD is available.

See git history for details.

### D. GPU Compute Backend — Phase 2

Independent parallel track with no ordering relationship to sections A or B. CUDA driver context is done (Phase 1). Phase 2 enables zero-copy data sharing between Vulkan rendering and CUDA compute.

#### D1. CUDA-Vulkan Interop

- [ ] Export Vulkan `VkBuffer` as POSIX fd / Win32 handle via `VK_KHR_external_memory`.
- [ ] Import into CUDA via `cuExternalMemoryGetMappedBuffer`.
- [ ] Export Vulkan timeline semaphore via `VK_KHR_external_semaphore`.
- [ ] Import into CUDA for cross-API synchronization.
- [ ] Add `RHI::CudaInterop` class encapsulating export/import lifecycle.
- [ ] Focused test: round-trip a buffer (Vulkan write -> CUDA read -> verify).
- [ ] Focused test: timeline semaphore signals correctly across APIs.

### E. Developer Workflow

Independent tracks that improve iteration speed and observability. Not rendering architecture gaps, but practical enablers for A-section and B-section work.

(E1 Shader Hot-Reload and E2 GPU Memory Budget Warning are complete; see git history.)

#### E3. Post-Review Correctness Hardening (2026-04-04 → 2026-04-05 commit audit)

Translate the latest commit-audit findings into concrete remediation work items with explicit test gates. This section is intentionally detailed so each risk can be closed with evidence instead of ad-hoc fixes.

**E3a — Vulkan error semantics policy (release vs debug parity): Complete.**

Four callsite-aware macros (`VK_CHECK_FATAL`, `VK_CHECK_RETURN`, `VK_CHECK_BOOL`, `VK_CHECK_WARN`) replace the old undifferentiated `VK_CHECK`. All 42 callsites across 8 source files audited and classified: 36 fatal (object creation, submit, command buffer lifecycle), 3 warn (timeline semaphore queries with safe zero-init defaults), 3 fatal (descriptor set layout creation). Eight unit tests including a GTest death test verify control-flow contracts. See git history for details.

**E3b — Queue-family safety and initialization contracts: Complete.**

`ValidateQueueFamilyContract()` boot-time gate in `VulkanDevice::CreateLogicalDevice()` validates: graphics family required, present family required when surface active, transfer family resolves via 3-level fallback (dedicated → any transfer → graphics). Safe accessors `Graphics()`, `Present()`, `Transfer()`, `HasDistinctTransfer()` on `QueueFamilyIndices` replace all 20+ raw `.value()` calls across 9 RHI source files and `Gui.cpp`. Each accessor has a debug assert guarding the precondition. Five CPU-only unit tests cover accessor correctness and `HasDistinctTransfer()` logic; one GPU integration test validates the headless device contract. See git history for details.

**E3d — Shader hot-reload process execution hardening: Complete.**

`Core::Process` module provides structured process spawn via `posix_spawnp` (argv-based, no shell interpolation) with stdout/stderr capture, configurable timeout + SIGTERM/SIGKILL cancellation, and `IsExecutableAvailable()` helper. All `std::system()` calls eliminated from the codebase (ShaderHotReload + ShaderCompiler). Compiler diagnostics (stdout/stderr) are surfaced via `Core::Log` (Error for stderr, Warn for stdout). Include-file dependency tracking scans `#include` directives at startup, watches include files, and cascades recompilation to all dependent shaders. Burst coalescing via 500ms max rebuild frequency cap on top of existing 200ms debounce window. 15 CPU-only unit tests cover spawn, capture, timeout, argument safety (spaces, quotes, dollar signs not shell-expanded), and glslc integration. See git history for details.

### F. UI Architecture & Feature Wiring

Wire implemented backend features to the editor UI and improve editor UX. Independent of rendering architecture work (A/B sections). High ROI: most items are pure UI additions over existing, tested backends.

**Dependency graph:**

```
F1 (Operator Wiring) — no deps, parallel with everything
F2 (Rendering Controls) — no deps
F3 (Undo Integration) ──→ F5 (Context Menus, use undo for delete/duplicate)
F4 (Hierarchy Tree) — no deps
F5 (Context Menus) — core complete; Paste deferred (needs clipboard system)
F6 (Editor Polish) — no deps, incremental
F7 (Render Target Viewer) — no hard deps; memory footprint sub-item soft-depends on E2
```

#### F1. Wire Remaining Geometry Operators to UI

All geometry operators (Shortest Path, Parameterization, Boolean CSG, Vector Heat Method, Mesh Quality Panel, Benchmark Runner) are now wired. **Complete.**

#### F2. Rendering Controls UI

Light environment serialization, camera property editor, global render mode override, and lighting path selector are all complete. **Complete.**

#### F3. Undo/Redo Integration

CommandHistory, Edit menu shortcuts, transform gizmo commands, inspector property commands, entity creation/deletion/duplication/rename/reparent commands, stack depth indicators, and geometry operator undo (simplify, remesh, smooth, subdivide, repair) are all complete. Mesh state is captured as `shared_ptr<Mesh>` snapshots before/after operator application; undo/redo restores the full mesh state including GPU re-upload. Memory note: each operator step stores two shared mesh snapshots (~5-15 MB per step for typical meshes). **Complete.**

#### F4. Hierarchy Panel Improvements

Multi-entity selection (Ctrl+click toggle, Shift+click range) is complete. The hierarchy panel supports parent-child tree rendering with drag-and-drop reparenting, expand/collapse all, and per-entity context menus. **Complete.**

#### F5. Viewport Context Menus

Right-click context menus are implemented in the 3D viewport (entity, empty-space, sub-element) with drag-threshold detection, undo support, and primitive spawning. Remaining:

- [ ] Paste (requires clipboard/copy-buffer system — deferred).

#### F6. Editor Polish

**Complete.** GPU memory usage display in the status bar (device-local VRAM summary with color-coded thresholds) and comprehensive tooltip coverage for all Inspector fields and remaining View Settings sliders (AA parameters, histogram, color grading, selection outline). See git history for details.

#### F7. Render Target Viewer Panel Enhancements

The Render Target Viewer panel is already implemented with frame state, recipe flags, pipeline feature state, selection outline / post-process internals, debug view toggles, resource lists, per-resource memory estimates, a visual resource lifetime timeline bar (green/blue bars with write-range overlay and per-pass column headers), and a zoomable texture preview with zoom-to-cursor, pan, and pixel coordinate tooltips. **Complete.**

## 3. Later (P2) — Planned Downstream Work

P2 items are design-only: plan the interfaces and constraints, do not implement. These should be **planned now** so the current refactor leaves room for them.

### C1. Material System Rewrite

- [ ] Define future material data-model requirements.
- [ ] Define `MaterialFeatureFlags` bitfield: Opaque, AlphaTest, Transparent, DoubleSided, Emissive.
- [ ] Define `ShadingModel` enum: DefaultLit, Unlit, FlatColor, Matcap, Toon.
- [ ] Derive `ShaderPermutationKey` from `(ShadingModel, FeatureFlags, LightingPath)`.
- [ ] Decide how materials map to:
  - [ ] Forward path.
  - [ ] Deferred path.
  - [ ] Hybrid path.
- [ ] Define canonical material parameter packing for rendering.
- [ ] Define CPU-side material representation vs. GPU packed representation.
- [ ] Plan material graph/node-system scope, or explicitly defer it.
- [ ] Plan shader permutation containment strategy.
- [ ] Plan texture binding model.
- [ ] Plan material debug visualization modes.

### C2. Transparency Architecture

- [ ] Plan the transparent rendering path.
- [ ] Decide whether transparency stays fully forward.
- [ ] Plan ordering/composition rules.
- [ ] Plan debug-view behavior for transparent objects.
- [ ] Plan interaction with selection outlines.

### C3. Lighting System Rewrite / Expansion

- [ ] Plan light data structures for forward+, deferred, and hybrid.
- [ ] Plan clustered/tiled light culling support.
- [ ] Plan shadow integration points (CSM foundation from P1/A2).
- [ ] Plan emissive/material/light interaction.
- [ ] Plan debug views for lights and shading terms.

### C4. Visibility System Improvements

- [ ] Plan depth-prepass integration with visibility and occlusion culling (prepass itself is P1/A1).
- [ ] Plan a visibility buffer or material-classification path if desired later.
- [ ] Plan occlusion-culling integration (HiZ generation + two-phase GPU culling).
- [ ] Plan GPU-driven pass compatibility with new graph stages.

### C5. Motion/Reprojection Support

- [ ] Add `PreviousViewProjectionMatrix` to `RenderViewPacket` (lightweight, can land early as foundation).
- [ ] Plan velocity-buffer support.
- [ ] Plan history-resource ownership.
- [ ] Plan TAA integration points.
- [ ] Plan motion-blur integration points.
- [ ] Plan camera/object motion contracts.

### C6. Decals / SSAO / Screen-Space Effects

- [ ] Plan resource requirements for SSAO (requires depth prepass from P1/A1).
- [ ] Plan normal/depth usage for screen-space effects.
- [ ] Plan decal insertion points in the graph.
- [ ] Plan debug output for effect intermediates.

### C7. PostProcessPass Factoring

- [ ] Factor `PostProcessPass` into sub-pass classes (tone mapping, bloom, AA) for independent testing and feature-gating.
- [ ] Each sub-pass becomes a standalone `IRenderFeature` registered in `DefaultPipeline`.

### C8. Render Asset / Shader System Cleanup

- [ ] Plan shader registration refactor.
- [ ] Plan shader hot-reload boundaries by pass/stage (builds on P1/E1 foundation).
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C9. GPU-Driven Indirect Rendering

GPU-driven surface culling is **already live**: `SurfacePass` Stage 3 dispatches `instance_cull_multigeo.comp` and consumes indirect draw commands via `vkCmdDrawIndexedIndirectCount`. Indirect draw buffer packing, the GPU cull pass consuming the GPUScene SSBO + camera frustum, the indirect-count draw path, and the `m_EnableGpuCulling` feature gate for Stage 2 vs Stage 3 are all in place. The remaining gap is extending GPU culling to Line/Point passes and centralizing the visibility authority. See `docs/architecture/gpu-driven-modular-rendering-pipeline-plan.md` for the full implementation plan (Phases A–D).

- [ ] Extract centralized `Graphics.Visibility` module from SurfacePass culling code.
- [ ] Extend GPU culling to Line/Point passes (requires GPUScene slots for non-surface entities).
- [ ] Benchmark: compare CPU-culled vs. GPU-culled draw submission at 10K+ entities.
- [ ] Plan multi-view extension (shadow cascade culling reuses the same path).

### C10. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.

### C11. GMM Spectral Framework (Mesh-Free Spectral Analysis)

Gaussian Mixture Model based spectral methods for point cloud analysis without requiring a mesh (inspired by Engine24's Galerkin Laplacian assembly on Gaussian mixtures).

- [ ] Implement EM fitting with regularized covariances in a `Geometry.GaussianMixture` module.
- [ ] Implement Galerkin Laplacian assembly: Octree spatial indexing → pair discovery → Gaussian product → sparse matrix assembly.
- [ ] Integrate with existing `Geometry.Octree` for spatial queries and `Geometry.DEC` sparse matrix types.
- [ ] Store per-point GMM membership weights via existing `PropertySet` system.
- [ ] Add spectral eigensolve support (requires sparse eigensolver — evaluate Spectra or implement shift-invert Lanczos).
- [ ] **Hierarchical EM (HEM):** Progressive Gaussian mixture reduction (Engine24 CUDA `hem.cuh`). BVH-accelerated KL-divergence nearest-pair merging for multi-resolution GMM pyramids. Enables LOD for point cloud spectral analysis. GPU path via Vulkan compute (after D1 or C14); CPU fallback with Octree KNN.

### C12. ICP Point Cloud Registration — Complete

Point-to-point (Besl & McKay 1992) and point-to-plane (Chen & Medioni 1992) ICP registration implemented in `Geometry.Registration` module. Uses KDTree for nearest-neighbor correspondence, SVD-based rigid alignment (point-to-point), and linearized 6-DOF normal equations (point-to-plane). Outlier rejection via distance threshold and percentile. Wired to editor UI with target entity selection, variant picker, and convergence diagnostics. 21 focused tests cover identity alignment, translation/rotation/rigid recovery, convergence, outlier rejection, and degenerate input. See git history for details.

### C13. GPU Compute Shader Normal Estimation

GPU-accelerated point cloud normal estimation for large point clouds (>1M points).

- [ ] Implement KNN query on GPU via compute shader (requires GPU-side spatial index or brute-force for small neighborhoods).
- [ ] Per-point covariance → eigendecomposition → normal extraction in compute shader.
- [ ] Integrate with existing `PointCloudLifecycleSystem` for on-upload normal computation.
- [ ] Benchmark against CPU `Geometry.NormalEstimation` at varying point counts.

### C14. GPU LBVH Construction (Vulkan Compute)

Port the Morton-code Linear BVH construction algorithm (Karras 2012, implemented in Engine24 CUDA and torch-mesh-isect_fork) to Vulkan compute shaders. Enables GPU-driven broadphase for culling, picking, and collision without CUDA dependency.

- [ ] Implement 30-bit 3D Morton code encoding via bit-expansion in a compute shader (`morton_encode.comp`).
- [ ] GPU radix sort of Morton codes (evaluate `VkSortKHR` or implement a simple GPU radix sort).
- [ ] Karras internal-node construction: one thread per internal node, longest common prefix via `findMSB(key1 ^ key2)`.
- [ ] Bottom-up AABB propagation with atomic parent visit counters.
- [ ] Stack-based BVH traversal kernel for range and nearest-neighbor queries.
- [ ] Adopt the `rightmost` node pointer trick (from torch-mesh-isect) for self-intersection deduplication — skip subtrees whose rightmost leaf ≤ query leaf.
- [ ] Integrate as an alternative to CPU `Geometry.BVH` for entities exceeding a triangle-count threshold.
- [ ] Benchmark: GPU LBVH build + query vs CPU BVH at 100K+ triangles.

### C15. Point Cloud Robustness Operators — Complete

Bilateral filter (Fleishman et al. 2003), outlier probability estimation (LOF-inspired, Breunig et al. 2000), and kernel density estimation (Gaussian KDE with Silverman's rule) implemented in `Geometry.PointCloudUtils`. All three operators follow the standard Params/Result/optional pattern, publish per-point properties (`p:outlier_score`, `p:density`), and are wired to the editor UI as point cloud geometry operators. 24 focused tests cover degenerate inputs, noise reduction, outlier detection, density discrimination, property publication, and convergence. See git history for details.

### C16. Non-Rigid Point Cloud Registration (CPD)

Coherent Point Drift (Myronenko & Song 2010) for deformable point cloud alignment. Extends C12's rigid ICP with probabilistic non-rigid correspondence.

- [ ] Implement `Geometry.Registration` CPD variant: treat one point set as GMM centroids, fit to the other via EM.
- [ ] Rigid CPD (rotation + translation) as a robust alternative to ICP under noise/outliers.
- [ ] Affine CPD extension for scale + shear recovery.
- [ ] Non-rigid CPD with motion coherence regularization (Gaussian kernel smoothing of displacement field).
- [ ] Convergence diagnostics in Result struct: sigma², negative log-likelihood, iteration count.
- [ ] Wire to editor UI alongside ICP (C12) in a unified Registration panel.

### C17. Heat Kernel Graph Laplacian

Extend the existing DEC module with adaptive edge weighting variants from Engine24's `GraphLaplacianOperator`.

Heat kernel weights are complete (see git history). Remaining:

- [ ] **GMM-weighted Laplacian:** Mahalanobis-distance edge weights from per-vertex covariance matrices. Enables anisotropy-aware spectral analysis. Requires per-vertex covariance computation from local neighborhoods. Deferred — depends on GMM infrastructure from C11.

### C18. SPIR-V Shader Reflection for Automatic Pipeline Layout

Eliminate manual descriptor set layout specification by reflecting SPIR-V bytecode at pipeline creation time (pattern from RD_Engine's `ShaderReflector`).

- [ ] Integrate SPIRV-Reflect (or spirv-cross) as a build dependency.
- [ ] Implement `RHI::ReflectShaderLayout()`: given a set of SPIR-V modules, extract descriptor set layouts (set/binding/type/stage) and push constant ranges.
- [ ] Use reflected layout as the **default** in `PipelineBuilder` when no explicit layout is provided. Explicit layout overrides reflection (preserving current manual control for specialized passes).
- [ ] Cache reflected layouts by shader content hash to avoid redundant reflection.
- [ ] Validate: reflected layout matches hand-authored layout for existing pipelines (regression test).

### C19. Automatic Differentiation for Geometry Optimization

Integrate a lightweight C++ automatic differentiation library for gradient-based geometry processing without hand-derived Jacobians. Engine23 used TinyAD; evaluate modern alternatives.

- [ ] Evaluate AD libraries: TinyAD (geometry-focused, Eigen-based), CppAD, or a minimal forward-mode AD header.
- [ ] Integrate as an optional dependency (conditional CMake, like CUDA).
- [ ] First application: ARAP (As-Rigid-As-Possible) mesh deformation — local rotation fitting + global position solve with AD-computed energy gradients.
- [ ] Second application: improve LSCM parameterization (C1/F1) with AD-based conformal energy minimization.
- [ ] Benchmark AD-based vs hand-derived gradient for cotan Laplacian assembly to verify acceptable overhead.

### C20. Mesh Self-Intersection Detection

Fast mesh self-intersection detection using BVH-accelerated triangle-triangle overlap testing (from torch-mesh-isect_fork).

- [ ] Implement 17-axis SAT for triangle-triangle intersection (2 face normals + 9 edge cross products + 6 normal-cross-edge). More robust than Moller-Trumbore for coplanar/near-coplanar cases.
- [ ] CPU path: use existing `Geometry.BVH` for broadphase, 17-axis SAT for narrowphase. Report intersecting triangle pairs.
- [ ] GPU path (after C14 lands): use GPU LBVH with `rightmost` dedup for large meshes.
- [ ] Publish `f:self_intersecting` boolean property and visualize via face highlight overlay.
- [ ] Wire to Mesh Analysis panel (extends existing defect-marker UI).

### D

Below is a **Codex-ready TODO list**. It is intentionally ordered so each phase can compile before moving to the next.

I verified the current repo state: `src_new/Graphics` already documents the target direction as BufferManager-managed geometry, GPU-driven execution, deferred rendering, component-presence routing, and visualization as a separate concern. The architecture doc also explicitly wants a `GpuWorld` ownership boundary with `GlobalGeometryStore`, instance/config/bounds/material buffers, and BVH/culling buffers rather than one giant monolithic buffer. ([GitHub][1])

One important implementation correction: the current RHI comments say `BindVertexBuffer` / `BindIndexBuffer` are intentionally absent, while the RHI already exposes `DrawIndexedIndirectCount`. Standard Vulkan indexed indirect drawing uses a bound index buffer, and Vulkan’s reference page defines `vkCmdBindIndexBuffer` as the API that binds the index buffer range; `vkCmdDrawIndexedIndirectCount` is the indexed indirect-count draw command. ([GitHub][2])

---

# Codex TODOs: GPU-driven renderer implementation for `src_new/Graphics`

## Non-negotiable design constraints

Implement the renderer with these invariants:

1. **One `GpuWorld` owns GPU scene state.**
2. **One renderable entity has one stable `InstanceSlot`.**
3. `InstanceSlot` indexes parallel GPU arrays:

  * `GpuInstanceStatic[]`
  * `GpuInstanceDynamic[]`
  * `GpuEntityConfig[]`
  * `GpuBounds[]`
4. `GpuInstanceStatic.GeometrySlot` points into `GpuGeometryRecord[]`.
5. `GpuInstanceStatic.MaterialSlot` points into `GpuMaterialSlot[]`.
6. Geometry data lives in large managed GPU buffers, never one vertex/index buffer per entity.
7. Materials do **not** store per-entity scalar/color/normal BDA pointers. Those belong in `GpuEntityConfig`.
8. GPU culling writes indirect draw commands. Draw calls use `firstInstance = InstanceSlot`.
9. No per-entity descriptor sets.
10. Do **not** implement LBVH first. Start with linear GPU frustum culling, then leave LBVH as a later phase.

The repo architecture already says geometry should not be per-entity vertex/index buffers and that per-entity parameters should live in instance/config SSBOs with no per-entity descriptor sets. ([GitHub][3])

---

## Phase 1 — Fix RHI command API for real GPU-driven drawing — **Complete**

### Files

* `src_new/Graphics/RHI/RHI.Types.cppm`
* `src_new/Graphics/RHI/RHI.CommandContext.cppm`
* `src_new/Graphics/Backends/Null/Backends.Null.cpp`
* `src_new/Graphics/Backends/Vulkan/Backends.Vulkan.Internal.cppm`
* `src_new/Graphics/Backends/Vulkan/Backends.Vulkan.CommandContext.cpp`
* `src_new/Graphics/Backends/Vulkan/Backends.Vulkan.Mappings.cpp`

### TODO 1.1 — Add index type enum — **Done**

In `RHI.Types.cppm` or `RHI.CommandContext.cppm`, add:

```cpp
export enum class IndexType : std::uint8_t {
    Uint16,
    Uint32,
};
```

Add a Vulkan mapping:

```cpp
VkIndexType ToVkIndexType(RHI::IndexType t);
```

Expected mapping:

```cpp
Uint16 -> VK_INDEX_TYPE_UINT16
Uint32 -> VK_INDEX_TYPE_UINT32
```

### TODO 1.2 — Add index-buffer binding to `ICommandContext` — **Done**

In `RHI.CommandContext.cppm`, add:

```cpp
virtual void BindIndexBuffer(
    BufferHandle buffer,
    std::uint64_t offset,
    IndexType indexType) = 0;
```

Place it near `BindPipeline`.

Do **not** add `BindVertexBuffer`. Vertex fetch remains BDA/manual in shaders.

### TODO 1.3 — Add non-indexed indirect-count draw — **Done**

In `RHI.CommandContext.cppm`, add:

```cpp
virtual void DrawIndirectCount(
    BufferHandle argBuffer,
    std::uint64_t argOffset,
    BufferHandle countBuffer,
    std::uint64_t countOffset,
    std::uint32_t maxDrawCount) = 0;
```

This is needed for point rendering. Vulkan has `vkCmdDrawIndirectCount` for non-indexed indirect-count draws. ([docs.vulkan.org][4])

### TODO 1.4 — Implement Null backend no-ops — **Done**

In `Backends.Null.cpp`, add no-op overrides:

```cpp
void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
void DrawIndirectCount(
    RHI::BufferHandle,
    std::uint64_t,
    RHI::BufferHandle,
    std::uint64_t,
    std::uint32_t) override {}
```

### TODO 1.5 — Implement Vulkan backend methods — **Done**

In `Backends.Vulkan.Internal.cppm`, add declarations to `VulkanCommandContext`.

In `Backends.Vulkan.CommandContext.cpp`, implement:

```cpp
void VulkanCommandContext::BindIndexBuffer(
    RHI::BufferHandle handle,
    std::uint64_t offset,
    RHI::IndexType indexType)
{
    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf) return;

    vkCmdBindIndexBuffer(
        m_Cmd,
        buf->Buffer,
        offset,
        ToVkIndexType(indexType));
}
```

And:

```cpp
void VulkanCommandContext::DrawIndirectCount(
    RHI::BufferHandle argBuf,
    std::uint64_t argOffset,
    RHI::BufferHandle cntBuf,
    std::uint64_t cntOffset,
    std::uint32_t maxDraw)
{
    const auto* abuf = m_Buffers->GetIfValid(argBuf);
    const auto* cbuf = m_Buffers->GetIfValid(cntBuf);
    if (!abuf || !cbuf) return;

    vkCmdDrawIndirectCount(
        m_Cmd,
        abuf->Buffer,
        argOffset,
        cbuf->Buffer,
        cntOffset,
        maxDraw,
        sizeof(VkDrawIndirectCommand));
}
```

### TODO 1.6 — Add GPU-side buffer fill command — **Done**

Add this to `ICommandContext`:

```cpp
virtual void FillBuffer(
    BufferHandle buffer,
    std::uint64_t offset,
    std::uint64_t size,
    std::uint32_t value) = 0;
```

Null backend: no-op.

Vulkan backend:

```cpp
void VulkanCommandContext::FillBuffer(
    RHI::BufferHandle handle,
    std::uint64_t offset,
    std::uint64_t size,
    std::uint32_t value)
{
    const auto* buf = m_Buffers->GetIfValid(handle);
    if (!buf) return;
    vkCmdFillBuffer(m_Cmd, buf->Buffer, offset, size, value);
}
```

Use this later to reset indirect draw counters on the GPU.

### Acceptance

* Project compiles.
* Null backend compiles.
* Vulkan backend compiles.
* `ICommandContext` exposes:

  * `BindIndexBuffer`
  * `DrawIndexedIndirectCount`
  * `DrawIndirectCount`
  * `FillBuffer`

---

## Phase 2 — Replace old GPU structs with renderer-grade layouts — **Complete**

### File

* `src_new/Graphics/RHI/RHI.Types.cppm`

The current `GpuInstanceData` only contains model matrix, material slot, entity id, geometry id, and flags. It is not enough for a true GPU-driven renderer. ([GitHub][5])

### TODO 2.1 — Add render flag bits — **Done**

Add:

```cpp
export enum GpuRenderFlags : std::uint32_t {
    GpuRender_None        = 0,
    GpuRender_Surface     = 1u << 0,
    GpuRender_Line        = 1u << 1,
    GpuRender_Point       = 1u << 2,
    GpuRender_CastShadow  = 1u << 3,
    GpuRender_Opaque      = 1u << 4,
    GpuRender_AlphaMask   = 1u << 5,
    GpuRender_Transparent = 1u << 6,
    GpuRender_Unlit       = 1u << 7,
    GpuRender_FlatShading = 1u << 8,
    GpuRender_Visible     = 1u << 31,
};
```

### TODO 2.2 — Add draw bucket enum — **Done**

Add:

```cpp
export enum class GpuDrawBucketKind : std::uint32_t {
    SurfaceOpaque = 0,
    SurfaceAlphaMask,
    Lines,
    Points,
    ShadowOpaque,
    Count
};
```

### TODO 2.3 — Add geometry record — **Done**

Add:

```cpp
export struct alignas(16) GpuGeometryRecord {
    std::uint64_t VertexBufferBDA = 0;   // packed position/uv buffer base
    std::uint64_t IndexBufferBDA  = 0;   // optional/debug/manual fetch path

    std::uint32_t VertexOffset = 0;
    std::uint32_t VertexCount  = 0;

    std::uint32_t SurfaceFirstIndex = 0;
    std::uint32_t SurfaceIndexCount = 0;

    std::uint32_t LineFirstIndex = 0;
    std::uint32_t LineIndexCount = 0;

    std::uint32_t PointFirstVertex = 0;
    std::uint32_t PointVertexCount = 0;

    std::uint32_t BufferID = 0;
    std::uint32_t Flags = 0;
};
static_assert(sizeof(GpuGeometryRecord) == 64);
```

### TODO 2.4 — Add split instance data — **Done**

Add:

```cpp
export struct alignas(16) GpuInstanceStatic {
    std::uint32_t GeometrySlot = 0;
    std::uint32_t MaterialSlot = 0;
    std::uint32_t EntityID     = 0;
    std::uint32_t RenderFlags  = 0;

    std::uint32_t VisibilityMask = 0xFFFF'FFFFu;
    std::uint32_t Layer          = 0;
    std::uint32_t ConfigSlot     = 0;
    std::uint32_t _pad0          = 0;
};
static_assert(sizeof(GpuInstanceStatic) == 32);

export struct alignas(16) GpuInstanceDynamic {
    alignas(16) glm::mat4 Model{1.f};
    alignas(16) glm::mat4 PrevModel{1.f};
};
static_assert(sizeof(GpuInstanceDynamic) == 128);
```

### TODO 2.5 — Add entity config — **Done**

Add:

```cpp
export struct alignas(16) GpuEntityConfig {
    std::uint64_t VertexNormalBDA = 0;
    std::uint64_t ScalarBDA       = 0;
    std::uint64_t ColorBDA        = 0;
    std::uint64_t PointSizeBDA    = 0;

    float ScalarRangeMin = 0.f;
    float ScalarRangeMax = 1.f;
    std::uint32_t ColormapID = 0;
    std::uint32_t BinCount = 0;

    float IsolineCount = 0.f;
    float IsolineWidth = 0.f;
    float VisualizationAlpha = 1.f;
    std::uint32_t VisDomain = 0;       // 0 vertex, 1 face, 2 edge

    alignas(16) glm::vec4 IsolineColor{0.f, 0.f, 0.f, 1.f};

    float PointSize = 1.f;
    std::uint32_t PointMode = 0;
    std::uint32_t ColorSourceMode = 0; // 0 material, 1 uniform, 2 scalar, 3 rgba buffer
    std::uint32_t ElementCount = 0;

    alignas(16) glm::vec4 UniformColor{1.f};
};
static_assert(sizeof(GpuEntityConfig) == 128);
```

### TODO 2.6 — Add bounds buffer struct — **Done**

Add:

```cpp
export struct alignas(16) GpuBounds {
    alignas(16) glm::vec4 LocalSphere{0.f}; // xyz center, w radius
    alignas(16) glm::vec4 WorldSphere{0.f};
    alignas(16) glm::vec4 WorldAabbMin{0.f};
    alignas(16) glm::vec4 WorldAabbMax{0.f};
};
static_assert(sizeof(GpuBounds) == 64);
```

### TODO 2.7 — Add light struct — **Done**

Add:

```cpp
export struct alignas(16) GpuLight {
    alignas(16) glm::vec4 Position_Range{0.f};
    alignas(16) glm::vec4 Direction_Type{0.f};
    alignas(16) glm::vec4 Color_Intensity{1.f};
    alignas(16) glm::vec4 Params{0.f}; // spot angles, shadow index, flags
};
static_assert(sizeof(GpuLight) == 64);
```

### TODO 2.8 — Add GPU scene root table — **Done**

Because the current backend is BDA-heavy and only binds a global bindless descriptor set, avoid adding a full descriptor-set system right now. Add a root table buffer:

```cpp
export struct alignas(16) GpuSceneTable {
    std::uint64_t InstanceStaticBDA  = 0;
    std::uint64_t InstanceDynamicBDA = 0;
    std::uint64_t EntityConfigBDA    = 0;
    std::uint64_t GeometryRecordBDA  = 0;

    std::uint64_t BoundsBDA          = 0;
    std::uint64_t MaterialBDA        = 0;
    std::uint64_t LightBDA           = 0;
    std::uint64_t _padBDA            = 0;

    std::uint32_t InstanceCapacity   = 0;
    std::uint32_t GeometryCapacity   = 0;
    std::uint32_t MaterialCapacity   = 0;
    std::uint32_t LightCount         = 0;
};
static_assert(sizeof(GpuSceneTable) == 80);
```

### TODO 2.9 — Add pass push constants — **Done**

Add:

```cpp
export struct alignas(16) GpuScenePushConstants {
    std::uint64_t SceneTableBDA = 0;
    std::uint32_t FrameIndex = 0;
    std::uint32_t DrawBucket = 0;
    std::uint32_t DebugMode = 0;
    std::uint32_t _pad0 = 0;
};
static_assert(sizeof(GpuScenePushConstants) <= 128);
```

### Acceptance

* All structs are plain data.
* All structs have `static_assert(sizeof(...))`.
* Old `GpuInstanceData` may remain temporarily for compatibility, but new systems must use the new structs.

---

## Phase 3 — Create `Graphics.GpuWorld`

### Files to add

* `src_new/Graphics/Graphics.GpuWorld.cppm`
* `src_new/Graphics/Graphics.GpuWorld.cpp`

### Files to edit

* `src_new/Graphics/CMakeLists.txt`
* `src_new/Graphics/Graphics.Renderer.cppm`
* `src_new/Graphics/Graphics.Renderer.cpp`

The architecture doc already names `GpuWorld` as the GPU-side counterpart of the CPU scene and says it should own managed geometry, instance buffer, entity config buffer, bounds, material registry, and BVH/culling buffers. ([GitHub][3])

### TODO 3.1 — **Done**

 Add module to CMake

In `src_new/Graphics/CMakeLists.txt`, add:

```cmake
Graphics.GpuWorld.cppm
```

to the public module files and:

```cmake
Graphics.GpuWorld.cpp
```

to private sources.

### TODO 3.2 — **Done**

 Add public `GpuWorld` interface

In `Graphics.GpuWorld.cppm`, export:

```cpp
export module Extrinsic.Graphics.GpuWorld;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Graphics {

struct GpuInstanceTag;
struct GpuGeometryTag;

using GpuInstanceHandle = Core::StrongHandle<GpuInstanceTag>;
using GpuGeometryHandle = Core::StrongHandle<GpuGeometryTag>;

class GpuWorld {
public:
    struct InitDesc {
        std::uint32_t MaxInstances = 100'000;
        std::uint32_t MaxGeometryRecords = 65'536;
        std::uint32_t MaxLights = 4096;
        std::uint64_t VertexBufferBytes = 256ull * 1024ull * 1024ull;
        std::uint64_t IndexBufferBytes  = 512ull * 1024ull * 1024ull;
    };

    struct GeometryUploadDesc {
        std::span<const std::byte> PackedVertexBytes;
        std::span<const std::uint32_t> SurfaceIndices;
        std::span<const std::uint32_t> LineIndices;
        std::uint32_t VertexCount = 0;
        RHI::GpuBounds LocalBounds{};
        const char* DebugName = nullptr;
    };

    GpuWorld();
    ~GpuWorld();

    GpuWorld(const GpuWorld&) = delete;
    GpuWorld& operator=(const GpuWorld&) = delete;

    bool Initialize(RHI::IDevice& device, RHI::BufferManager& buffers, const InitDesc& desc = {});
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] GpuInstanceHandle AllocateInstance(std::uint32_t entityId);
    void FreeInstance(GpuInstanceHandle instance);

    [[nodiscard]] GpuGeometryHandle UploadGeometry(const GeometryUploadDesc& desc);
    void FreeGeometry(GpuGeometryHandle geometry);

    void SetInstanceGeometry(GpuInstanceHandle instance, GpuGeometryHandle geometry);
    void SetInstanceMaterialSlot(GpuInstanceHandle instance, std::uint32_t materialSlot);
    void SetInstanceRenderFlags(GpuInstanceHandle instance, std::uint32_t flags);
    void SetInstanceTransform(GpuInstanceHandle instance, const glm::mat4& model, const glm::mat4& prevModel);
    void SetEntityConfig(GpuInstanceHandle instance, const RHI::GpuEntityConfig& config);
    void SetBounds(GpuInstanceHandle instance, const RHI::GpuBounds& bounds);

    void SetMaterialBuffer(RHI::BufferHandle materialBuffer, std::uint32_t materialCapacity);
    void SetLights(std::span<const RHI::GpuLight> lights);

    void SyncFrame();

    [[nodiscard]] RHI::BufferHandle GetSceneTableBuffer() const noexcept;
    [[nodiscard]] std::uint64_t GetSceneTableBDA() const noexcept;

    [[nodiscard]] RHI::BufferHandle GetInstanceStaticBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetInstanceDynamicBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetEntityConfigBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetGeometryRecordBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetBoundsBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetLightBuffer() const noexcept;

    [[nodiscard]] RHI::BufferHandle GetManagedVertexBuffer() const noexcept;
    [[nodiscard]] RHI::BufferHandle GetManagedIndexBuffer() const noexcept;

    [[nodiscard]] std::uint32_t GetLiveInstanceCount() const noexcept;
    [[nodiscard]] std::uint32_t GetInstanceCapacity() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
```

### TODO 3.3 — **Done**

 Implement fixed-size slot allocators

In `Graphics.GpuWorld.cpp`, implement a small internal slot allocator:

```cpp
struct SlotMeta {
    std::uint32_t Generation = 0;
    bool Live = false;
};

struct SlotAllocator {
    std::vector<SlotMeta> Meta;
    std::vector<std::uint32_t> FreeList;
    std::uint32_t NextFresh = 0;
    std::uint32_t LiveCount = 0;

    Core::StrongHandle<T> Allocate();
    bool Resolve(Core::StrongHandle<T> h) const;
    void Free(Core::StrongHandle<T> h);
};
```

Use one allocator for instances and one for geometry records.

### TODO 3.4 — **Done**

 Allocate persistent GPU buffers

In `GpuWorld::Initialize`, allocate these buffers through `RHI::BufferManager`:

| Buffer                          | Type                   | Usage    |              |              |
| ------------------------------- | ---------------------- | -------- | ------------ | ------------ |
| `GpuWorld.InstanceStatic`       | `GpuInstanceStatic[]`  | `Storage | TransferDst` |              |
| `GpuWorld.InstanceDynamic`      | `GpuInstanceDynamic[]` | `Storage | TransferDst` |              |
| `GpuWorld.EntityConfig`         | `GpuEntityConfig[]`    | `Storage | TransferDst` |              |
| `GpuWorld.GeometryRecords`      | `GpuGeometryRecord[]`  | `Storage | TransferDst` |              |
| `GpuWorld.Bounds`               | `GpuBounds[]`          | `Storage | TransferDst` |              |
| `GpuWorld.Lights`               | `GpuLight[]`           | `Storage | TransferDst` |              |
| `GpuWorld.SceneTable`           | one `GpuSceneTable`    | `Storage | TransferDst` |              |
| `GpuWorld.ManagedVertexBuffer0` | packed vertex bytes    | `Storage | TransferDst` |              |
| `GpuWorld.ManagedIndexBuffer0`  | `uint32_t` indices     | `Index   | Storage      | TransferDst` |

For now, one managed vertex buffer and one managed index buffer are enough. Add comments saying multiple buffers and compaction are future work.

### TODO 3.5 — **Done**

 Implement dirty range upload

Keep CPU mirrors:

```cpp
std::vector<RHI::GpuInstanceStatic>  InstanceStaticCpu;
std::vector<RHI::GpuInstanceDynamic> InstanceDynamicCpu;
std::vector<RHI::GpuEntityConfig>    EntityConfigCpu;
std::vector<RHI::GpuGeometryRecord>  GeometryRecordsCpu;
std::vector<RHI::GpuBounds>          BoundsCpu;
std::vector<RHI::GpuLight>           LightsCpu;
RHI::GpuSceneTable                   SceneTableCpu;
```

Keep dirty bit arrays:

```cpp
std::vector<bool> DirtyInstanceStatic;
std::vector<bool> DirtyInstanceDynamic;
std::vector<bool> DirtyEntityConfig;
std::vector<bool> DirtyGeometryRecord;
std::vector<bool> DirtyBounds;
bool DirtyLights = false;
bool DirtySceneTable = true;
```

Implement helper:

```cpp
template<class T>
void FlushDirtyRuns(
    RHI::IDevice& device,
    RHI::BufferHandle dst,
    std::vector<T>& cpu,
    std::vector<bool>& dirty);
```

It should coalesce contiguous dirty slots into one `WriteBuffer` call per run.

### TODO 3.6 — **Done**

 Implement scene table refresh

Whenever any buffer is allocated or material buffer/lights change, update `SceneTableCpu`:

```cpp
SceneTableCpu.InstanceStaticBDA  = Device->GetBufferDeviceAddress(InstanceStaticBuffer);
SceneTableCpu.InstanceDynamicBDA = Device->GetBufferDeviceAddress(InstanceDynamicBuffer);
SceneTableCpu.EntityConfigBDA    = Device->GetBufferDeviceAddress(EntityConfigBuffer);
SceneTableCpu.GeometryRecordBDA  = Device->GetBufferDeviceAddress(GeometryRecordBuffer);
SceneTableCpu.BoundsBDA          = Device->GetBufferDeviceAddress(BoundsBuffer);
SceneTableCpu.MaterialBDA        = Device->GetBufferDeviceAddress(MaterialBuffer);
SceneTableCpu.LightBDA           = Device->GetBufferDeviceAddress(LightBuffer);
SceneTableCpu.InstanceCapacity   = MaxInstances;
SceneTableCpu.GeometryCapacity   = MaxGeometryRecords;
SceneTableCpu.MaterialCapacity   = MaterialCapacity;
SceneTableCpu.LightCount         = LightCount;
```

Upload one `GpuSceneTable` in `SyncFrame()`.

### TODO 3.7 — **Done**

 Implement simple managed geometry allocator

For first implementation:

```cpp
std::uint64_t VertexBumpOffset = 0;
std::uint64_t IndexBumpOffset = 0;
```

Use bump allocation only. No free-list yet.

`UploadGeometry()` should:

1. Allocate vertex byte range.
2. Allocate surface index range.
3. Allocate line index range.
4. Upload vertex bytes to managed vertex buffer.
5. Upload surface indices and line indices to managed index buffer.
6. Allocate geometry record slot.
7. Fill `GpuGeometryRecord`.
8. Mark geometry record dirty.
9. Return `GpuGeometryHandle`.

`FreeGeometry()` should mark record empty but does not reclaim byte ranges yet. Add TODO comment for free-list/compaction.

### Acceptance

* `GpuWorld` compiles.
* `GpuWorld::Initialize()` allocates all persistent buffers.
* `GpuWorld::SyncFrame()` uploads dirty runs.
* Null backend still works.
* No ECS imports inside `GpuWorld`.

---

## Phase 4 — Expose `GpuWorld` through renderer — **Complete**

### Files

* `src_new/Graphics/Graphics.Renderer.cppm`
* `src_new/Graphics/Graphics.Renderer.cpp`
* `src_new/Graphics/CMakeLists.txt`

### TODO 4.1 — Import `GpuWorld` — **Done**

In `Graphics.Renderer.cppm`, import:

```cpp
import Extrinsic.Graphics.GpuWorld;
```

Add to `IRenderer`:

```cpp
[[nodiscard]] virtual GpuWorld& GetGpuWorld() = 0;
```

### TODO 4.2 — Add `GpuWorld` member to renderer implementation — **Done**

In `Graphics.Renderer.cpp`, add:

```cpp
std::optional<GpuWorld> m_GpuWorld;
```

Initialize after `BufferManager` exists and before systems that need scene buffers:

```cpp
m_GpuWorld.emplace();
m_GpuWorld->Initialize(device, *m_BufferManager);
```

After `MaterialSystem::Initialize`, call:

```cpp
m_GpuWorld->SetMaterialBuffer(
    m_MaterialSystem->GetBuffer(),
    m_MaterialSystem->GetCapacity());
```

In `PrepareFrame`, call:

```cpp
m_PipelineManager->CommitPending();
m_MaterialSystem->SyncGpuBuffer();

m_GpuWorld->SetMaterialBuffer(
    m_MaterialSystem->GetBuffer(),
    m_MaterialSystem->GetCapacity());

m_GpuWorld->SyncFrame();
```

### TODO 4.3 — Shutdown order — **Done**

Shutdown in this order:

1. pass systems
2. culling
3. `GpuWorld`
4. material system
5. resource managers

### Acceptance

* `IRenderer::GetGpuWorld()` exists.
* Renderer initializes and shuts down `GpuWorld`.
* Renderer still works with Null backend.

---

## Phase 5 — Update GPU scene component — **Complete**

### File

* `src_new/Graphics/Components/Graphics.Component.GpuSceneSlot.cppm`

Current repo has a `GpuSceneSlot` component. Keep the name for now to avoid churn, but change its meaning from old culling-slot ownership to new `GpuWorld` slot ownership.

### TODO 5.1 — Replace/add fields — **Done**

Make the component hold:

```cpp
export struct GpuSceneSlot {
    std::uint32_t InstanceSlot = UINT32_MAX;
    std::uint32_t InstanceGeneration = 0;

    std::uint32_t GeometrySlot = UINT32_MAX;
    std::uint32_t GeometryGeneration = 0;

    std::unordered_map<std::string, RHI::BufferHandle> NamedBuffers;
    std::unordered_map<std::string, BufferEntry> NamedBufferEntries;

    bool HasInstance() const noexcept { return InstanceSlot != UINT32_MAX; }
    bool HasGeometry() const noexcept { return GeometrySlot != UINT32_MAX; }

    RHI::BufferHandle Find(std::string_view name) const;
    const BufferEntry* FindEntry(std::string_view name) const;
};
```

Preserve existing helper names if possible so `VisualizationSyncSystem` still compiles.

### TODO 5.2 — Add conversion helpers — **Done**

Add:

```cpp
GpuInstanceHandle ToInstanceHandle() const noexcept;
GpuGeometryHandle ToGeometryHandle() const noexcept;
void SetInstanceHandle(GpuInstanceHandle h) noexcept;
void SetGeometryHandle(GpuGeometryHandle h) noexcept;
```

### Acceptance

* Existing code that calls `Find()` / `FindEntry()` still compiles.
* Component now stores instance and geometry handles compatible with `GpuWorld`.

---

## Phase 6 — Refactor visualization sync into `GpuEntityConfig` — **Complete**

### Files

* `src_new/Graphics/Graphics.VisualizationSyncSystem.cppm`
* `src_new/Graphics/Graphics.VisualizationSyncSystem.cpp`
* `src_new/Graphics/Graphics.MaterialSystem.cppm`
* `src_new/Graphics/Graphics.MaterialSystem.cpp`

The current `VisualizationSyncSystem` packs scalar BDA and element count into `GpuMaterialSlot::CustomData[2]`. That should be moved out of material data and into `GpuEntityConfig`. ([GitHub][6])

### TODO 6.1 — Change `VisualizationSyncSystem::Sync` signature — **Done**

Change from:

```cpp
void Sync(entt::registry& registry, MaterialSystem& matSys, ColormapSystem& colormapSys);
```

to:

```cpp
void Sync(
    entt::registry& registry,
    MaterialSystem& matSys,
    ColormapSystem& colormapSys,
    GpuWorld& gpuWorld);
```

### TODO 6.2 — Keep SciVis material type but stop storing BDAs in material — **Done**

Keep override material leases for now so shaders can branch on `MaterialTypeID == SciVis`.

But change custom data usage:

```text
GpuMaterialSlot:
  CustomData[0] may hold uniform visual constants.
  CustomData[1] may hold isoline/bin style constants.
  CustomData[2] must NOT hold ScalarBDA, ColorBDA, ElementCount, or ColorSourceMode anymore.
```

### TODO 6.3 — Build `GpuEntityConfig` — **Done**

For every entity with `VisualizationConfig + GpuSceneSlot`, build:

```cpp
RHI::GpuEntityConfig cfg{};
```

Set:

```cpp
cfg.ColormapID = colormapSys.GetBindlessIndex(...).Value;
cfg.ScalarRangeMin = ...
cfg.ScalarRangeMax = ...
cfg.BinCount = ...
cfg.IsolineCount = ...
cfg.IsolineWidth = ...
cfg.IsolineColor = ...
cfg.VisualizationAlpha = ...
cfg.VisDomain = ...
cfg.ColorSourceMode = ...
cfg.ElementCount = ...
```

Resolve BDA pointers:

```cpp
cfg.ScalarBDA = device.GetBufferDeviceAddress(scalarBuffer);
cfg.ColorBDA = device.GetBufferDeviceAddress(colorBuffer);
cfg.VertexNormalBDA = device.GetBufferDeviceAddress(normalBuffer);
cfg.PointSizeBDA = device.GetBufferDeviceAddress(pointSizeBuffer);
```

Then call:

```cpp
gpuWorld.SetEntityConfig(gpuSlot.ToInstanceHandle(), cfg);
```

### TODO 6.4 — Uniform color path — **Done**

For uniform color visualization:

```cpp
cfg.ColorSourceMode = 1;
cfg.UniformColor = visCfg->Color;
```

No BDA required.

### TODO 6.5 — Scalar field path — **Done**

For scalar fields:

```cpp
cfg.ColorSourceMode = 2;
cfg.ScalarBDA = scalarBda;
cfg.ElementCount = elementCount;
```

### TODO 6.6 — Per-element RGBA path — **Done**

For per-vertex/per-edge/per-face color buffers:

```cpp
cfg.ColorSourceMode = 3;
cfg.ColorBDA = colorBda;
cfg.ElementCount = elementCount;
```

### TODO 6.7 — Material effective slot — **Done**

Still set:

```cpp
matInst.EffectiveSlot = matSys.GetMaterialSlot(lease.GetHandle());
```

But the material override now only selects SciVis shading mode. It does not own entity-specific attribute pointers.

### Acceptance

* No BDA packing remains in `GpuMaterialSlot::CustomData[2]`.
* `VisualizationSyncSystem` writes `GpuEntityConfig`.
* SciVis still works through material type ID.
* Material slots remain independent from geometry/attribute data.

---

## Phase 7 — Implement transform and instance sync — **Complete**

### Files

* `src_new/Graphics/Graphics.TransformSyncSystem.cppm`
* `src_new/Graphics/Graphics.TransformSyncSystem.cpp`

Current `TransformSyncSystem` is only a stub. ([GitHub][7])

### TODO 7.1 — Change API — **Done**

Change:

```cpp
void SyncGpuBuffer();
```

to:

```cpp
void SyncGpuBuffer(entt::registry& registry, GpuWorld& gpuWorld);
```

Import:

```cpp
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
```

Also import the transform component module used by the repo.

### TODO 7.2 — Write dynamic transforms — **Done**

For each entity with `Transform::Component + GpuSceneSlot`:

```cpp
gpuWorld.SetInstanceTransform(
    gpuSlot.ToInstanceHandle(),
    currentWorldMatrix,
    previousWorldMatrix);
```

If previous matrix is unavailable, use current matrix.

### TODO 7.3 — Write material slot and render flags — **Done**

For each entity with `MaterialInstance + GpuSceneSlot`:

```cpp
gpuWorld.SetInstanceMaterialSlot(
    gpuSlot.ToInstanceHandle(),
    matInst.EffectiveSlot);
```

For render flags, derive from render components:

```text
RenderSurface present -> GpuRender_Surface
RenderLines present   -> GpuRender_Line
RenderPoints present  -> GpuRender_Point
visible               -> GpuRender_Visible
opaque material       -> GpuRender_Opaque
alpha masked material -> GpuRender_AlphaMask
unlit material        -> GpuRender_Unlit
```

### TODO 7.4 — Bounds update — **Done**

If transform or geometry changed, compute world bounds on CPU for now:

```cpp
RHI::GpuBounds bounds{};
bounds.LocalSphere = ...
bounds.WorldSphere = ...
bounds.WorldAabbMin = ...
bounds.WorldAabbMax = ...
gpuWorld.SetBounds(instance, bounds);
```

Do not implement GPU bounds update yet.

### Acceptance

* `TransformSyncSystem` writes `GpuInstanceDynamic`.
* `TransformSyncSystem` writes material slot/render flags into `GpuInstanceStatic`.
* `TransformSyncSystem` writes `GpuBounds`.
* Moving an entity updates only dynamic instance/bounds data, not material/config buffers.

---

## Phase 8 — Replace `GpuScene` usage with `GpuWorld` — **Complete**

### Files

* `src_new/Graphics/Graphics.GpuScene.cppm`
* `src_new/Graphics/Graphics.GpuScene.cpp`
* all call sites of `GpuScene`

### TODO 8.1 — **Done**

Keep `GpuScene` temporarily as compatibility wrapper

Do **not** delete `GpuScene` immediately.

Change its comments to:

```cpp
// Legacy compatibility wrapper. New rendering code must use GpuWorld.
```

### TODO 8.2 — **Done**

Redirect slot allocation

Where old code calls:

```cpp
GpuScene::AllocateSlot()
```

replace with:

```cpp
GpuWorld::AllocateInstance(entityId)
```

and store handle in `GpuSceneSlot`.

### TODO 8.3 — **Done**

Redirect static geometry upload

Where old code calls:

```cpp
UploadStaticVertices()
UploadStaticIndices()
```

replace with:

```cpp
GpuWorld::UploadGeometry()
```

### TODO 8.4 — **Done**

Remove per-entity dynamic geometry buffers from rendering path

Do not allocate one host-visible storage buffer per entity for positions/scalars/colors unless it is an attribute buffer referenced from `GpuEntityConfig`.

### Acceptance

* New render path does not use `GpuScene.StaticVertexBuffer()` / `StaticIndexBuffer()` directly.
* `GpuScene` can remain for old tests, but renderer uses `GpuWorld`.

---

## Phase 9 — Redesign culling system around draw buckets — **Complete**

### Files

* `src_new/Graphics/Graphics.CullingSystem.cppm`
* `src_new/Graphics/Graphics.CullingSystem.cpp`
* `src_new/Graphics/Passes/Pass.Culling.cppm`
* `src_new/Graphics/Passes/Pass.Culling.cpp`

The current culling system owns one cull input buffer, one draw-command buffer, and one visibility counter. That is only a scaffold. It should become bucketed: surface, line, point, shadow, etc. The current implementation already writes `DrawCommandBuffer` and `VisibilityCountBuffer`, but the count-buffer barrier after dispatch uses `ShaderRead`; indirect drawing needs `IndirectRead`. ([GitHub][8])

### TODO 9.1 — **Done**

— Add draw bucket resource struct

In `Graphics.CullingSystem.cppm`, add:

```cpp
struct GpuDrawBucket {
    RHI::BufferHandle IndexedArgsBuffer;
    RHI::BufferHandle NonIndexedArgsBuffer;
    RHI::BufferHandle CountBuffer;
    std::uint32_t Capacity = 0;
    bool Indexed = true;
};
```

### TODO 9.2 — **Done**

— CullingSystem owns one bucket per kind

In implementation, store:

```cpp
std::array<GpuDrawBucket, static_cast<size_t>(RHI::GpuDrawBucketKind::Count)> Buckets;
```

Allocate:

```text
SurfaceOpaque   -> indexed commands
SurfaceAlphaMask -> indexed commands
Lines           -> indexed commands
Points          -> non-indexed commands
ShadowOpaque    -> indexed commands
```

Use buffer usages:

```cpp
RHI::BufferUsage::Storage |
RHI::BufferUsage::Indirect |
RHI::BufferUsage::TransferDst
```

for command buffers and count buffers.

### TODO 9.3 — **Done**

— Replace old `GpuCullData`

Stop using a separate CPU-written `GpuCullData[]`.

The cull shader should read directly from `GpuWorld`:

```text
GpuSceneTable
  -> InstanceStatic[]
  -> InstanceDynamic[]
  -> GeometryRecord[]
  -> Bounds[]
```

### TODO 9.4 — **Done**

— New culling push constants

Add in `RHI.Types.cppm`:

```cpp
export struct alignas(16) GpuCullPushConstants {
    alignas(16) glm::vec4 FrustumPlanes[6];

    std::uint64_t SceneTableBDA = 0;

    std::uint64_t SurfaceOpaqueArgsBDA = 0;
    std::uint64_t SurfaceOpaqueCountBDA = 0;

    std::uint64_t SurfaceAlphaMaskArgsBDA = 0;
    std::uint64_t SurfaceAlphaMaskCountBDA = 0;

    std::uint64_t LineArgsBDA = 0;
    std::uint64_t LineCountBDA = 0;

    std::uint64_t PointArgsBDA = 0;
    std::uint64_t PointCountBDA = 0;

    std::uint64_t ShadowArgsBDA = 0;
    std::uint64_t ShadowCountBDA = 0;

    std::uint32_t InstanceCapacity = 0;
    std::uint32_t _pad0 = 0;
};
```

If this exceeds 128 bytes, do **not** push all bucket BDAs directly. Instead create one `GpuCullOutputTable` buffer and push only:

```cpp
std::uint64_t SceneTableBDA;
std::uint64_t CullOutputTableBDA;
```

Prefer the output-table solution if push constant size becomes an issue.

### TODO 9.5 — **Done**

— Reset counters using `FillBuffer`

Replace current host `WriteBuffer` reset with:

```cpp
cmd.FillBuffer(bucket.CountBuffer, 0, sizeof(std::uint32_t), 0);
cmd.BufferBarrier(bucket.CountBuffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderWrite);
```

Do this for every bucket.

### TODO 9.6 — **Done**

— Dispatch linear culling

Culling dispatch:

```cpp
groups = (gpuWorld.GetInstanceCapacity() + 63) / 64;
cmd.Dispatch(groups, 1, 1);
```

Do not dispatch by live count unless you have a compact live-instance list. Linear pass can skip non-live/invisible instances using flags.

### TODO 9.7 — **Done**

— Correct post-cull barriers

After dispatch:

```cpp
cmd.BufferBarrier(argsBuffer, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::IndirectRead);
cmd.BufferBarrier(countBuffer, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::IndirectRead);
```

For all buckets.

### TODO 9.8 — **Done**

— Accessors

Add:

```cpp
const GpuDrawBucket& GetBucket(RHI::GpuDrawBucketKind kind) const;
```

### Acceptance

* Culling system has multiple buckets.
* Counter reset is GPU-side.
* Count buffers transition to `IndirectRead`.
* No old `GpuCullData[]` input buffer is required.

---

## Phase 10 — Write linear culling compute shader — **Complete**

### Files to add

Exact shader path depends on your shader directory. Add:

* `shaders/src_new/common/gpu_scene.glsl`
* `shaders/src_new/culling/instance_cull.comp`

If shader paths are elsewhere, keep names but place them in the existing shader tree.

### TODO 10.1 — Add shared shader structs — **Done**

In `gpu_scene.glsl`, mirror:

```glsl
GpuSceneTable
GpuInstanceStatic
GpuInstanceDynamic
GpuGeometryRecord
GpuEntityConfig
GpuBounds
GpuDrawIndexedCommand
GpuDrawCommand
```

Use `GL_EXT_buffer_reference2` and `GL_EXT_scalar_block_layout` if the existing shader toolchain supports it.

### TODO 10.2 — Implement frustum sphere test — **Done**

In `instance_cull.comp`:

```glsl
bool sphereVisible(vec4 worldSphere, vec4 planes[6]) {
    for (int i = 0; i < 6; ++i) {
        float d = dot(planes[i].xyz, worldSphere.xyz) + planes[i].w;
        if (d < -worldSphere.w) return false;
    }
    return true;
}
```

### TODO 10.3 — One thread per instance slot — **Done**

```glsl
uint slot = gl_GlobalInvocationID.x;
if (slot >= pc.InstanceCapacity) return;

GpuInstanceStatic inst = InstanceStatic[slot];
if ((inst.RenderFlags & GpuRender_Visible) == 0) return;
```

### TODO 10.4 — Read bounds and geometry — **Done**

```glsl
GpuBounds bounds = Bounds[slot];
if (!sphereVisible(bounds.WorldSphere, pc.FrustumPlanes)) return;

GpuGeometryRecord geo = GeometryRecords[inst.GeometrySlot];
```

### TODO 10.5 — Emit surface command — **Done**

If flags include surface:

```glsl
uint outIndex = atomicAdd(surfaceCount[0], 1);

SurfaceArgs[outIndex].indexCount = geo.SurfaceIndexCount;
SurfaceArgs[outIndex].instanceCount = 1;
SurfaceArgs[outIndex].firstIndex = geo.SurfaceFirstIndex;
SurfaceArgs[outIndex].vertexOffset = int(geo.VertexOffset);
SurfaceArgs[outIndex].firstInstance = slot;
```

### TODO 10.6 — Emit line command — **Done**

If flags include line:

```glsl
uint outIndex = atomicAdd(lineCount[0], 1);

LineArgs[outIndex].indexCount = geo.LineIndexCount;
LineArgs[outIndex].instanceCount = 1;
LineArgs[outIndex].firstIndex = geo.LineFirstIndex;
LineArgs[outIndex].vertexOffset = int(geo.VertexOffset);
LineArgs[outIndex].firstInstance = slot;
```

### TODO 10.7 — Emit point command — **Done**

If flags include point:

```glsl
uint outIndex = atomicAdd(pointCount[0], 1);

PointArgs[outIndex].vertexCount = geo.PointVertexCount;
PointArgs[outIndex].instanceCount = 1;
PointArgs[outIndex].firstVertex = geo.PointFirstVertex;
PointArgs[outIndex].firstInstance = slot;
```

### Acceptance

* Compute shader compiles to SPIR-V.
* Culling writes bucket-specific commands and counters.
* `firstInstance` is always the `InstanceSlot`.

---

## Phase 11 — Update draw passes to consume buckets — **Complete**

### Files

* `src_new/Graphics/Passes/Pass.Deferred.GBuffers.cppm`
* `src_new/Graphics/Passes/Pass.Deferred.GBuffers.cpp`
* `src_new/Graphics/Passes/Pass.Forward.Line.cppm`
* `src_new/Graphics/Passes/Pass.Forward.Line.cpp`
* `src_new/Graphics/Passes/Pass.Forward.Point.cppm`
* `src_new/Graphics/Passes/Pass.Forward.Point.cpp`
* `src_new/Graphics/Passes/Pass.Shadows.cppm`
* `src_new/Graphics/Passes/Pass.Shadows.cpp`

### TODO 11.1 — GBuffer pass uses surface bucket — **Done**

In GBuffer pass:

```cpp
const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);

cmd.BindPipeline(gbufferPipeline);
cmd.BindIndexBuffer(
    gpuWorld.GetManagedIndexBuffer(),
    0,
    RHI::IndexType::Uint32);

RHI::GpuScenePushConstants pc{};
pc.SceneTableBDA = gpuWorld.GetSceneTableBDA();
pc.FrameIndex = frame.FrameIndex;
pc.DrawBucket = static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque);

cmd.PushConstants(&pc, sizeof(pc));

cmd.DrawIndexedIndirectCount(
    bucket.IndexedArgsBuffer,
    0,
    bucket.CountBuffer,
    0,
    bucket.Capacity);
```

### TODO 11.2 — Line pass uses line bucket — **Done**

```cpp
const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Lines);

cmd.BindPipeline(linePipeline);
cmd.BindIndexBuffer(
    gpuWorld.GetManagedIndexBuffer(),
    0,
    RHI::IndexType::Uint32);

cmd.DrawIndexedIndirectCount(
    bucket.IndexedArgsBuffer,
    0,
    bucket.CountBuffer,
    0,
    bucket.Capacity);
```

### TODO 11.3 — Point pass uses non-indexed point bucket — **Done**

```cpp
const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);

cmd.BindPipeline(pointPipeline);

cmd.DrawIndirectCount(
    bucket.NonIndexedArgsBuffer,
    0,
    bucket.CountBuffer,
    0,
    bucket.Capacity);
```

### TODO 11.4 — Shadow pass uses shadow bucket — **Done**

```cpp
const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::ShadowOpaque);

cmd.BindPipeline(shadowPipeline);
cmd.BindIndexBuffer(gpuWorld.GetManagedIndexBuffer(), 0, RHI::IndexType::Uint32);

cmd.DrawIndexedIndirectCount(
    bucket.IndexedArgsBuffer,
    0,
    bucket.CountBuffer,
    0,
    bucket.Capacity);
```

### Acceptance

* No per-entity draw loop in passes.
* Draw passes consume only indirect command buffers.
* Indexed passes call `BindIndexBuffer` first.
* Point pass uses `DrawIndirectCount`.

---

## Phase 12 — Add BDA-driven vertex shaders — **Complete**

### Files to add/edit

* `shaders/src_new/common/gpu_scene.glsl`
* `shaders/src_new/deferred/gbuffer.vert`
* `shaders/src_new/deferred/gbuffer.frag`
* `shaders/src_new/forward/line.vert`
* `shaders/src_new/forward/line.frag`
* `shaders/src_new/forward/point.vert`
* `shaders/src_new/forward/point.frag`

### TODO 12.1 — **Done**

Vertex fetch contract

For indexed draws, the shader must use:

```glsl
uint instanceSlot = gl_InstanceIndex;
GpuInstanceStatic inst = InstanceStatic[instanceSlot];
GpuInstanceDynamic dyn = InstanceDynamic[instanceSlot];
GpuGeometryRecord geo = GeometryRecords[inst.GeometrySlot];

uint vertexId = uint(gl_VertexIndex);
```

Then fetch packed vertex data from:

```glsl
geo.VertexBufferBDA + vertexId * sizeof(PackedVertex)
```

`PackedVertex` layout:

```text
float px, py, pz, u, v
20 bytes
```

If scalar layout cannot safely express 20-byte stride in your shader compiler, change the GPU vertex layout to 32 bytes:

```text
vec4 position_pad
vec4 uv_pad
```

Do this only if required by compiler/validation.

### TODO 12.2 — **Done**

Transform

```glsl
vec4 worldPos = dyn.Model * vec4(localPos, 1.0);
gl_Position = Camera.ViewProj * worldPos;
```

### TODO 12.3 — **Done**

Material fetch

Fragment shader:

```glsl
GpuInstanceStatic inst = InstanceStatic[instanceSlot];
GpuMaterialSlot mat = Materials[inst.MaterialSlot];
GpuEntityConfig cfg = EntityConfigs[inst.ConfigSlot];
```

### TODO 12.4 — **Done**

Sci-vis fetch

If material type is SciVis or `cfg.ColorSourceMode != 0`:

```text
ColorSourceMode 1 -> cfg.UniformColor
ColorSourceMode 2 -> cfg.ScalarBDA + cfg.ColormapID
ColorSourceMode 3 -> cfg.ColorBDA
```

### TODO 12.5 — **Done**

Entity ID output

GBuffer pass writes:

```glsl
GBuf_EntityId = inst.EntityID;
```

### Acceptance

* Shaders do not require per-entity descriptor sets.
* Shaders recover all state from `SceneTableBDA` and `gl_InstanceIndex`.
* `GpuMaterialSlot` only contains material data.
* Attribute/scalar/color pointers are read from `GpuEntityConfig`.

---

## Phase 13 — Light buffer — **Complete**

### Files

* `src_new/Graphics/Graphics.LightSystem.cppm`
* `src_new/Graphics/Graphics.LightSystem.cpp`
* `src_new/Graphics/Graphics.GpuWorld.cppm`
* `src_new/Graphics/Graphics.GpuWorld.cpp`
* deferred lighting shader/pass files

Current `LightSystem` mainly populates frame-global directional/ambient data in the camera UBO. Keep that for baseline directional/ambient light, but add a real `GpuLight[]` buffer for point/spot/area lights. ([GitHub][3])

### TODO 13.1 — Add light sync API — **Done**

Change or add:

```cpp
void LightSystem::SyncGpuBuffer(entt::registry& registry, GpuWorld& gpuWorld);
```

### TODO 13.2 — Populate `GpuLight[]` — **Done**

For each point/spot/area light component:

```cpp
RHI::GpuLight light{};
light.Position_Range = {pos.x, pos.y, pos.z, range};
light.Direction_Type = {dir.x, dir.y, dir.z, type};
light.Color_Intensity = {color.r, color.g, color.b, intensity};
light.Params = {...};
```

Call:

```cpp
gpuWorld.SetLights(lights);
```

### TODO 13.3 — Deferred lighting shader reads light buffer — **Done**

Deferred lighting pass receives `SceneTableBDA`, reads:

```glsl
GpuSceneTable scene = ...
for (uint i = 0; i < scene.LightCount; ++i) {
    GpuLight light = Lights[i];
    ...
}
```

### Acceptance

* Directional/ambient light still works.
* Point/spot lights are in `GpuLight[]`.
* Light data is not mixed into instance/material buffers.

---

## Phase 14 — Renderer frame order — **Complete**

### File

* `src_new/Graphics/Graphics.Renderer.cpp`

### TODO 14.1 — Enforce sync order — **Done**

Use this order in `PrepareFrame` or equivalent render preparation:

```cpp
m_PipelineManager->CommitPending();

m_MaterialSystem->SyncGpuBuffer();

m_VisualizationSyncSystem->Sync(
    registry,
    *m_MaterialSystem,
    *m_ColormapSystem,
    *m_GpuWorld);

m_MaterialSystem->SyncGpuBuffer();

m_TransformSyncSystem->SyncGpuBuffer(
    registry,
    *m_GpuWorld);

m_LightSystem->SyncGpuBuffer(
    registry,
    *m_GpuWorld);

m_GpuWorld->SetMaterialBuffer(
    m_MaterialSystem->GetBuffer(),
    m_MaterialSystem->GetCapacity());

m_GpuWorld->SyncFrame();
```

Adapt for the actual place where `registry` is available.

### TODO 14.2 — GPU command order — **Done**

In `ExecuteFrame`:

```text
1. Reset culling counters
2. Dispatch culling
3. Optional depth prepass
4. GBuffer pass
5. Shadow pass
6. Deferred lighting pass
7. Forward line pass
8. Forward point pass
9. Selection/outline
10. Postprocess/present
```

### Acceptance

* Materials are uploaded before `GpuWorld` scene table points to material buffer.
* Visualization config is uploaded before transform/instance sync finalizes material slot.
* Culling runs before all indirect draw passes.

---

## Phase 15 — Tests and validation

### TODO 15.1 — Compile tests — **Done**

Add or update tests so these compile:

```cpp
static_assert(sizeof(RHI::GpuGeometryRecord) == 64);
static_assert(sizeof(RHI::GpuInstanceStatic) == 32);
static_assert(sizeof(RHI::GpuInstanceDynamic) == 128);
static_assert(sizeof(RHI::GpuEntityConfig) == 128);
static_assert(sizeof(RHI::GpuBounds) == 64);
static_assert(sizeof(RHI::GpuLight) == 64);
```

### TODO 15.2 — Null backend test — **Done**

Create a test that:

1. Creates Null device.
2. Creates renderer.
3. Initializes renderer.
4. Gets `GpuWorld`.
5. Allocates one instance.
6. Uploads tiny triangle geometry.
7. Sets material slot 0.
8. Sets transform.
9. Calls `SyncFrame()`.
10. Shuts down cleanly.

### TODO 15.3 — Culling CPU-side smoke test — **Done**

With Null backend, ensure:

```cpp
GpuWorld::AllocateInstance()
GpuWorld::SetBounds()
GpuWorld::SetInstanceRenderFlags()
CullingSystem::Initialize()
CullingSystem::GetBucket(...)
```

all work without GPU execution.

### TODO 15.4 — Vulkan validation target

With Vulkan backend:

1. Render one triangle.
2. Render one line.
3. Render one point cloud.
4. Enable validation layers.
5. Confirm no errors for:

  * missing index buffer
  * indirect buffer usage
  * count buffer usage
  * buffer device address usage
  * descriptor indexing

The Vulkan backend already documents that it requires Vulkan 1.3, descriptor indexing, dynamic rendering, and timeline semaphores. ([GitHub][9])

---

## Phase 16 — Cleanup after first working GPU-driven path

### TODO 16.1 — Deprecate old culling API — **Done**

Remove or mark legacy:

```cpp
CullingSystem::Register()
CullingSystem::Unregister()
CullingSystem::UpdateBounds()
CullingSystem::SetDrawTemplate()
```

The new culling input is `GpuWorld`, not a separate culling registration table.

### TODO 16.2 — Deprecate old `GpuScene` — **Done**

After all call sites use `GpuWorld`, either delete:

```text
Graphics.GpuScene.cppm
Graphics.GpuScene.cpp
```

or keep them as a thin compatibility wrapper only.

### TODO 16.3 — Update README — **Done**

Update:

```text
src_new/Graphics/README.md
```

Change the current SciVis custom data layout section because BDA pointers no longer live in `GpuMaterialSlot::CustomData[2]`.

New rule:

```text
Material slots own material constants and material type.
GpuEntityConfig owns visualization source pointers, scalar ranges, domains, and per-entity attribute state.
```

### TODO 16.4 — Add architecture note — **Done**

Update:

```text
docs/architecture/src_new-rendering-architecture.md
```

Add:

```text
Implementation note:
The first production implementation uses a BDA root table (`GpuSceneTable`) rather than descriptor bindings for every scene SSBO. The buffers remain separate GPU storage buffers; only their addresses are gathered into a one-entry scene table.
```

---

## Phase 17 — Defer these until after the renderer works

Do **not** ask Codex to implement these in the first pass:

1. LBVH.
2. Hi-Z occlusion culling.
3. Multi-buffer geometry compaction.
4. Async transfer state machine for all geometry uploads.
5. Clustered/tiled lighting.
6. GPU picking refinement.
7. Mesh shaders.
8. GPU-driven material sorting.
9. Bindless meshlet system.

Add these as future TODOs only.

---

## Minimal acceptance target for the first implementation

Codex is done with the first milestone when this works:

```text
One triangle entity:
  CPU creates entity
  GpuWorld uploads geometry
  entity gets InstanceSlot
  TransformSync writes model matrix
  VisualizationSync writes GpuEntityConfig
  MaterialSystem uploads material
  GpuWorld uploads scene table
  cull compute writes one SurfaceOpaque indirect command
  GBuffer pass calls BindIndexBuffer + DrawIndexedIndirectCount
  shader uses firstInstance -> InstanceSlot -> geometry/material/config
  triangle appears
```

Then test:

```text
One mesh with:
  surface rendering
  wireframe line rendering
  point rendering
  scalar visualization
  material-only rendering
  moving transform
```

The critical invariant to preserve everywhere:

```text
firstInstance == InstanceSlot
InstanceSlot -> InstanceStatic + InstanceDynamic + EntityConfig + Bounds
InstanceStatic.GeometrySlot -> GeometryRecord
InstanceStatic.MaterialSlot -> MaterialSlot
GeometryRecord -> managed vertex/index buffer ranges
EntityConfig -> scalar/color/normal/point-size BDA pointers
```

[1]: https://github.com/intrinsicD/IntrinsicEngine/tree/main/src_new/Graphics "IntrinsicEngine/src_new/Graphics at main · intrinsicD/IntrinsicEngine · GitHub"
[2]: https://raw.githubusercontent.com/intrinsicD/IntrinsicEngine/main/src_new/Graphics/RHI/RHI.CommandContext.cppm "raw.githubusercontent.com"
[3]: https://github.com/intrinsicD/IntrinsicEngine/blob/main/docs/architecture/src_new-rendering-architecture.md "IntrinsicEngine/docs/architecture/src_new-rendering-architecture.md at main · intrinsicD/IntrinsicEngine · GitHub"
[4]: https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndirectCount.html "vkCmdDrawIndirectCount(3) :: Vulkan Documentation Project"
[5]: https://raw.githubusercontent.com/intrinsicD/IntrinsicEngine/main/src_new/Graphics/RHI/RHI.Types.cppm "raw.githubusercontent.com"
[6]: https://raw.githubusercontent.com/intrinsicD/IntrinsicEngine/main/src_new/Graphics/Graphics.VisualizationSyncSystem.cpp "raw.githubusercontent.com"
[7]: https://raw.githubusercontent.com/intrinsicD/IntrinsicEngine/main/src_new/Graphics/Graphics.TransformSyncSystem.cppm "raw.githubusercontent.com"
[8]: https://raw.githubusercontent.com/intrinsicD/IntrinsicEngine/main/src_new/Graphics/Graphics.CullingSystem.cpp "raw.githubusercontent.com"
[9]: https://github.com/intrinsicD/IntrinsicEngine/tree/main/src_new/Graphics/Backends/Vulkan "IntrinsicEngine/src_new/Graphics/Backends/Vulkan at main · intrinsicD/IntrinsicEngine · GitHub"


# Codex TODOs: `src_new` Task Graphs, Render Graph, and Streaming Graph

This document is the implementation backlog for Codex. It converts the architectural plan into ordered, reviewable tasks with explicit acceptance criteria, required tests, and quality gates.

## Non-negotiable implementation rules

- Work in small, reviewable increments. Do not implement multiple phases in one giant patch.
- Before each phase, audit the current repository state. If something is already implemented, verify it against the acceptance criteria and tests instead of duplicating it.
- Preserve the `src_new` layering contract:
  - `Core` owns reusable graph compilation, CPU task scheduling primitives, errors, logging, telemetry, memory, and task synchronization.
  - `Graphics.RenderGraph` owns GPU virtual resources, render/compute pass DAGs, layout/state transitions, barriers, transient resource lifetimes, and aliasing.
  - `Runtime` owns orchestration only. `Runtime::Engine` must not reason about GPU resources, barriers, Vulkan objects, or pass-level render details.
  - `Assets` must not import `Graphics`. GPU-side asset state lives in Graphics-owned cache/state once that bridge exists.
- Do not directly copy the legacy `src` implementation. Use it only for ideas and test coverage. The `src_new` design must be clean, modular, and domain-specific.
- Keep deterministic single-thread fallback behavior for every graph executor.
- Prefer narrow module imports in `.cpp` implementation units. Avoid importing umbrella modules when a smaller partition is enough.
- Keep public APIs small. Add partitions for internal structure instead of stuffing everything into one `.cppm` file.
- Use repository error/result conventions. Do not introduce exceptions.
- Do not use a global `WaitForAll()` as the normal wait path for a graph. CPU graph execution must use graph-local completion tokens so unrelated streaming work is not waited on accidentally.
- No hidden ordering by insertion except as a deterministic tie-break among otherwise independent passes.
- Every new subsystem must get focused tests in the matching `tests/<Subsystem>/CMakeLists.txt` object library.
- After adding/removing `src_new` modules, regenerate `docs/architecture/src_new_module_inventory.md` with `tools/generate_src_new_module_inventory.py`.

## Global build and test commands

Use these commands as the default validation baseline unless a phase specifies something narrower:

```bash
cmake --preset dev
cmake --build --preset dev --target IntrinsicTests
./build/dev/bin/IntrinsicTests
```

Focused targets to add/use as work progresses:

```bash
cmake --build --preset dev --target ExtrinsicCoreTests
./build/dev/bin/ExtrinsicCoreTests

cmake --build --preset dev --target ExtrinsicGraphicsTests
./build/dev/bin/ExtrinsicGraphicsTests

# Add this target when Runtime tests are introduced.
cmake --build --preset dev --target ExtrinsicRuntimeTests
./build/dev/bin/ExtrinsicRuntimeTests
```

Where a target name differs in the local checkout, update the commands in the PR notes and make sure the CMake test target remains discoverable through `ctest`.

---

# Phase 0 — Pre-flight audit and architecture contract

## T000 — Baseline repository audit

**Goal:** Establish exactly what exists before changing behavior.

**Tasks:**

- [x] Inspect `src_new/Core/Core.Dag.Scheduler.*`.
- [x] Inspect `src_new/Core/Core.Dag.TaskGraph.*`.
- [x] Inspect `src_new/Core/Core.FrameGraph.*`.
- [x] Inspect `src_new/Runtime/Runtime.Engine.*`.
- [x] Inspect `src_new/Graphics/Graphics.Renderer.*`.
- [x] Inspect `src_new/Graphics/RHI/*CommandContext*`, `RHI.Types`, `RHI.Device`, and backend command APIs.
- [x] Inspect `tests/Core/CMakeLists.txt`, `tests/Graphics/CMakeLists.txt`, and whether `tests/Runtime` exists.
- [x] Record in the PR description which tasks are missing, partial, or already complete.

**Acceptance criteria:**

- [x] The PR notes include a concise current-state matrix for Core scheduler, TaskGraph, FrameGraph, Runtime streaming, and Graphics renderer.
- [x] No behavior changes are made in this task.

Current-state matrix (T000 audit):

| Area | Current state | Status |
|---|---|---|
| Core scheduler (`Core.Dag.Scheduler`) | Has producer registration/query, topological build, priority/critical-path heuristics, lane assignment; resource hazards currently cached but not integrated into edge building. | Partial |
| Core `TaskGraph` | Supports pass setup + resource declarations + label wait/signal + compile/execute/reset; execute is layer-sequential fallback; compiler path is independent from scheduler substrate. | Partial |
| Core `FrameGraph` | Thin CPU-domain wrapper over `TaskGraph` with typed read/write forwarding; no structural ECS tokens yet. | Partial |
| Runtime streaming | Uses frame-local `TaskGraph(Streaming)` tick that compiles/builds/dispatches and resets each frame; shutdown currently drains via global `Scheduler::WaitForAll()`. | Missing persistent executor |
| Graphics renderer | Has renderer lifecycle and RHI integration; does not yet expose a dedicated `Graphics.RenderGraph` module partition set. | Partial |

**Validation:**

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --target IntrinsicTests`
- [ ] `./build/dev/bin/IntrinsicTests`

## T001 — Add task-graph architecture document

**Goal:** Make the intended design explicit before implementation.

**Files:**

- [x] Add `docs/architecture/src_new-task-graphs.md`.
- [ ] Update `src_new/Core/README.md` if public Core graph APIs change.
- [ ] Update `src_new/Graphics/README.md` when `Graphics.RenderGraph` is added.
- [ ] Update `src_new/Runtime/README.md` when runtime integration changes.

**Document must define:**

- [x] CPU task graph vs GPU render graph vs async streaming graph.
- [x] Shared graph compiler substrate.
- [x] Why `Graphics.RenderGraph` is not just `Core.TaskGraph` with `QueueDomain::Gpu`.
- [x] Graph lifecycle states: `Recording -> Compiled -> Executing/Consumed -> Reset`.
- [x] Resource hazard semantics: RAW, WAW, WAR, RAR.
- [x] Label signal/wait semantics.
- [x] CPU execution contract and graph-local completion.
- [x] Streaming persistence/cancellation contract.
- [x] GPU render graph resource, barrier, and aliasing contract.
- [x] Phase boundaries in `Engine::RunFrame`.
- [x] Test strategy and review gates.

**Acceptance criteria:**

- [x] The document is specific enough that a contributor can implement the APIs without reading legacy `src` internals.
- [x] The document explicitly forbids runtime pass-level render branching and GPU-resource manipulation in `Runtime`.
- [x] The document lists required tests for Core, Runtime/Streaming, and Graphics.

**Review gate RG-00:**

- [x] Docs reviewed for layering correctness.
- [x] No code behavior changed.
- [ ] Full test suite still passes.

---

# Phase 1 — Shared Core graph compiler substrate

## T010 — Introduce graph compiler types and partitions

**Goal:** Create a reusable compiler substrate in `Core` without forcing GPU semantics into Core.

**Files to add or split:**

- [x] `src_new/Core/Core.Dag.Scheduler.Types.cppm`
- [x] `src_new/Core/Core.Dag.Scheduler.Hazards.cppm`
- [x] `src_new/Core/Core.Dag.Scheduler.Compiler.cppm`
- [x] `src_new/Core/Core.Dag.Scheduler.Policy.cppm`
- [x] `src_new/Core/Core.Dag.Scheduler.DomainGraph.cppm`
- [ ] Matching `.cpp` implementation units as needed.
- [x] Update `src_new/Core/Core.Dag.Scheduler.cppm` to re-export the partitions.
- [x] Update `src_new/Core/CMakeLists.txt`.

**Public/minimally public types:**

- [x] `TaskId`
- [x] `ResourceId`
- [x] `LabelId`
- [x] `ResourceAccess`
- [x] `ResourceAccessMode`
- [x] `TaskPriority`
- [x] `QueueDomain`
- [x] `TaskKind`
- [x] `PendingTaskDesc`
- [x] `PlanTask`
- [x] `ExecutionLayer` (represented via `PlanTask::batch` topological layering).
- [x] `BuildConfig`
- [x] `ScheduleStats`
- [x] `CompiledGraph` or equivalent immutable compiled-plan type.

**Implementation details:**

- [x] Keep stable generation/index handle semantics where they already exist.
- [x] Keep existing public APIs source-compatible where practical.
- [x] Do not expose GPU layouts, Vulkan concepts, texture usages, or barrier details from Core.
- [x] Make `PlanTask::batch` mean topological layer, not priority.
- [x] Preserve `TaskPriority` separately from topo layer.
- [x] Preserve deterministic plan ordering.

**Tests to add:**

- [x] `tests/Core/Test.Core.GraphCompiler.cpp`
  - [x] Empty graph compiles.
  - [x] Single-node graph compiles.
  - [x] Independent nodes produce zero dependency edges.
  - [x] Explicit dependencies produce valid topological order.
  - [x] Missing explicit dependency returns `InvalidArgument` or existing equivalent error.
  - [x] Duplicate `TaskId` returns `InvalidArgument`.
  - [x] Priority ordering is stable among ready nodes.
  - [x] `batch` values are actual topological layers.
  - [x] Deterministic ordering repeated over at least 100 compiles.

**Acceptance criteria:**

- [x] Existing scheduler users still compile.
- [x] Public API churn is minimal and documented.
- [x] New tests are registered in `tests/Core/CMakeLists.txt`.

## T011 — Implement `ResourceHazardBuilder`

**Goal:** Centralize resource dependency edge construction for CPU/streaming-style graphs.

**Required behavior:**

For each resource, maintain:

```cpp
struct ResourceState {
    uint32_t LastWriter = InvalidNode;
    SmallVector<uint32_t, 4> CurrentReaders;
};
```

Hazard rules:

- [x] `Read(node, R)`:
  - [x] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [x] Add `node` to `CurrentReaders(R)`.
- [x] `WeakRead(node, R)`:
  - [x] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [x] Do not add `node` to `CurrentReaders(R)`.
- [x] `Write(node, R)`:
  - [x] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [x] For each reader in `CurrentReaders(R)`, emit `reader -> node`.
  - [x] Clear `CurrentReaders(R)`.
  - [x] Set `LastWriter(R) = node`.
- [x] Deduplicate emitted edges.
- [x] Keep RAR parallelism: two pure readers of the same resource must not create an edge.

**Tests to add in `Test.Core.ResourceHazards.cpp` or `Test.Core.GraphCompiler.cpp`:**

- [x] RAW: write then read serializes writer before reader.
- [x] WAW: write then write serializes first writer before second writer.
- [x] WAR: read then write serializes reader before writer.
- [x] RAR: read then read remains parallel/same layer.
- [x] WeakRead then Write does not force writer to wait for weak reader.
- [x] Write then WeakRead forces weak reader after writer.
- [x] Multiple readers then writer emits all reader edges.
- [x] Duplicate accesses do not duplicate edge count.
- [ ] 10,000-node hazard stress test completes deterministically.

**Acceptance criteria:**

- [x] `PendingTaskDesc::resources` affects scheduling.
- [x] `DomainTaskGraph` no longer ignores resource declarations.
- [x] Edge count in `ScheduleStats` includes hazard edges.

## T012 — Implement real label signal/wait handling

**Goal:** Labels must be first-class dependency constructs, not fake resources.

**Required API/behavior:**

- [x] Add or preserve `Signal(LabelId/StringID)`.
- [x] Add or preserve `WaitFor(LabelId/StringID)`.
- [x] A wait must depend on all earlier signalers of the same label.
- [x] Multiple signalers are allowed as fan-in, but diagnostics must make this visible when useful.
- [x] A wait with no known signaler must not crash. Choose and document one behavior:
  - [x] either compile error; or
  - [ ] unresolved wait is ignored with a warning; or
  - [ ] wait is bound when a later signal appears.
- [x] The chosen behavior must be covered by tests.

**Recommended behavior:**

- Wait depends on all signalers registered before the wait in recording order.
- Waiting before any signal is a compile-time `InvalidState` unless explicitly marked optional.

**Tests:**

- [x] Signal before wait orders signaler before waiter.
- [x] Multiple signalers before wait produce fan-in.
- [x] Independent labels do not interfere.
- [x] Signal after wait follows documented behavior.
- [ ] Label cycle reports `InvalidState` and includes pass names in diagnostics.

## T013 — Add cycle diagnostics with pass/resource context

**Goal:** Cycle failures must be debuggable.

**Required behavior:**

- [x] When topological compilation fails, find at least one cycle.
- [x] Emit pass/task names in the diagnostic.
- [ ] Include edge reason when available:
  - [x] explicit dependency;
  - [x] RAW/WAW/WAR resource hazard;
  - [ ] label wait/signal;
  - [ ] domain-specific reason reserved for GPU render graph.
- [x] Return the existing error convention, typically `ErrorCode::InvalidState`.
- [x] Keep diagnostics available in logs and optionally in `ScheduleStats`/debug info.

**Tests:**

- [x] Explicit A -> B -> A cycle returns invalid state.
- [x] Resource-derived cycle, if constructible through explicit plus hazard edges, includes both pass names.
- [ ] Label-derived cycle includes label name or label ID.
- [ ] Cycle diagnostic does not allocate unbounded memory on large graphs.

## T014 — Refactor `DomainTaskGraph` through shared compiler

**Goal:** Make raw `PendingTaskDesc` scheduling use the shared hazard/compiler path.

**Tasks:**

- [x] Route explicit dependencies into compiler edges.
- [x] Route `PendingTaskDesc::resources` into `ResourceHazardBuilder`.
- [x] Preserve priority and queue-budget lane assignment.
- [x] Preserve stable deterministic ready-set ordering.
- [ ] Report accurate `ScheduleStats`:
  - [x] task count;
  - [x] explicit edge count;
  - [x] hazard edge count;
  - [x] total edge count;
  - [x] topological layer count;
  - [x] critical path cost;
  - [x] max ready queue width.

**Tests:**

- [ ] Existing `Test.Core.DagScheduler.cpp` still passes.
- [x] Add raw `DomainTaskGraph` tests for resource hazards.
- [x] Add stats tests checking edge counts and layer counts.
- [x] Add lane assignment tests for CPU, GPU, and Streaming budgets.

## T015 — Refactor `TaskGraph` through shared compiler

**Goal:** Closure-oriented `TaskGraph` must share the same dependency engine as `DomainTaskGraph`.

**Tasks:**

- [ ] Preserve `AddPass(name, setup_fn, execute_fn)` API.
- [ ] Preserve builder functions for typed reads/writes.
- [ ] Preserve `ReadResource`, `WriteResource`, `WaitFor`, and `Signal`.
- [ ] Store pass metadata needed by compiler.
- [ ] `Compile()` builds immutable compiled graph/layers.
- [ ] `BuildPlan()` returns plan in compiler order.
- [ ] `ExecutePass(uint32_t)` validates compiled state and index.
- [ ] `TakePassExecute(uint32_t)` remains safe for streaming handoff until streaming executor replaces this pattern.
- [ ] `Reset()` is illegal or debug-asserted while execution token is live.
- [ ] `GetScheduleStats()` returns meaningful stats, not only task count.

**Tests:**

- [ ] Existing `Test.Core.TaskGraph.cpp` still passes.
- [ ] Closure execution order follows resource hazards.
- [ ] `BuildPlan()` batches match topological layers.
- [ ] `Reset()` clears resources, labels, stats, and pass closures.
- [ ] `TakePassExecute()` moves only the target pass closure and leaves other passes valid.
- [ ] Reusing graph across epochs does not leak labels/resources from prior epoch.

**Review gate RG-01 — Core compiler substrate:**

- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] No public Core API change is undocumented.
- [ ] No GPU-specific type has entered `Core.Dag.*`.
- [ ] `PendingTaskDesc::resources` is verified by tests to affect scheduling.
- [ ] Cycle diagnostics include names.
- [ ] Determinism test passes under repeated runs.

---

# Phase 2 — CPU `TaskGraph` and `FrameGraph` parallel execution

## T020 — Add pass options and execution metadata

**Goal:** Give CPU graph passes enough metadata for robust scheduling and diagnostics.

**API to add:**

```cpp
struct TaskPassOptions {
    TaskPriority Priority = TaskPriority::Normal;
    uint32_t EstimatedCost = 1;
    bool MainThreadOnly = false;
    bool AllowParallel = true;
    std::string_view DebugCategory = {};
};

struct FrameGraphPassOptions {
    TaskPriority Priority = TaskPriority::Normal;
    uint32_t EstimatedCost = 1;
    bool MainThreadOnly = false;
    bool AllowParallel = true;
    std::string_view DebugCategory = {};
};
```

**Tasks:**

- [x] Add options overloads without breaking old overloads.
- [x] Thread options into `PendingTaskDesc`/compiler metadata.
- [x] Include options in schedule stats/debug dumps.
- [x] Validate `EstimatedCost >= 1` or clamp to 1.

**Tests:**

- [x] Default overload compiles and behaves as before.
- [x] Explicit priority affects ready-node ordering.
- [x] Estimated cost affects critical-path ordering.
- [x] MainThreadOnly flag is preserved in compiled plan.

## T021 — Add ECS/frame structural safety declarations

**Goal:** Prevent unsafe parallel ECS structural mutations.

**FrameGraph builder additions:**

- [x] `StructuralRead()`
- [x] `StructuralWrite()`
- [x] `CommitWorld()` or `CommitTick()` dependency token
- [x] `ReadResource(ResourceId/StringID)` and `WriteResource(ResourceId/StringID)` if not already present at FrameGraph level

**Required built-in resource tokens:**

- [x] `RegistryStructureToken`
- [x] `SceneCommitToken`
- [x] `RenderExtractionToken`

**Rules:**

- [ ] Entity create/destroy and component add/remove systems must declare `StructuralWrite()`.
- [ ] Systems that inspect entity/component membership declare `StructuralRead()`.
- [ ] Pure component data reads/writes use typed `Read<T>()` / `Write<T>()`.
- [ ] Extraction depends on commit token, not incidental pass order.

**Tests:**

- [x] Two structural reads can share a layer.
- [x] Structural write serializes after prior structural reads.
- [x] Structural write serializes before later structural reads.
- [x] Typed component writes do not unnecessarily serialize unrelated typed component reads.
- [x] Commit token creates required phase boundary.

## T022 — Implement graph-local dependency-ready CPU executor

**Goal:** Replace sequential layer execution with dependency-ready dispatch through `Core.Tasks`.

**Execution model:**

- [x] Compile graph if needed.
- [x] Copy initial indegrees into per-execution atomic counters.
- [x] Dispatch all root nodes that are eligible for worker execution.
- [x] On pass completion, decrement successors.
- [x] When a successor reaches zero, dispatch it.
- [x] Signal a graph-local `CounterEvent` or equivalent only when this graph is complete.
- [x] Wait on the graph-local completion primitive.
- [x] Do not wait for unrelated global work.

**Suggested execution state:**

```cpp
struct ExecutionState {
    std::span<const uint32_t> InitialIndegrees;
    std::span<const uint32_t> Successors;
    std::unique_ptr<std::atomic_uint32_t[]> RemainingDeps;
    std::unique_ptr<std::atomic_uint8_t[]> Dispatched;
    Core::Tasks::CounterEvent Done;
};
```

**Tasks:**

- [x] Add `TaskGraphExecutionToken` or internal execution guard.
- [x] Add deterministic sequential fallback when scheduler unavailable or worker count is one.
- [x] Add fail-safe if a pass closure is missing.
- [x] Ensure closures are value-captured/moved safely and not referenced after reset.
- [x] Prevent recursive lambda lifetime hazards.
- [x] Add pass-level telemetry timings if existing telemetry supports it.

**Tests:**

- [x] Independent passes run concurrently. Use atomics/barriers to prove overlap.
- [x] Dependent passes never overlap incorrectly.
- [x] Graph-local wait returns after graph work, while an unrelated long worker job continues running.
- [x] Sequential fallback produces identical side-effect order for dependent passes.
- [x] Missing closure either no-ops or returns documented error; test chosen behavior.
- [x] Repeated execute/reset loops do not leak or use-after-free.

## T023 — Main-thread-only pass support

**Goal:** Support passes that must run on the main thread without breaking worker parallelism.

**First implementation:**

- [x] Layer fallback: for each layer, dispatch worker-eligible passes; execute main-thread-only passes on main thread; wait for that layer before advancing.

**Better optional follow-up:**

- [ ] Main-thread ready queue: workers continue while main thread pumps ready main-thread tasks.

**Required behavior:**

- [x] A `MainThreadOnly` pass must execute on the thread that called `Execute()`.
- [x] Other passes in the same independent layer may run on workers if safe.
- [x] Main-thread pass dependencies are respected.

**Tests:**

- [x] Capture caller thread ID and assert main-thread-only pass runs there.
- [x] Independent worker pass can overlap with main-thread pass if executor supports that mode, or document layer-conservative behavior.
- [x] Dependent pass waits for main-thread-only predecessor.

## T024 — Route `FrameGraph::Execute()` through the new executor

**Goal:** Make `FrameGraph` a thin ECS façade over the completed CPU `TaskGraph` executor.

**Tasks:**

- [x] Add FrameGraph pass options overloads.
- [x] Thread typed component access through shared resource tokens.
- [x] Implement structural tokens.
- [x] Ensure `FrameGraph::Compile()`, `Execute()`, and `Reset()` preserve lifecycle invariants.
- [x] Expose schedule stats via `FrameGraph`.

**Tests:**

- [x] Existing `Test.Core.GraphInterfaces.cpp` still passes.
- [x] Existing FrameGraph tests still pass.
- [x] Add `tests/Core/Test.Core.FrameGraphParallel.cpp`:
  - [x] typed read/read parallelism;
  - [x] typed write/read serialization;
  - [x] typed read/write serialization;
  - [x] structural write global serialization;
  - [x] commit/extraction barrier ordering;
  - [x] main-thread-only pass behavior;
  - [x] graph-local wait behavior.

## T025 — CPU graph stress and performance tests

**Goal:** Catch hidden O(N^2) or allocation regressions.

**Tests:**

- [x] 1,000-pass mixed hazard graph compiles under a reasonable threshold.
- [x] 10,000 independent pass graph compiles deterministically.
- [x] Wide graph execution completes and uses more than one worker when available.
- [x] Deep graph execution follows exact dependency chain.
- [x] Reusing a graph for 1,000 epochs does not grow retained memory beyond expected high-water marks.

**Review gate RG-02 — CPU FrameGraph execution:**

- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] CPU graph no longer executes all passes sequentially when workers are available.
- [ ] There is a deterministic single-thread fallback.
- [ ] No global scheduler wait is used as the normal graph wait.
- [ ] Structural ECS mutation hazards are represented by explicit resources.
- [ ] Pass options and stats are documented.

---

# Phase 3 — Persistent async streaming executor

## T030 — Decide streaming executor home and module shape

**Goal:** Place streaming execution in the right layer.

**Preferred module:**

- [x] `src_new/Runtime/Runtime.StreamingExecutor.cppm`
- [x] `src_new/Runtime/Runtime.StreamingExecutor.cpp`

**Alternative if asset-specific:**

- [ ] `src_new/Assets/Asset.StreamingExecutor.*`

**Decision rule:**

- [x] Put generic continuous executor mechanics in `Runtime`.
- [x] Put asset-specific decode/load pipeline construction in `Assets`.
- [x] Do not put asset-specific state machines inside `Core`.

**Acceptance criteria:**

- [x] The module depends only on allowed lower layers.
- [x] The location is documented in the relevant README.

## T031 — Implement persistent streaming task table

**Goal:** Streaming work must survive across frames and not be destroyed by frame-local graph reset.

**Types:**

```cpp
enum class StreamingTaskState {
    Pending,
    Ready,
    Running,
    WaitingForMainThreadApply,
    WaitingForGpuUpload,
    Complete,
    Failed,
    Cancelled
};
```

```cpp
struct StreamingTaskDesc {
    std::string Name;
    TaskKind Kind;
    TaskPriority Priority;
    uint32_t EstimatedCost;
    uint64_t CancellationGeneration;
    std::span<const StreamingTaskHandle> DependsOn;
    std::move_only_function<StreamingResult()> Execute;
    std::move_only_function<void(StreamingResult&&)> ApplyOnMainThread;
};
```

**API:**

```cpp
class StreamingExecutor {
public:
    StreamingTaskHandle Submit(StreamingTaskDesc desc);
    void Cancel(StreamingTaskHandle handle);
    void PumpBackground(uint32_t maxLaunches);
    void DrainCompletions();
    void ApplyMainThreadResults();
    void ShutdownAndDrain();
};
```

**Tasks:**

- [x] Implement stable handles with generation validation.
- [x] Implement persistent storage with state transitions.
- [x] Implement dependency counters.
- [x] Implement priority-ready queues.
- [ ] Implement cancellation generation checks.
- [x] Implement worker completion queue.
- [x] Implement main-thread apply queue.
- [x] Implement shutdown drain/cancel semantics.

**Tests:**

- [x] Submitted task remains pending across frames until pumped.
- [x] Dependency chain spans frames.
- [x] Higher-priority ready task launches before lower-priority ready task.
- [x] Cancelling a pending task prevents execution.
- [ ] Cancelling a running task suppresses stale apply.
- [ ] Generation mismatch prevents stale result publication.
- [x] `ApplyOnMainThread` runs on caller thread of `ApplyMainThreadResults()`.
- [ ] Shutdown drains or cancels all running work deterministically.

## T032 — Replace frame-local streaming graph tick

**Goal:** Remove the pattern of compiling streaming work, moving closures, resetting graph, and relying on detached worker execution.

**Tasks:**

- [ ] Locate current `Engine::TickStreamingGraph()` or equivalent.
- [ ] Replace frame-local streaming graph execution with `StreamingExecutor` calls.
- [ ] Phase 10 should call:
  - [ ] collect completed transfers;
  - [ ] drain streaming completions;
  - [ ] apply main-thread results;
  - [ ] tick asset service/state machines;
  - [ ] pump a bounded number of background launches.
- [ ] Keep old `GetStreamingGraph()` only as a temporary compatibility shim if required, and mark it deprecated.
- [ ] Ensure shutdown calls `StreamingExecutor::ShutdownAndDrain()` before `Core.Tasks::Scheduler::Shutdown()`.

**Tests:**

- [ ] Add or create `tests/Runtime/Test.Runtime.StreamingExecutor.cpp`.
- [ ] Runtime maintenance phase applies completed streaming result once.
- [ ] Streaming job cannot publish after engine shutdown begins.
- [ ] Long streaming job does not block render extraction.
- [ ] Frame-local reset cannot invalidate running streaming closures.

## T033 — Add asset/GPU upload handoff hooks without overreaching

**Goal:** Prepare streaming executor for asset decode and GPU upload without implementing unrelated asset features.

**Tasks:**

- [ ] Define a result type that can represent CPU payload ready, failed load, or upload request.
- [ ] Ensure worker thread does not create/destroy GPU resources directly.
- [ ] Provide main-thread handoff API that a future `GpuAssetCache` can consume.
- [ ] Do not invent a second transfer queue or staging manager.
- [ ] If `GpuAssetCache` already exists, wire non-blocking request/result behavior through it; otherwise add TODO stubs and tests for executor behavior only.

**Tests:**

- [ ] Worker result enqueues main-thread upload request callback.
- [ ] Upload request callback can be skipped safely if task was cancelled.
- [ ] Failed worker result moves task to `Failed` and does not call upload callback.

**Review gate RG-03 — Streaming executor:**

- [ ] Runtime or Assets focused tests pass.
- [ ] `ExtrinsicCoreTests` still passes.
- [ ] `IntrinsicTests` passes.
- [ ] Streaming jobs persist across frames.
- [ ] Cancellation suppresses stale apply.
- [ ] Shutdown order is deterministic.
- [ ] Worker code cannot mutate ECS or GPU resources directly through the executor API.

---

# Phase 4 — `Graphics.RenderGraph` scaffold

## T040 — Add `Graphics.RenderGraph` module and CMake wiring

**Goal:** Create a GPU-specific render graph module in `Graphics`, not `Core`.

**Files to add:**

- [ ] `src_new/Graphics/Graphics.RenderGraph.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Resources.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Pass.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Compiler.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Barriers.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.TransientAllocator.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Executor.cppm`
- [ ] Matching `.cpp` implementation units.
- [ ] Update `src_new/Graphics/CMakeLists.txt`.
- [ ] Update `src_new/Graphics/README.md`.
- [ ] Add tests to `tests/Graphics/CMakeLists.txt`.

**Layering:**

- [ ] May import `Extrinsic.Core` graph compiler/scheduler types as needed.
- [ ] May import `Extrinsic.RHI.*`.
- [ ] May import `Extrinsic.Graphics.RenderWorld` and `GpuWorld` read-only interfaces.
- [ ] Must not import `ECS`.
- [ ] Must not expose Vulkan backend types in public RenderGraph API.

**Acceptance criteria:**

- [ ] Empty render graph compiles and resets.
- [ ] Module inventory regeneration succeeds.

## T041 — Implement virtual resource model

**Goal:** Support imported and transient textures/buffers.

**Public handles:**

- [ ] `TextureRef`
- [ ] `BufferRef`

**Resource APIs:**

```cpp
TextureRef ImportBackbuffer(std::string_view name, RHI::TextureHandle handle);
TextureRef ImportTexture(std::string_view name, RHI::TextureHandle handle, TextureState initial);
BufferRef  ImportBuffer(std::string_view name, RHI::BufferHandle handle, BufferState initial);

TextureRef CreateTexture(std::string_view name, const TextureDesc& desc);
BufferRef  CreateBuffer(std::string_view name, const BufferDesc& desc);
```

**Tasks:**

- [ ] Implement stable resource handles with generation.
- [ ] Track imported vs transient resources.
- [ ] Track initial/final states for imported resources.
- [ ] Store texture/buffer descriptors.
- [ ] Validate resource ref generation on every access.
- [ ] Keep debug names for diagnostics.

**Tests:**

- [ ] Import backbuffer returns valid texture ref.
- [ ] Imported texture cannot be alias-allocated.
- [ ] Transient texture lifetime is empty before use.
- [ ] Invalid generation fails validation.
- [ ] Duplicate debug names are allowed or rejected according to documented behavior.

## T042 — Implement render pass builder and access declarations

**Goal:** Passes declare all resource usage through a builder.

**API sketch:**

```cpp
class RenderGraphBuilder {
public:
    TextureRef Read(TextureRef, TextureUsage usage);
    TextureRef Write(TextureRef, TextureUsage usage);
    BufferRef  Read(BufferRef, BufferUsage usage);
    BufferRef  Write(BufferRef, BufferUsage usage);

    void SetQueue(RenderQueue queue);
    void SetRenderPass(const RHI::RenderPassDesc& desc);
    void SideEffect();
};
```

**Usage enums:**

- [ ] `TextureUsage::ColorAttachmentRead`
- [ ] `TextureUsage::ColorAttachmentWrite`
- [ ] `TextureUsage::DepthRead`
- [ ] `TextureUsage::DepthWrite`
- [ ] `TextureUsage::ShaderRead`
- [ ] `TextureUsage::ShaderWrite`
- [ ] `TextureUsage::TransferSrc`
- [ ] `TextureUsage::TransferDst`
- [ ] `TextureUsage::Present`
- [ ] `BufferUsage::IndirectRead`
- [ ] `BufferUsage::IndexRead`
- [ ] `BufferUsage::VertexRead` if needed
- [ ] `BufferUsage::ShaderRead`
- [ ] `BufferUsage::ShaderWrite`
- [ ] `BufferUsage::TransferSrc`
- [ ] `BufferUsage::TransferDst`
- [ ] `BufferUsage::HostReadback`

**Tests:**

- [ ] Pass with declared read compiles.
- [ ] Pass with declared write compiles.
- [ ] Pass cannot use invalid resource ref.
- [ ] Pass cannot write an imported read-only/present-only resource unless imported as writable.
- [ ] Side-effect pass is not culled.

## T043 — Implement RenderGraph compile validation and pass DAG

**Goal:** Compile render passes into valid topological order.

**Compile phases:**

- [ ] Validate all resource refs.
- [ ] Validate pass render attachment declarations match declared writes.
- [ ] Validate present pass targets imported backbuffer.
- [ ] Build producer/consumer dependency edges:
  - [ ] write -> read;
  - [ ] write -> write;
  - [ ] read -> write;
  - [ ] explicit pass dependency, if API supports it;
  - [ ] queue handoff placeholder edge, even if only one queue exists initially.
- [ ] Topologically sort passes using shared compiler substrate or a Graphics-specific wrapper around it.
- [ ] Detect cycles with pass/resource names.
- [ ] Build immutable compiled plan.

**Tests:**

- [ ] Two independent passes can remain same layer or adjacent deterministic order.
- [ ] Write color then shader-read creates dependency.
- [ ] Read then write creates dependency.
- [ ] Write then write creates dependency.
- [ ] Invalid present target fails validation.
- [ ] Missing resource declaration fails if execution tries to resolve it.
- [ ] Cycle reports pass names.

## T044 — Implement basic lifetime analysis and pass culling

**Goal:** Know first/last use and remove unused passes safely.

**Tasks:**

- [ ] Compute first use and last use for every virtual resource.
- [ ] Mark imported resources as external lifetime.
- [ ] Mark side-effect passes as roots for reachability.
- [ ] Mark present/backbuffer final use as side-effect root.
- [ ] Cull passes whose outputs are never consumed and which have no side effects.
- [ ] Do not cull passes that write imported resources.

**Tests:**

- [ ] Unused transient-producing pass is culled.
- [ ] Side-effect pass remains.
- [ ] Present chain keeps all producer passes.
- [ ] Imported-resource writer remains.
- [ ] Lifetime first/last pass indices are correct.

## T045 — Implement coarse barrier packet generation

**Goal:** Generate resource transitions from declared usage.

**Tasks:**

- [ ] Map `TextureUsage` to abstract texture state/layout/access.
- [ ] Map `BufferUsage` to abstract buffer access/stage.
- [ ] For every resource use transition, emit barrier packet before consuming pass.
- [ ] Collapse redundant no-op transitions.
- [ ] Support imported initial state.
- [ ] Support imported final/present state.
- [ ] Implement conversion to current RHI coarse `TextureBarrier` / `BufferBarrier` calls where possible.
- [ ] Keep exact Sync2-style barrier API as a later phase if RHI does not yet expose it.

**Tests:**

- [ ] Undefined/imported -> color attachment write barrier.
- [ ] Color attachment write -> shader read barrier.
- [ ] Depth write -> depth read/shader read barrier.
- [ ] Compute shader write -> indirect read barrier.
- [ ] Transfer dst -> shader read barrier.
- [ ] Color/shader read -> present barrier for backbuffer.
- [ ] Redundant read -> read transition emits no barrier.

## T046 — Implement RenderGraph execution context and null backend behavior

**Goal:** Make graph execution callable before full Vulkan backend support.

**API sketch:**

```cpp
class RenderGraphContext {
public:
    RHI::ICommandContext& Commands();
    RHI::IDevice& Device();
    const RenderWorld& World();

    RHI::TextureHandle Resolve(TextureRef);
    RHI::BufferHandle Resolve(BufferRef);
};
```

**Tasks:**

- [ ] Allocate/resolve imported resources.
- [ ] For first scaffold, transient resources may be logical-only or allocated through existing texture/buffer managers if available.
- [ ] Execute passes in compiled order.
- [ ] Emit barrier calls before each pass.
- [ ] Null backend should record/observe calls enough for tests without requiring a real GPU.
- [ ] Failed compile prevents execute.

**Tests:**

- [ ] Execute empty graph succeeds.
- [ ] Execute simple present chain in mock/null backend records expected pass order.
- [ ] Barrier packets are visible to tests.
- [ ] Resource resolution fails for invalid ref.

**Review gate RG-04 — RenderGraph scaffold:**

- [ ] `ExtrinsicGraphicsTests` passes.
- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] `Graphics.RenderGraph` has no ECS dependency.
- [ ] Core still has no GPU resource/barrier semantics.
- [ ] RenderGraph compile order comes from declared resources, not hardcoded pass order.
- [ ] Barriers are generated from usage transitions.

---

# Phase 5 — Renderer integration with RenderGraph

## T050 — Renderer owns and resets a RenderGraph per frame

**Goal:** Move GPU pass ordering into `Graphics.RenderGraph` inside the renderer.

**Tasks:**

- [ ] Add `RenderGraph` member to renderer implementation.
- [ ] In `ExecuteFrame`, reset graph, import frame/backbuffer resources, add passes, compile, execute.
- [ ] Keep `Runtime::Engine` unaware of pass-level details.
- [ ] Renderer `BeginFrame`/`EndFrame` lifecycle remains unchanged unless documented.
- [ ] Resize invalidates/recreates graph-owned transient resources as needed.

**Tests:**

- [ ] Null renderer/frame path compiles and executes graph.
- [ ] Resize causes transient resource invalidation without stale handle use.
- [ ] Runtime smoke test confirms `RunFrame` does not inspect graph resources.

## T051 — Register initial fixed pass sequence through graph builders

**Goal:** Convert fixed-order render code into graph pass registration.

**Passes to register, feature-gated where appropriate:**

- [ ] Compute prologue / scene update / culling.
- [ ] Picking pass, conditional on pending pick.
- [ ] Optional depth prepass.
- [ ] G-buffer pass.
- [ ] Deferred lighting pass.
- [ ] Forward surface pass.
- [ ] Forward line pass.
- [ ] Forward point pass.
- [ ] Overlay surface/debug pass if present.
- [ ] Bloom pass.
- [ ] Tone-map pass.
- [ ] FXAA/SMAA pass.
- [ ] Selection outline pass.
- [ ] Debug view pass.
- [ ] ImGui pass.
- [ ] Present pass.

**Rules:**

- [ ] Every pass declares all texture/buffer reads/writes.
- [ ] No pass relies on registration order for required correctness.
- [ ] Registration order is only a stable tie-break.
- [ ] Fixed sequence may remain as builder call order, but dependencies/barriers must come from resource declarations.

**Tests:**

- [ ] Expected pass names appear in compiled graph when features enabled.
- [ ] Conditional picking pass absent when no pick request.
- [ ] G-buffer outputs feed deferred lighting.
- [ ] Deferred lighting feeds postprocess.
- [ ] Postprocess feeds present.
- [ ] Selection outline reads entity ID/depth resources.
- [ ] Pass culling removes disabled/unused optional passes.

## T052 — Import persistent GPU world resources into RenderGraph

**Goal:** Persistent buffers/textures from `GpuWorld` and systems must enter graph as imported resources.

**Tasks:**

- [ ] Import scene/instance/entity/material/light buffers as read resources.
- [ ] Import indirect draw buffers as read/write as appropriate.
- [ ] Import shadow atlas if present.
- [ ] Import selection readback buffers if present.
- [ ] Ensure imported resource states are specified.
- [ ] Ensure graph does not own imported lifetimes.

**Tests:**

- [ ] Imported buffers are not destroyed by graph reset.
- [ ] Imported buffers cannot alias transient buffers.
- [ ] Culling write -> draw indirect read barrier is generated.
- [ ] Lighting pass reads light buffer.

## T053 — Renderer integration telemetry and diagnostics

**Goal:** Make render-graph failures obvious and measurable.

**Tasks:**

- [ ] Log compile failures with pass/resource names.
- [ ] Expose per-frame graph stats:
  - [ ] pass count;
  - [ ] culled pass count;
  - [ ] resource count;
  - [ ] barrier count;
  - [ ] transient memory estimate;
  - [ ] compile time;
  - [ ] execute/record time.
- [ ] Add debug dump function for compiled render graph.

**Tests:**

- [ ] Compile failure log contains failing pass name.
- [ ] Stats are nonzero for nonempty graph.
- [ ] Debug dump contains pass order and resources.

**Review gate RG-05 — Renderer integration:**

- [ ] `ExtrinsicGraphicsTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] Renderer `ExecuteFrame` builds and runs a `Graphics.RenderGraph`.
- [ ] Runtime does not know render pass names or GPU resources.
- [ ] Null/headless path remains testable.
- [ ] Pass order is test-verified by graph output, not only by code inspection.

---

# Phase 6 — Precise GPU barriers, transient resources, aliasing

## T060 — Extend RHI barrier API if needed

**Goal:** Move from coarse barriers to explicit Sync2-style barrier packets where backend supports it.

**Tasks:**

- [ ] Define backend-agnostic barrier packet structs in RHI or Graphics/RHI-facing layer:
  - [ ] texture/image barrier;
  - [ ] buffer barrier;
  - [ ] memory barrier if needed.
- [ ] Include source/destination stages, access masks, old/new layouts, queue ownership if supported.
- [ ] Add command-context method to submit a batch of barriers.
- [ ] Keep fallback mapping to existing coarse API for null/unsupported backends.
- [ ] Implement Vulkan backend mapping.

**Tests:**

- [ ] RHI unit tests verify packet fields survive to mock command context.
- [ ] Vulkan mapping unit tests if backend test hooks exist.
- [ ] Existing coarse barrier tests still pass.

## T061 — Implement transient resource allocation

**Goal:** Allocate render graph-created transient textures/buffers from graph lifetime analysis.

**Tasks:**

- [ ] Build transient resource allocation requests from descriptors and first/last use.
- [ ] Allocate actual RHI resources before execution.
- [ ] Free/return transient resources after frame or keep in a reusable cache.
- [ ] Handle resize by invalidating incompatible cached resources.
- [ ] Never allocate imported resources.
- [ ] Add memory estimate/statistics.

**Tests:**

- [ ] Transient resources are allocated before first use.
- [ ] Transient resources are released/recycled after graph reset/frame completion.
- [ ] Resize invalidates resources with old extent.
- [ ] Imported resources are never allocated or freed by transient allocator.

## T062 — Implement aliasing of compatible transient resources

**Goal:** Reuse memory for non-overlapping transient resources.

**Rules:**

- [ ] Resources may alias only when lifetimes do not overlap.
- [ ] Descriptors must be compatible.
- [ ] Imported resources never alias.
- [ ] Aliasing boundaries must emit required discard/transition barriers.
- [ ] Aliasing must be optional behind a config flag for debugging.

**Tests:**

- [ ] Non-overlapping compatible textures alias.
- [ ] Overlapping compatible textures do not alias.
- [ ] Non-compatible descriptors do not alias.
- [ ] Imported resource does not alias.
- [ ] Alias on/off produces same logical pass output order and barrier correctness.

## T063 — Vulkan validation and GPU integration tests

**Goal:** Confirm render graph barriers and lifetimes are valid under real backend validation.

**Tests:**

- [ ] G-buffer -> lighting -> postprocess -> present frame passes Vulkan validation.
- [ ] Depth write -> shader read transition passes validation.
- [ ] Compute write -> indirect draw read transition passes validation.
- [ ] Swapchain present transition passes validation.
- [ ] Transient aliasing enabled passes validation.
- [ ] Tests are skipped gracefully when Vulkan validation backend is unavailable.

**Review gate RG-06 — Production GPU graph mechanics:**

- [ ] Graphics tests pass in null backend.
- [ ] Vulkan validation tests pass or skip cleanly in unsupported environments.
- [ ] Barriers are generated from declared usages.
- [ ] Aliasing is correct and can be disabled.
- [ ] Resize behavior is tested.
- [ ] No pass emits ad-hoc barriers outside RenderGraph except documented backend internals.

---

# Phase 7 — Runtime phase integration and render-prep graph

## T070 — Keep `Engine::RunFrame` broad-phase-only

**Goal:** Preserve the intended frame loop shape.

**Tasks:**

- [ ] Verify `RunFrame` consists only of broad phases:
  - [ ] platform/event handling;
  - [ ] fixed-step simulation;
  - [ ] variable tick;
  - [ ] render input/snapshot;
  - [ ] renderer begin;
  - [ ] render extraction;
  - [ ] render prepare;
  - [ ] render execute;
  - [ ] end/present;
  - [ ] maintenance/streaming rendezvous;
  - [ ] clock end.
- [ ] Remove or avoid any pass-level renderer logic from Runtime.
- [ ] Ensure streaming rendezvous happens in maintenance.
- [ ] Ensure GPU transfers are collected before asset state flips to ready.
- [ ] Update Runtime README if phase ordering changes.

**Tests:**

- [ ] Runtime frame-loop test with fakes verifies phase order.
- [ ] Resize path still drains/resizes/acknowledges in order.
- [ ] Minimized window path does not tick sim/render.
- [ ] Maintenance runs after present/end-frame.

## T071 — Convert render-prep work to CPU task graph where useful

**Goal:** Use CPU graph for render-prep jobs without prematurely parallelizing unsafe work.

**Candidate jobs:**

- [ ] frustum cull prep;
- [ ] draw packet sort;
- [ ] LOD selection;
- [ ] staging upload preparation;
- [ ] CPU-side visibility list building;
- [ ] debug draw packet freezing.

**Tasks:**

- [ ] Add a render-prep `TaskGraph` owned by renderer or frame context.
- [ ] Render-prep graph reads immutable `RenderWorld`.
- [ ] Render-prep graph writes renderer-owned transient prep packets only.
- [ ] Execute and await render-prep graph before GPU RenderGraph execution.
- [ ] Keep render-prep graph optional/sequential for small scenes.

**Tests:**

- [ ] Render-prep job cannot run before extraction has produced `RenderWorld`.
- [ ] GPU graph cannot execute before render-prep graph completes.
- [ ] Independent render-prep jobs can run in parallel.
- [ ] Render-prep graph does not mutate ECS.

**Review gate RG-07 — Runtime integration:**

- [ ] Runtime tests pass.
- [ ] Core tests pass.
- [ ] Graphics tests pass.
- [ ] Full `IntrinsicTests` pass.
- [ ] `RunFrame` remains broad-phase-only.
- [ ] Streaming, CPU graph, and GPU graph each enter the frame at the documented phase.

---

# Phase 8 — Final docs, module inventory, and cutover audit

## T080 — Regenerate module inventory and sync READMEs

**Tasks:**

- [ ] Run `python3 tools/generate_src_new_module_inventory.py`.
- [ ] Update `docs/architecture/src_new_module_inventory.md`.
- [ ] Update `src_new/Core/README.md` with new graph partitions and public surface.
- [ ] Update `src_new/Graphics/README.md` with `Graphics.RenderGraph` public surface.
- [ ] Update `src_new/Runtime/README.md` with streaming executor/frame phases.
- [ ] Update top-level `TODO.md` if milestones are completed or replaced.

**Acceptance criteria:**

- [ ] Inventory reflects every new `.cppm` module/partition.
- [ ] README public module surfaces match CMake files.
- [ ] Architecture docs and implementation names match.

## T081 — Add post-merge audit checklist result

**Goal:** Make architecture-touching changes safe to merge.

**Audit items:**

- [ ] No new dependency cycle between `Core`, `Assets`, `ECS`, `Graphics`, `Runtime`, `Platform`.
- [ ] `Graphics` does not import `ECS` for RenderGraph.
- [ ] `Runtime` does not inspect GPU resources/barriers.
- [ ] `Core` does not expose GPU layout/barrier semantics.
- [ ] Streaming worker closures cannot mutate ECS/GPU directly.
- [ ] CPU graph uses graph-local wait.
- [ ] Single-thread fallback works.
- [ ] Null/headless paths still pass.
- [ ] Cycle diagnostics include pass/task names.
- [ ] Build/test commands are listed in PR notes.

## T082 — Final end-to-end acceptance tests

**Required test runs:**

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --target IntrinsicTests`
- [ ] `./build/dev/bin/IntrinsicTests`
- [ ] `./build/dev/bin/ExtrinsicCoreTests`
- [ ] `./build/dev/bin/ExtrinsicGraphicsTests`
- [ ] `./build/dev/bin/ExtrinsicRuntimeTests` if Runtime tests were added.
- [ ] Focused graph stress tests.
- [ ] Vulkan validation graph test if backend/environment supports it.
- [ ] Compile hotspot tool if module partition changes caused suspicious build regressions:

```bash
python3 ./tools/compile_hotspots.py \
  --build-dir build \
  --top 40 \
  --json-out build/compile_hotspots_report.json \
  --baseline-json tools/compile_hotspot_baseline.json
```

## T083 — Final cutover criteria

Mark complete only when all are true:

- [ ] `FrameGraph::Execute()` uses dependency-ready graph execution when workers are available.
- [ ] CPU graph has deterministic single-thread fallback.
- [ ] `DagScheduler`, `DomainTaskGraph`, and `TaskGraph` share compiler/hazard logic or have an explicitly documented reason not to.
- [ ] `PendingTaskDesc::resources` affects scheduling.
- [ ] Cycle diagnostics include names and edge reasons.
- [ ] Streaming tasks are persistent across frames.
- [ ] Streaming cancellation prevents stale result publication.
- [ ] Streaming shutdown is deterministic and happens before task scheduler shutdown.
- [ ] `Renderer::ExecuteFrame()` builds and executes a real `Graphics.RenderGraph`.
- [ ] GPU barriers are generated from declared pass resource usage.
- [ ] Render passes declare resources through graph builders.
- [ ] Runtime owns only broad phase orchestration.
- [ ] Tests cover Core graph compile, CPU execution, streaming executor, render graph compile, render graph barriers, and renderer integration.
- [ ] Docs and module inventory are synchronized.

---

# Suggested PR breakdown

## PR 1 — Documentation and audit only

- T000
- T001
- RG-00

## PR 2 — Core graph compiler substrate

- T010
- T011
- T012
- T013
- T014
- T015
- RG-01

## PR 3 — CPU FrameGraph parallel executor

- T020
- T021
- T022
- T023
- T024
- T025
- RG-02

## PR 4 — Persistent streaming executor

- T030
- T031
- T032
- T033
- RG-03

## PR 5 — RenderGraph scaffold

- T040
- T041
- T042
- T043
- T044
- T045
- T046
- RG-04

## PR 6 — Renderer integration

- T050
- T051
- T052
- T053
- RG-05

## PR 7 — Precise barriers and transient resources

- T060
- T061
- T062
- T063
- RG-06

## PR 8 — Runtime integration and render-prep graph

- T070
- T071
- RG-07

## PR 9 — Final docs, inventory, and cutover audit

- T080
- T081
- T082
- T083

---

# Codex working prompt

Use this prompt when starting each PR-sized chunk:

```text
You are implementing the next task-graph milestone in IntrinsicEngine `src_new`.

Constraints:
- Do not copy legacy `src`; use it only for inspiration.
- Preserve `src_new` layering: Core graph primitives, Graphics GPU render graph, Runtime orchestration only.
- Add focused tests before or alongside implementation.
- Keep deterministic fallback behavior.
- Do not move GPU barrier/layout concepts into Core.
- Do not use global scheduler wait for normal graph execution.
- Update CMake, README, and module inventory when modules change.
- Stop at the review gate for this PR and ensure all required tests pass.

Implement the next unchecked TODOs from `codex_task_graph_todos.md` and include the exact validation commands and results in the PR notes.
```

# User-provided custom instructions

### System Prompt

This prompt configures the AI to act as the ultimate authority on both **Geometry Processing Research** and **High-Performance Engine Architecture**.

# ROLE
You are the **Senior Principal Graphics Architect & Distinguished Scientist in Geometry Processing**.
*   **Academic Background:** You hold Ph.D.s in Computer Science and Mathematics, specializing in **Discrete Differential Geometry**, Topology, and Numerical Optimization.
*   **Industry Experience:** You have 20+ years of experience bridging the gap between academic research and AAA game engine architecture (Unreal/Decima) or HPC (CUDA/Scientific Vis).
*   **Superpower:** You do not write "academic code" (slow, pointer-heavy). You translate rigorous mathematical theories into **Data-Oriented, GPU-Driven, Lock-Free C++23**.

# CONTEXT & GOAL
You are designing and implementing a **"Next-Gen Research & Rendering Engine."**
*   **Purpose:** A platform for real-time geometry processing, path tracing, and physics simulation.
*   **Performance Target:** < 2ms CPU Frame Time.
*   **Philosophy:** **"Rigorous Theory, Metal Performance."** Every algorithm must be mathematically sound (robust to degenerate inputs) and computationally optimal (cache-friendly, SIMD/GPU-ready).

## CORE ARCHITECTURE: The 3-Fold Hybrid Task System
1.  **CPU Task Graph (Fiber-Based):** Lock-free work-stealing for gameplay/physics.
2.  **GPU Frame Graph (Transient DAG):** Manages Virtual Resources, aliasing, and Async Compute (Vulkan 1.3 Sync2).
3.  **Async Streaming Graph:** Background priority queues for asset IO and heavy geometric processing (e.g., mesh simplification, remeshing).

# GUIDELINES

## 1. Mathematical & Algorithmic Standards
*   **Formalism:** When introducing geometric algorithms, use **LaTeX** (`$...$` or `$$...$$`) to define the formulation precisely (e.g., minimizing energies, spectral decomposition).
*   **Robustness:** Explicitly handle degenerate cases (zero-area triangles, non-manifold edges). Prefer numerical stability over naive implementations.
*   **Analysis:** Briefly state the Time Complexity ($O(n)$) and Space Complexity of your proposed solutions.

## 2. Engineering & Data-Oriented Design (DOD)
*   **Memory Layout:**
    *   **Struct-of-Arrays (SoA):** Mandatory for hot data (positions, velocities).
    *   **Allocators:** Use `LinearAllocator` (Stack) for per-frame data. No raw `new`/`delete` or `std::shared_ptr` in hot loops.
    *   **Handle-Based Ownership:** Use generational indices (`StrongHandle<T>`) instead of pointers.
*   **GPU-Driven Rendering:**
    *   **Bindless by Default:** Descriptor Indexing.
    *   **Buffer Device Address (BDA):** Raw pointers in shaders.
    *   **Indirect Execution:** CPU prepares packets; GPU drives execution (Mesh Shaders/Compute).

## 3. Coding Standards (Modern C++ & Modules)
*   **Standard:** **C++23**.
    *   Use **Explicit Object Parameters** ("Deducing `this`").
    *   Use **Monadic Operations** (`.and_then`, `.transform`) on `std::expected`.
    *   Use `std::span` and Ranges views over raw pointer arithmetic.
*   **Modules Strategy:**
    *   **Logical Units:** One named module per library (`Core`, `Geometry`).
    *   **Partitions:** `.cppm` for Interface (`export module Core:Math;`), `.cpp` for Implementation (`module Core:Math.Impl;`).
    *   **Headers:** Global Module Fragment (`module;`) only.

# WORKFLOW
1.  **Theoretical Analysis:** Define the problem mathematically. What is the geometric invariant? What is the energy to minimize? (Use LaTeX).
2.  **Architecture Check:** Which Graph handles this? (CPU vs. Compute Shader).
3.  **Data Design:** Define memory layout (SoA vs AoS) for cache coherency.
4.  **Interface (.cppm):** Minimal exports using C++23 features.
5.  **Implementation (.cpp):** SIMD-friendly, branchless logic.
6.  **Verification:** GTest + Telemetry marker.

# OUTPUT FORMAT
Provide code in Markdown blocks. Use the following structure:

**1. Mathematical & Architectural Analysis**
*   *Theory:* $$ E(u) = \int_S |\nabla u|^2 dA $$ (Explain the math/geometry).
*   *Implementation:* "We will solve this using a Parallel Jacobi iteration on the Compute Queue..."

**2. Module Interface Partition (.cppm)**
```cpp
// Geometry.Laplacian.cppm
module;
#include <concepts>
export module Geometry:Laplacian;
// ...
```

**3. Module Implementation Partition (.cpp)**
```cpp
// Geometry.Laplacian.cpp
module Geometry:Laplacian.Impl;
import :Laplacian;
// ... SIMD/GPU optimized implementation
```

**4. Testing & Verification**
```cpp
// Geometry.Tests.Laplacian.cpp
// Verify numerical error convergence
```

**5. Telemetry**
```cpp
// Tracy / Nsight markers
```
