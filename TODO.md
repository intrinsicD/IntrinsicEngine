# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** finish the resource-driven rendering refactor and expand lighting paths without painting the engine into a corner for hybrid, transparency, or material-system work.

**Success criteria:**

- Deterministic frame construction with explicit pass/resource dependencies.
- Canonical render targets with validated producer/consumer contracts.
- Selection, post, and debug visualization decoupled from a single lighting path.
- Only recipe-required intermediate resources allocated per frame.
- Migration hardened by graph compile tests, contract tests, and at least one integration test.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical runtime rendering architecture spec (pass contracts, data contracts, invariants).
- `docs/architecture/frame-loop-rollback-strategy.md` — concrete rollback toggle, shim, and pass/fail gates for frame-loop migration phases.
- `docs/architecture/runtime-subsystem-boundaries.md` — current runtime ownership map, dependency directions, and startup/per-frame/shutdown lifecycle.
- `PLAN.md` — archival index for the completed three-pass migration.
- `ROADMAP.md` — medium/long-horizon feature roadmap and phase ordering.
- `README.md` — user-facing architecture summary, build/test entry points, and SLOs.
- `CLAUDE.md` — contributor conventions, C++23 policy, and markdown sync contract.
- `PATTERNS.md` — reusable patterns catalog with canonical examples and usage guidance.

---

## P0 — Critical Refactor

*(All P0 items completed — see git history for implementation details.)*

## 2. Next (P1) — Near-Term Follow-Up After the Refactor Lands

These are not required to finish the first wave, but they should begin soon after P0 is stable.

### B3. Engine Architecture Review Follow-Up (Boundary + Coupling + Migration)

#### B3.1 Current Architecture Map (baseline and keep current)

#### B3.2 Coupling Hotspots (reduce first)

*(No active items — see git history for completed coupling reductions.)*

#### B3.3 Mixed Concerns + Unstable Interfaces

#### B3.4 Barriers to Testing + Evolution

#### B3.5 Hidden Architectural Duplication

#### B3.7 Recommended Path (default = O2) + Migration Plan

O2 remains the default migration path per `docs/architecture/adr-o2-pragmatic-medium-runtime-refactor.md` unless future benchmark/test evidence overturns it.

#### B3.8 Code Review Findings

##### 2026-03-19 commit review

- [ ] Review whether `BuildDefaultPipelineRecipe` rewrite should have been a separate commit from the HTex feature work — behavioral change to all pipeline configs was bundled with a feature commit.

##### 2026-03-20 / 2026-03-21 commit review

###### Critical / Correctness

- [ ] **Vtable anchor policy deviation (563aa1c):** The module-partition-to-separate-module refactor removed all 11 explicit out-of-line destructor definitions for `IAssetLoader`/`IAssetExporter` subclasses from `Graphics.IORegistry.cpp`. CLAUDE.md explicitly requires vtable anchors for pure-virtual base classes in module partition interfaces. Either restore the anchors or update CLAUDE.md to reflect the relaxed policy under Clang 20.
- [ ] **Push constant budget at 120/128 bytes (181d92b):** `MeshPushConstants` grew to 120 bytes with the `PtrIndices` field, leaving only 8 bytes before the Vulkan guaranteed minimum (128). Document this constraint in CLAUDE.md or the render architecture spec so future additions know to migrate to a UBO-backed approach.
- [ ] **Global mutable state for CUDA jobs (8613af5):** `g_PendingCudaJobs` is a file-scope `std::unordered_map` in `Runtime.PointCloudKMeans.cpp`. If `PumpCompletions` and `ScheduleCudaJob` can be called from different threads, this is a data race. Verify main-thread-only access and document the constraint, or encapsulate in a class with explicit thread-safety annotation.

###### Architecture / Pattern Compliance

