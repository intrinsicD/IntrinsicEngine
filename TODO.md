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

#### B1.0 Reference main loop (canonical migration target)

Use the following loop shape as the canonical reference. Adaptation to current Intrinsic ownership should happen *under* this shape, not by changing the shape itself:

```cpp
while (!app.should_quit())
{
    platform.pump_events();          // window, input, quit, resize
    clock.begin_frame();

    if (app.is_minimized())
    {
        platform.wait_for_events_or_timeout();
        continue;
    }

    // 1) Advance fixed-step simulation
    accumulator += clock.frame_delta_clamped();

    while (accumulator >= fixed_dt)
    {
        input_system.begin_sim_tick();
        simulation.run_fixed_tick(fixed_dt);
        gameplay.run_fixed_tick(fixed_dt);
        physics.run_fixed_tick(fixed_dt);
        animation.run_fixed_tick(fixed_dt);
        world.commit_tick();         // authoritative state becomes consistent
        accumulator -= fixed_dt;
    }

    const double alpha = accumulator / fixed_dt;

    // 2) Build render snapshot from stable world state
    RenderFrameInput render_input{};
    render_input.alpha = alpha;
    render_input.camera = camera_system.interpolate(alpha);
    render_input.viewport = platform.viewport();
    render_input.world_snapshot = world.create_readonly_snapshot();
    render_input.input_snapshot = input_system.render_snapshot();

    // 3) Start frame context
    FrameContext& frame = renderer.begin_frame();

    // 4) Extract immutable render data
    RenderWorld render_world = renderer.extract_render_world(render_input);

    // 5) Prepare render graph/jobs
    renderer.prepare_frame(frame, render_world);

    // 6) Execute GPU frame
    renderer.execute_frame(frame);

    // 7) End frame / retire finished work
    renderer.end_frame(frame);
    resource_system.collect_garbage(renderer.completed_gpu_value());

    clock.end_frame();
}
```

Mapping guidance for current Intrinsic code:

- `platform` maps to `RuntimePlatformFrameHost` + `PlatformFrameCoordinator` (main-thread ownership, pump/minimize, framebuffer extent capture).
- `world` maps to `SceneManager` + authoritative ECS scene ownership, with an explicit `commit_tick()` / readonly snapshot boundary.
- `renderer` maps to `RenderOrchestrator` plus `RenderDriver`, with the runtime render lane now following `begin_frame -> extract_render_world -> prepare_frame -> execute_frame -> end_frame`.
- `resource_system` maps to `Runtime::ResourceMaintenanceService` (GPU sync capture, readback, deferred-destruction, transfer GC, texture/material retirement).
- `RenderFrameInput`, `RenderWorld`, and `FrameContext` are first-class types rather than remaining implicit in `Engine::Run()` / `RenderDriver::OnUpdate(...)`.

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

