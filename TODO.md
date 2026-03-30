# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** harden the frame pipeline, close the remaining rendering architecture gaps (depth prepass, shadow mapping, GPU-driven submission), and advance the GPU compute backend — without painting the engine into a corner for hybrid, transparency, or material-system work.

**Success criteria:**

- Deterministic frame construction with explicit pass/resource dependencies.
- Canonical render targets with validated producer/consumer contracts.
- Selection, post, and debug visualization decoupled from a single lighting path.
- Only recipe-required intermediate resources allocated per frame.
- Migration hardened by graph compile tests, contract tests, and at least one integration test.
- Depth prepass available for both forward and deferred paths. ✓
- At least one shadow-casting light type with validated resource contracts.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical runtime rendering architecture spec (pass contracts, data contracts, invariants).
- `docs/architecture/frame-loop-rollback-strategy.md` — concrete rollback toggle, shim, and pass/fail gates for frame-loop migration phases.
- `docs/architecture/runtime-subsystem-boundaries.md` — current runtime ownership map, dependency directions, and startup/per-frame/shutdown lifecycle.
- `docs/architecture/adr-o2-pragmatic-medium-runtime-refactor.md` — ratified default runtime migration path.
- `PLAN.md` — archival index for the completed three-pass migration.
- `ROADMAP.md` — medium/long-horizon feature roadmap and phase ordering.
- `README.md` — user-facing architecture summary, build/test entry points, and SLOs.
- `CLAUDE.md` — contributor conventions, C++23 policy, and markdown sync contract.
- `PATTERNS.md` — reusable patterns catalog with canonical examples and usage guidance.

---

## 2. Next (P1) — Near-Term Priorities

P1 items are active development targets with concrete deliverables and test requirements.

### A. Rendering Architecture Gaps

These close critical gaps in the rendering pipeline that block or de-risk later feature work.

**Dependency graph:**

```
A1 (Depth Prepass) ✓ ──→ A2 (CSM Phase 1) ──→ A2b (CSM Phase 2: PCF)
                     └─→ future SSAO (see P2 C6)
                     └─→ future HiZ (see P2 C4)
```

#### A2. Cascaded Shadow Maps (CSM)

Shadow mapping is the single largest visual fidelity gap. CSM for the directional light is the minimal viable target. **Depends on A1** (reuses depth-only pipeline pattern and resource contracts).

Split into two sub-phases to keep commits reviewable:

**Phase 1 — Shadow atlas + depth-only rendering:**
- [ ] Define `ShadowAtlas` transient resource (depth-only, e.g. 2048x2048 x 4 cascades).
- [ ] Add `ShadowPass` render feature: depth-only rendering into cascade viewports using `SurfacePass` geometry.
- [ ] Compute cascade split distances (practical split scheme: logarithmic/uniform blend).
- [ ] Pack cascade matrices into a UBO/SSBO readable by lit passes.
- [ ] Add `ShadowParams` to `LightEnvironmentPacket` (cascade splits, bias, filter size).
- [ ] Recipe-driven: shadow resources allocated only when shadows are enabled.
- [ ] Add focused test: shadow pass produces non-trivial depth for a known scene.

**Phase 2 — PCF sampling + integration:**
- [ ] Add PCF sampling in forward `surface.frag` and deferred `deferred_lighting.frag`.
- [ ] Stabilized cascade frusta (texel snapping to reduce shimmer).
- [ ] Update `rendering-three-pass.md` pass contract table with `ShadowPass`.

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

- [x] Centralize Line/Point CPU frustum culling into `CullDrawPackets()` in `Graphics.RenderPipeline`, producing `CulledDrawList` consumed by `LinePass` and `PointPass`. Self-contained `LocalBoundingSphere` on draw packets enables future GPU cull path.
- [x] Add 8 integration tests (`DrawCulling.*`): culling disabled, behind camera, beyond far plane, zero radius, empty packets, transform-into-view, statistics, consistency with per-sphere test.
- [ ] Extract `RenderDriver::PrepareFrame()` rendering preparation into a schedulable unit (job-graph node) rather than inline sequential call.
- [ ] Emit draw packets and scheduling metadata rather than immediate live-state callbacks.

#### B2. GPU Submission Stage Hardening

- [ ] Make the renderer follow the explicit-API rhythm: wait frame-context availability -> acquire -> reset per-frame allocators -> record -> submit -> present.
- [ ] Keep swapchain acquire/present and final submit on the main thread; push all other practical work to jobs.
- [ ] Abstract queue submission behind a `QueueDomain` enum (Graphics/Compute/Transfer) in `RHI`, even if the initial implementation maps all to a single queue family.
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

#### B6. Maintenance Lane Completion

Verify that the centralized maintenance lane (from commit `25d5bd2`) covers all remaining concerns. If gaps exist, close them:

- [ ] Garbage collection of retired GPU resources.
- [ ] Profiler rollup and telemetry capture.
- [ ] Hot-reload bookkeeping (shader, texture, material).
- [ ] Add integration test: maintenance lane correctly retires resources after N frames.

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

#### E1. Shader Hot-Reload

Begin alongside A1 for fast shader iteration during rendering mode and shadow development.

- [ ] File watcher (inotify/kqueue) on the shader source directory.
- [ ] On change: recompile affected SPIR-V, recreate `VkPipeline` via `PipelineLibrary`.
- [ ] Graceful fallback: if compilation fails, keep the previous pipeline and log the error.
- [ ] Wire into `FeatureRegistry` as a toggle (`ShaderHotReload`).
- [ ] Scope: graphics pipelines only (compute hot-reload can follow later).
- [ ] Update ROADMAP "Ongoing" section to cross-reference this item.

#### E2. GPU Memory Budget Tracking

The engine has no visibility into GPU memory consumption.

- [ ] Query `VkPhysicalDeviceMemoryBudgetPropertiesEXT` (or VMA budget API) once per frame.
- [ ] Expose total/used/budget per memory heap in telemetry.
- [ ] Add an ImGui panel section showing live GPU memory usage.
- [ ] Warning threshold configurable via `FeatureRegistry`. Initial default 80% per heap. Warning fires once per heap per transition (not per frame).

---

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
- [ ] Plan shader hot-reload boundaries by pass/stage (builds on P1/A3 foundation).
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C9. GPU-Driven Indirect Rendering

The engine already has compute culling shaders (`instance_cull.comp`, `instance_cull_multigeo.comp`) but no runtime path exercises them. This bridges CPU-driven draw submission to GPU-driven indirect dispatch. B1 prepares draw packet structures to be consumable by this path.

- [ ] Define `IndirectDrawBuffer` resource and `DrawIndirectCommand` packing.
- [ ] Build a `GPUCullPass` that reads GPUScene SSBO + camera frustum, writes indirect draw commands.
- [ ] `SurfacePass` variant that consumes `vkCmdDrawIndexedIndirect` from the cull output.
- [ ] Feature-flagged: `FrameRecipe` selects CPU-driven or GPU-driven path.
- [ ] Benchmark: compare CPU-culled vs. GPU-culled draw submission at 10K+ entities.
- [ ] Plan multi-view extension (shadow cascade culling reuses the same path).

### C10. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.