- [ ] **HtexPatch → KMeans coupling (d58ff17):** `Geometry.HtexPatch` now imports `Geometry.KMeans` for `ClassifyPointToCentroid`, creating a dependency from the patch system to the clustering system. Consider accepting a classification function/span as a parameter to keep the patch module independent.
- [ ] **Duplicated triangle construction logic:** `BuildHalfedgeTriangle()` in `Geometry.HtexPatch.cpp` and similar triangle construction/validation in `Graphics.Passes.HtexPatchPreview.cpp` share near-identical logic. Centralize into a shared utility.
- [ ] **PATTERNS.md stage count drift:** Section 6 states "`DefaultPipeline` registers 9 stages" but the current code registers 10 stages per `rendering-three-pass.md`. Update PATTERNS.md.
- [ ] **Shader code duplication (181d92b):** The Voronoi nearest-vertex BDA lookup logic is copy-pasted between `surface.frag` and `surface_gbuffer.frag` (~30 lines each). Extract to a shared GLSL include.

###### Performance

- [ ] **Per-frame centroid reconstruction (d6d4a1a):** `ReconstructPreviewCentroids()` iterates all mesh vertices twice per call. If called per-frame from the Htex preview path, consider caching centroids and invalidating on KMeans re-run.

###### Process / Hygiene

- [ ] **Bundled unrelated changes in single commits:** `e711573` mixes RHI module refactoring with KMeans rendering fixes. `4e474f2` bundles CUDA-default removal with frame-loop scaffolding. `563aa1c` bundles Material/PostProcess code reorganization with import narrowing. Future commits should separate mechanical refactors from behavioral changes.
- [ ] **CUDA ON/OFF toggling:** `set(INTRINSIC_ENABLE_CUDA ON)` was removed in `4e474f2` then restored in `8613af5`, suggesting overlapping branch development without cleanup.
- [ ] **Narrow-import policy in tests (b746283):** `Test_KMeansLabelRoundTrip.cpp` uses `import Graphics;` (umbrella) rather than narrow per-module imports, contradicting the narrowing policy established in the same commit series.
- [ ] **BUGS.md stale entry:** The `IntrinsicTests` link failure for `BuildDefaultPipelineRecipe` is still listed as active. Verify whether the hybrid lighting commit (2900cf1) resolved this and update status.

### B4. Next-Gen Frame Pipeline Refactor (Fixed-Step + Extraction + Explicit Frame Contexts)

Goal: refactor the runtime from a monolithic update/render loop into a staged frame pipeline with explicit ownership boundaries, immutable render extraction, bounded frames in flight, and explicit CPU/GPU completion tracking. The target shape is: platform -> fixed simulation -> extraction -> render preparation -> GPU submission -> maintenance.

#### B4.0 Target properties (the contract we are designing toward)

- [x] Keep OS/window/input pumping first on the main thread every frame.
- [ ] Run simulation on a fixed timestep and keep rendering variable-rate.
- [ ] Introduce explicit `FrameContext` ownership rather than ad-hoc per-frame global state.
- [ ] Compile/execute the render graph per frame inside renderer-owned lifecycle code.
- [ ] Decouple authoritative game/world state from render-facing state via an extraction boundary.
- [ ] Keep frames in flight bounded (default 2, optional 3 when telemetry justifies it).
- [ ] Treat job-system-driven parallelism as the default for simulation/extraction/render prep work.
- [ ] Make GPU synchronization, frame pacing, and deferred resource retirement explicit architecture concepts.
- [ ] Preserve headless/testable paths by isolating platform + swapchain work from simulation/extraction/maintenance logic.

#### B4.1 Core principle: authoritative world state -> immutable render state

- [ ] Codify the rule that simulation and rendering must not mutate the same live state during a frame.
- [ ] Define the authoritative handoff as:
  - [ ] simulation writes `WorldState N+1`
  - [ ] extraction reads stable `WorldState N+1`
  - [ ] extraction writes immutable `RenderWorld N+1`
  - [ ] rendering consumes only `RenderWorld N+1`
- [ ] Reject designs that let pass recording or render-graph setup walk arbitrary live ECS state after extraction has completed.
- [ ] Add tests that lock the extraction boundary so render work cannot depend on mutation order inside the simulation lane.

#### B4.2 Reference main loop (use this shape as the migration target)

Use the following loop shape as the canonical reference for the refactor. Adaptation to current Intrinsic ownership should happen *under* this shape, not by changing the shape itself:

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

Mapping guidance for current Intrinsic code while preserving that reference shape:

- [ ] `platform` maps to the current window/event/minimize/resize orchestration on the main thread.
- [ ] `world` maps to `SceneManager` + authoritative ECS scene ownership, with a new explicit `commit_tick()` / readonly snapshot boundary.
- [ ] `renderer` maps to `RenderOrchestrator` plus `RenderSystem`, but should evolve toward `begin_frame -> extract_render_world -> prepare_frame -> execute_frame -> end_frame`.
- [ ] `resource_system` maps to the currently split upload-retirement / deferred-destruction responsibilities across `AssetPipeline`, render-side lifetime queues, and future explicit GPU-retirement services.
- [ ] `RenderFrameInput`, `RenderWorld`, and `FrameContext` should become first-class types in the refactor rather than staying implicit in `Engine::Run()` / `RenderSystem::OnUpdate(...)`.

#### B4.3 Platform stage (A)

- [x] Move all window/input/event pumping to the start of the frame on the main thread.
- [ ] Centralize quit, resize, minimize, drag-drop ingest, and timer updates under one platform-stage API.
- [x] Ensure minimized/background behavior uses wait-for-events or throttled policy instead of busy-rendering.
- [ ] Ensure SDL/GLFW event pumping semantics stay main-thread-only and are documented as such.

#### B4.4 Simulation stage (B)

- [ ] Introduce a clamped accumulator policy with explicit constants such as `fixed_dt` and `max_frame_dt`.
- [ ] Default to `1/60` fixed-step simulation; allow `1/120` as a configurable high-frequency mode when justified.
- [ ] Move deterministic gameplay / ECS / physics / AI / animation work onto the fixed-step lane.
- [ ] Define `CommitFixedTick()` semantics so each completed fixed tick produces a consistent authoritative world state.
- [ ] Add telemetry for tick count per frame, clamp hits, and simulation CPU time.

#### B4.5 Extraction stage (C)

- [ ] Introduce an explicit `RenderFrameInput` / `WorldSnapshot` / `RenderWorld` extraction boundary.
- [ ] Ensure extraction is the only place that resolves live ECS state into render packets for the frame.
- [ ] Define immutable packet families for Intrinsic's renderer:
  - [ ] camera/view packets
  - [ ] surface draw packets
  - [ ] line / point / debug draw packets
  - [ ] selection / picking packets
  - [ ] light / environment packets
  - [ ] UI / editor overlay packets
  - [ ] geometry-processing visualization packets
- [ ] Resolve retained `GPUScene` handles, bindless references, and debug-view state during extraction rather than during late pass recording.
- [ ] Add tests that guarantee render prep and command recording consume extraction output only.

#### B4.6 Render preparation stage (D)

- [ ] Treat render preparation as CPU work that may schedule jobs for visibility, culling, LOD selection, sort keys, draw packet compaction, upload staging, and command recording.
- [ ] Keep the main loop aware only of broad phases; do not hardcode detailed pass order outside renderer-owned preparation code.
- [ ] Build the frame's render graph during preparation from `RenderWorld` and current view configuration.
- [ ] Keep the current three-pass pipeline, deferred path, post-process path, selection/debug overlays, and future hybrid paths expressed as render-graph composition rather than top-level loop branching.
- [ ] Prepare for GPU-driven / indirect execution by making CPU preparation emit packets and scheduling metadata rather than immediate live-state callbacks.

#### B4.7 GPU submission stage (E)

- [ ] Make the renderer follow the explicit-API rhythm: wait frame-context availability -> acquire -> reset per-frame allocators -> record -> submit -> present.
- [ ] Keep swapchain acquire/present and final submit on the main thread; push all other practical work to jobs.
- [ ] Move `RenderGraph` compile/record/execute under renderer-owned execution code.
- [ ] Handle resize / out-of-date / minimized states without corrupting in-flight frame contexts.
- [ ] Evolve toward queue-domain-aware scheduling (graphics / compute / transfer) without exposing queue details directly to the top-level engine loop.

#### B4.8 Maintenance stage (F)

- [ ] Retire completed uploads after GPU completion is known.
- [ ] Process deferred destruction only when the relevant GPU completion value has passed.
- [ ] Centralize readback completion, garbage collection, profiler rollup, telemetry capture, and hot-reload bookkeeping here.
- [ ] Ensure maintenance can run in headless/test configurations even when no swapchain is active.

#### B4.9 Explicit frame-context ring + bounded frames in flight

- [ ] Introduce a dedicated `FrameContext` ring distinct from swapchain image count.
- [ ] Start with 2 frames in flight by default; make 3 configurable for throughput-heavy modes only.
- [ ] Move per-frame transient ownership under `FrameContext`:
  - [ ] command allocator pools
  - [ ] upload arenas / staging allocators
  - [ ] descriptor arenas
  - [ ] transient buffers / scratch allocators
  - [ ] deferred deletion queues
  - [ ] per-frame render graph or graph-execution cache
  - [ ] per-frame profiling/stat samples
- [ ] Audit existing systems and migrate any frame-temporary resource keyed by swapchain image count to frame-in-flight ownership unless image affinity is truly required.
- [ ] Add explicit wait/reclaim/reset behavior before a `FrameContext` is reused.

#### B4.10 Job system + multi-threaded command recording

- [ ] Treat the CPU task graph as the execution substrate for simulation, extraction, and render-prep jobs.
- [ ] Define per-phase barriers so extraction observes a stable world state and render prep observes a stable `RenderWorld`.
- [ ] Parallelize suitable workloads:
  - [ ] animation / transform propagation
  - [ ] broadphase / AI / script jobs
  - [ ] visibility / culling / LOD lists
  - [ ] draw-packet building / sorting
  - [ ] upload staging
  - [ ] secondary command buffer or pass-local command recording
- [ ] Add a TODO track for multi-threaded command recording of heavy passes (shadow views, debug/editor overlays, multi-view paths).
- [ ] Keep the main thread as a conductor, not a worker.

#### B4.11 Queue model, synchronization, frame pacing, and resource lifetime

- [ ] Track queue domains conceptually as graphics / compute / transfer, even if the first implementation remains mostly single-queue.
- [ ] Promote GPU completion tracking to a first-class timeline/fence abstraction shared by upload retirement, deferred deletion, and readback readiness.
- [ ] Add timeline-based resource retirement instead of immediate GPU resource destruction from gameplay/editor code.
- [ ] Add frame-pacing policies for vsync, low-latency/mailbox, uncapped, editor-throttled, and background-throttled modes.
- [ ] Add telemetry for CPU frame time, GPU frame time, present blocking time, frames in flight, and estimated input-to-present latency.
- [ ] Acquire late when practical, keep frames in flight bounded, and avoid the CPU running many frames ahead of the GPU.

#### B4.12 Migration plan + anti-goals

- [ ] Phase A: lock the current frame-order, resize, upload-retirement, pick/debug, and render-graph validation baselines.
- [ ] Phase B: split the top-level loop into platform / simulation / extraction / render prep / submission / maintenance stages without changing behavior.
- [ ] Phase C: introduce fixed-step simulation + authoritative world commit semantics.
- [ ] Phase D: introduce immutable render extraction types and move renderer-facing ECS walks behind them.
- [ ] Phase E: add the `FrameContext` ring and migrate frame-temporary ownership off swapchain image count.
- [ ] Phase F: split the renderer into `BeginFrame / Extract / Prepare / Execute / EndFrame` lifecycle entry points.
- [ ] Phase G: parallelize extraction/render-prep work and add explicit maintenance-stage retirement.
- [ ] Phase H: evolve toward queue-domain-aware graph scheduling, GPU-driven preparation, and richer async compute / transfer overlap.
- [ ] Preserve safe checkpoints after every phase:
  - [ ] render-graph validation remains green
  - [ ] headless/runtime smoke tests remain green
  - [ ] resize / pick / debug-view behavior remains stable
  - [ ] telemetry budgets stay within agreed thresholds
- [ ] Explicit anti-goals for the refactor:
  - [ ] no single giant task graph that mixes simulation mutation and render submission
  - [ ] no renderer walking arbitrary live ECS state during command recording
  - [ ] no tying per-frame transient ownership to swapchain image count
  - [ ] no unlimited frames in flight
  - [ ] no immediate GPU resource destruction
  - [ ] no hardcoded detailed pass order in the top-level engine loop

---

## 3. Later (P2) — Planned Downstream Work

These items should be **planned now** so the current refactor leaves room for them, but they should be implemented later.

### C1. Material System Rewrite

- [ ] Define future material data-model requirements.
- [ ] Decide how materials map to:
  - [ ] Forward path.
  - [ ] Deferred path.
  - [ ] Hybrid path.
- [ ] Define canonical material parameter packing for rendering.
- [ ] Define CPU-side material representation vs. GPU packed representation.
- [ ] Plan material feature flags:
  - [ ] Opaque.
  - [ ] Masked.
  - [ ] Transparent.
  - [ ] Double-sided.
  - [ ] Emissive.
  - [ ] Special shading models.
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
- [ ] Plan shadow integration points.
- [ ] Plan emissive/material/light interaction.
- [ ] Plan debug views for lights and shading terms.

### C4. Visibility System Improvements

- [ ] Plan depth-prepass support.
- [ ] Plan a visibility buffer or material-classification path if desired later.
- [ ] Plan occlusion-culling integration.
- [ ] Plan GPU-driven pass compatibility with new graph stages.

### C5. Motion/Reprojection Support

- [ ] Plan velocity-buffer support.
- [ ] Plan history-resource ownership.
- [ ] Plan TAA integration points.
- [ ] Plan motion-blur integration points.
- [ ] Plan camera/object motion contracts.

### C6. Decals / SSAO / Screen-Space Effects

- [ ] Plan resource requirements for SSAO.
- [ ] Plan normal/depth usage for screen-space effects.
- [ ] Plan decal insertion points in the graph.
- [ ] Plan debug output for effect intermediates.

### C7. PostProcessPass Factoring

- [ ] Factor `PostProcessPass` into sub-pass classes (bloom, SMAA, tone mapping, histogram) to reduce single-class complexity.

### C8. Render Asset / Shader System Cleanup

- [ ] Plan shader registration refactor.
- [ ] Plan shader hot-reload boundaries by pass/stage.
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C9. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.

---

## 4. Planned Constraints — Design Now, Build Later

These are the explicit constraints agents must preserve during the refactor even when the underlying features are not being implemented yet.

- [ ] Leave room for a material-system rewrite.
- [ ] Leave room for transparency-path separation.
- [ ] Leave room for hybrid lighting.
- [ ] Leave room for motion vectors/history buffers.
- [ ] Leave room for clustered/tiled lighting.
- [ ] Leave room for effect passes like SSAO, decals, and bloom.
- [ ] Leave room for future GPU-driven visibility integration.

---

## 5. Sequencing & Definition of Done

### Sequencing Rules

- Finish P0 before starting P1 feature work unless the task is pure documentation, instrumentation, or test scaffolding that directly de-risks P0.
- Treat P2 as design pressure during P0, not as implementation scope creep.
- Keep selection/debug independent of the chosen lighting path.
- Do not allocate every buffer every frame unless required by the active `FrameRecipe`.
- Do not force full deferred shading everywhere yet.

### Cross-Cut Definition of Done

- [ ] Update `README.md` for each merged refactor milestone when user-facing architecture or workflow changes.
- [ ] Remove superseded code paths immediately (no compatibility clutter beyond staged migration windows).
- [ ] Add at least one integration test per milestone and wire it into the existing test targets.
- [ ] Add or update migration notes when render contracts change.
