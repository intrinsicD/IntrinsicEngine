# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** harden the frame pipeline, close the remaining rendering architecture gaps (depth prepass, shadow mapping, GPU-driven submission), advance the GPU compute backend, and wire all implemented backend features to the editor UI — without painting the engine into a corner for hybrid, transparency, or material-system work.

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

- `docs/architecture/rendering-three-pass.md` — canonical runtime rendering architecture spec (pass contracts, data contracts, invariants).
- `docs/architecture/frame-loop-rollback-strategy.md` — concrete rollback toggle, shim, and pass/fail gates for frame-loop migration phases.
- `docs/architecture/runtime-subsystem-boundaries.md` — current runtime ownership map, dependency directions, and startup/per-frame/shutdown lifecycle.
- `docs/architecture/post-merge-audit-checklist.md` — required stabilization gate for architecture-touching PRs (contracts, telemetry, graph ownership, config ownership, UI churn checks).
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
A2 (CSM Phase 1) ──→ A2b (CSM Phase 2: PCF)
```

#### A2. Cascaded Shadow Maps (CSM)

Shadow mapping is the single largest visual fidelity gap. CSM for the directional light is the minimal viable target.

Split into two sub-phases to keep commits reviewable:

**Phase 1 — Shadow atlas + depth-only rendering:**
- [x] Define `ShadowAtlas` transient resource (depth-only, e.g. 2048x2048 x 4 cascades).
- [x] Add `ShadowPass` render feature: depth-only rendering into cascade viewports using `SurfacePass` geometry.
- [x] Compute cascade split distances (practical split scheme: logarithmic/uniform blend).
- [x] Pack cascade matrices into a UBO/SSBO readable by lit passes.
- [x] Recipe-driven: shadow resources allocated only when shadows are enabled.
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

#### E2. GPU Memory Budget Warning Configuration

- [x] Make the 80% GPU memory warning threshold configurable via `FeatureRegistry` (preset toggles: 70/75/85/90%).

### F. UI Architecture & Feature Wiring

Wire implemented backend features to the editor UI and improve editor UX. Independent of rendering architecture work (A/B sections). High ROI: most items are pure UI additions over existing, tested backends.

**Dependency graph:**

```
F1 (Operator Wiring) — no deps, parallel with everything
F2 (Rendering Controls) — no deps
F3 (Undo Integration) ──→ F5 (Context Menus, use undo for delete/duplicate)
F4 (Hierarchy Tree) — no deps
F5 (Context Menus) — depends on F3 for reversible actions
F6 (Editor Polish) — no deps, incremental
F7 (Render Target Viewer) — no hard deps; memory footprint sub-item soft-depends on E2
```

#### F1. Wire Remaining Geometry Operators to UI

Remaining geometry operators with full backends but no editor UI trigger. Wire each using the existing `GeometryWorkflowController` + `Widgets` pattern. (Note: Geodesic Distance, K-Means, Mesh Analysis, Normal Estimation, Remeshing, Simplification, Smoothing, Subdivision, Repair, Convex Hull, and Surface Reconstruction are already wired.)

- [x] **Shortest Path (Dijkstra):** Add widget in Inspector Geometry Processing section. Source vertices from `SubElementSelection`. Target vertices optional (empty = full tree). Display result path length, vertex count. Publish `v:shortest_path_distance` and auto-switch color source. "Extract Path Graph" button materializes result as a new `Graph::Graph` entity (requires entity-creation plumbing similar to `SpawnDemoPointCloud`).
- [x] **Parameterization (LSCM):** Add dedicated Geometry → Parameterization panel. Pin vertex selection from `SubElementSelection` (or auto-pin boundary). Display conformal energy, flipped triangle count. Publish `v:texcoord` and auto-switch color source.
- [x] **Boolean CSG:** Add Geometry → Boolean panel. Entity A = selected, Entity B = chosen via entity combo box (no secondary pick API exists). Operation combo (Union/Intersection/Difference). Display vertex/face counts of result. Warn on partial overlap (baseline limitation).
- [x] **Vector Heat Method:** Add widget in Inspector Geometry Processing section (Vertex mode). Source vertices from `SubElementSelection`. Two buttons: "Transport Vectors" and "Compute Log Map". Display per-vertex `v:transported_vector`/`v:logmap_coords` properties. Auto-switch color source to angle/distance.

- [x] **Mesh Quality Panel:** Add dedicated Geometry → Mesh Quality panel (distinct from the existing Mesh Analysis defect-marker panel). Display aggregate statistics table (min/max/mean angle, aspect ratio, edge length, valence, area, volume). Per-metric histograms via ImGui `PlotHistogram`. Summary diagnostics only — no per-element property publishing.
- [x] **Benchmark Runner Panel:** Add a Benchmark panel via `GUI::RegisterPanel`. Expose frame count, warmup frames, and output path inputs. "Run Benchmark" button calls `BenchmarkRunner::Configure()` + `Start()`. Display `BenchmarkStats` summary (avg/min/max/p95/p99 frame time, avg FPS, per-pass averages) in a table after completion. Currently CLI-only (`--benchmark`).

#### F2. Rendering Controls UI

The lighting environment, camera properties, and render mode are controlled programmatically but lack editor panels.

- [x] **Light Environment Serialization:** Extend `Runtime::SceneSerializer` to persist light environment fields. Separate commit from the panel itself.
- [x] **Camera Property Editor:** Add View Settings → Camera section. Expose FOV (degrees slider), near/far clip planes, projection type (perspective/orthographic). Orthographic zoom factor when in ortho mode. Display current eye position and look direction read-only.
- [x] **Global Render Mode Override:** Add a viewport-level render mode dropdown (Shaded/Wireframe/Wireframe+Shaded/Points/Flat). This is a *global* override distinct from the existing per-entity Surface/Wireframe/Vertex visibility toggles in the Inspector. Back this with a renderer-owned global override consumed when building frame draw packet spans.
- [x] **Lighting Path Selector:** Move the `FrameLightingPath` toggle (Forward/Deferred) from the Feature Browser into a prominent View Settings → Rendering combo box. The FeatureRegistry entry remains the backing store; this adds a more discoverable access point.

#### F3. Undo/Redo Integration

`Core::CommandHistory` and `CmdComponentChange<T>` are implemented and tested but not wired to any editor action.

- [x] Wire `CommandHistory` into `Engine` or `SceneManager` as a singleton accessible to all editor controllers.
- [x] Add Edit → Undo / Redo menu items with Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) keyboard shortcuts.
- [x] Wrap `TransformGizmo` drag completion (mouse release) as a `TransformCommand` capturing before/after `Transform::Component` snapshots for all affected entities.
- [x] Wrap Inspector numeric field edits (point size, line width, color changes) as property commands via `MakeComponentChangeCommand<T>()`.
- [ ] Wrap entity creation/deletion from hierarchy context menu as commands.
- [ ] Wrap geometry operator applications (simplify, remesh, smooth, subdivide, repair) as commands capturing mesh state before/after. Note: full mesh deep-copy snapshots are expensive for large meshes. Evaluate shallow CoW or diff-based approach if memory pressure is measured. Consider deferring to P2 if the snapshot cost is prohibitive.
- [x] Display undo/redo stack depth in status bar or Edit menu (e.g. "Undo: Scale (3 remaining)").

#### F4. Hierarchy Panel Improvements

The hierarchy panel renders a flat entity list. The backend supports parent-child relationships via `Transform::Component` parent references.

- [ ] Add drag-and-drop reparenting: dragging entity A onto entity B sets A's parent to B. Compute local transform via `TryComputeLocalTransform()`.
- [ ] Add expand/collapse all buttons.
- [ ] Add multi-entity selection support in hierarchy (Ctrl+click, Shift+click range). Cross-cutting: propagates into Inspector (multi-object editing, F6), gizmo (already supports multi-select), and context menus. Track as a separate sub-task if scope grows.
- [ ] Add remaining context menu actions: Duplicate, Rename, Create Child, Toggle Visibility.

#### F5. Viewport Context Menus

No right-click context menu exists in the 3D viewport.

- [ ] Right-click on entity: Focus Camera, Delete, Duplicate, Select Children, Toggle Visibility, Isolate (hide all others).
- [ ] Right-click on empty space: Create Primitive (Cube/Sphere/Plane/Cylinder), Paste, Reset Camera.
- [ ] Right-click on sub-element (vertex/edge/face mode): Select Connected, Grow Selection, Shrink Selection. Edge Ring/Loop selection deferred until halfedge traversal helpers for manifold strip walking are verified or added.
- [ ] All destructive actions routed through `CommandHistory` (F3 dependency).

#### F6. Editor Polish

Incremental improvements that modernize the editor feel. Each is independent.

- [ ] **Status Bar — GPU Memory:** Add GPU memory usage display to the status bar (depends on E2).
- [ ] **Console/Log Panel:** Add a scrollable, filterable log panel capturing engine log output (currently stdout-only). Category filters (Info/Warning/Error), search, auto-scroll toggle, clear button. Prerequisite: add a ring-buffer log sink to the logging backend so the panel can read captured entries.
- [ ] **Help → Keyboard Shortcuts Panel:** Add a reference panel listing all keyboard shortcuts (supplement to the shortcut hints already shown in menus and toolbar).
- [ ] **Multi-Object Property Editing:** When multiple entities are selected, Inspector shows shared properties with mixed-value indicators. Edits apply to all selected entities. Use `CommandHistory` for batch undo.
- [ ] **Tooltip Coverage — Inspector Fields:** Extend tooltip coverage to all Inspector fields and remaining View Settings sliders.

#### F7. Render Target Viewer Panel Enhancements

The Render Target Viewer panel is already implemented with frame state, recipe flags, pipeline feature state, selection outline / post-process internals, debug view toggles, and resource lists. These are incremental improvements:

- [ ] Add a visual resource lifetime timeline bar per resource (first producer pass → last consumer pass) alongside the existing resource table.
- [ ] Add zoomable texture preview on resource click (currently only viewport debug source selection is supported).
- [ ] When E2 (GPU Memory Budget) lands, show per-resource estimated memory footprint in the resource table.

### G. Compile-Time & Binary-Boundary Hardening — PImpl Refactor Program

#### Why now

The runtime module interfaces currently expose many heavyweight concrete members (`std::vector`, `std::mutex`, `std::unique_ptr` trees, Vulkan handles, large imported module surfaces) directly in exported class layouts. That increases compile fan-out whenever implementation details change, because importers must rebuild for ABI/layout deltas even when behavior/API is unchanged.

For high-churn orchestration classes, move detail-heavy state behind stable, thin exported shells using the PImpl idiom (`class X { struct Impl; std::unique_ptr<Impl> m_Impl; ... }`).

#### Subagent design-review summary (architecture + build focus)

A dedicated peer-review pass was run against engine-facing module interfaces and subsystem boundaries. Consensus:

- PImpl is **most valuable** at composition-root/runtime seams where implementation churn is high and dependency fan-out is wide.
- PImpl is **not universally good**: avoid it for tiny POD-like types, hot per-entity data, and trivial re-export modules where indirection adds cost without reducing rebuild scope.
- For this codebase, PImpl should be applied selectively to **orchestration/manager** classes, not to data-oriented geometry kernels or ECS component structs.

#### Candidate audit (priority-ordered)

**Tier A — Do first (highest rebuild impact, best architecture win)**

1. **`Runtime::RenderOrchestrator`** (`src/Runtime/Runtime.RenderOrchestrator.cppm`)
   - Problem: exported class currently carries many heavyweight members (`FrameGraph`, arenas, `GeometryPool`, `DebugDraw`, pipeline/registry ownership, frame-context ring).
   - Impact: churn in render orchestration internals likely triggers broad importer recompiles.
   - PImpl plan: keep only ctor/dtor + narrow accessor API exported; move all concrete state and helper methods into `Impl` in `.cpp`.

2. **`Graphics::RenderDriver`** (`src/Runtime/Graphics/Graphics.RenderDriver.cppm`)
   - Problem: exported surface includes multiple subsystem objects, cached debug vectors, pipeline retirement storage, and frame-state caches.
   - Impact: frequent rendering feature iteration causes high interface volatility.
   - PImpl plan: stabilize exported contract around frame lifecycle + query API; hide render-graph internals, cache vectors, and pipeline retirement policy in `Impl`.

3. **`Runtime::GraphicsBackend`** (`src/Runtime/Runtime.GraphicsBackend.cppm`)
   - Problem: Vulkan ownership graph is directly visible in exported layout (`Context`, `Device`, `Swapchain`, descriptors, bindless, texture manager, optional CUDA, `VkSurfaceKHR`).
   - Impact: backend maintenance or startup/shutdown ordering changes can force module rebuild cascades.
   - PImpl plan: move GPU stack ownership and teardown ordering into `Impl`; keep accessor-based API stable.

**Tier B — Do next (good payoff, moderate complexity)**

4. **`Runtime::AssetPipeline`** (`src/Runtime/Runtime.AssetPipeline.cppm`)
   - Problem: mutexes, vectors, pending-load structs, and queue internals are exported implementation detail.
   - Impact: async ingestion iteration invalidates dependents unnecessarily.
   - PImpl plan: hide synchronization containers and completion machinery behind `Impl`; preserve thread-safe API.

5. **`Graphics::PipelineLibrary`** (`src/Runtime/Graphics/Graphics.PipelineLibrary.cppm`)
   - Problem: pipeline maps, descriptor-set layouts, and compute pipeline ownership are in the exported class layout.
   - Impact: pipeline compilation/reload work changes headers often.
   - PImpl plan: expose lookup/build API only; move map/storage and layout lifecycle to `Impl`.

**Tier C — Usually avoid / case-by-case**

6. **`Graphics::GPUScene`** — defer PImpl unless compile metrics show it as top rebuild offender; prefer direct data-oriented clarity.
7. **`Graphics::DebugDraw`** — avoid PImpl: immediate-mode container with hot per-frame append/query path.
8. **Thin/re-export modules** (example: `Graphics.MaterialRegistry.cppm`) — avoid PImpl: no concrete layout to hide.

#### Refactor strategy & invariants

- Preserve all existing public behavior and ownership semantics; this is a compile-boundary refactor, not a feature rewrite.
- Keep constructors explicit about borrowed vs owned dependencies.
- Ensure destructors remain in exactly one TU (vtable anchor rule still applies where virtual types exist).
- Maintain no-exception / `std::expected` error propagation style.
- Verify no extra allocations in per-frame hot paths (allocate `Impl` at subsystem construction only).

#### Execution plan

**P0 — Baseline measurement (before code changes)**
- [ ] Capture clean build time and no-op incremental build time.
- [ ] Capture representative "touch one render source file" incremental build time.
- [x] Record module fan-out for Tier A/B interfaces. (`tools/module_fanout_baseline_2026-04-03.md`)

Baseline status note (2026-04-03):
- Local `cmake --preset dev` configure is currently blocked in this container by missing X11 RandR development headers (`libxrandr`), so the two build-time measurements above remain pending until that dependency is installed.

**P1 — Tier A migration**
- [x] Introduce PImpl for `Runtime::RenderOrchestrator`.
- [x] Introduce PImpl for `Graphics::RenderDriver`.
- [x] Introduce PImpl for `Runtime::GraphicsBackend`.
- [ ] Keep API signatures source-compatible where possible to minimize downstream churn.

**P2 — Tier B migration**
- [x] Introduce PImpl for `Runtime::AssetPipeline`.
- [x] Introduce PImpl for `Graphics::PipelineLibrary`.
- [ ] Re-run compile metrics and compare against P0 baseline.

**P3 — Validation & hardening**
- [ ] Run architecture tests + rendering integration tests for frame build/execute/present rhythm.
- [ ] Validate shutdown ordering and deferred destruction still pass under ASan/validation-enabled runs.
- [ ] Audit for accidental hot-path heap churn after PImpl introduction.
- [ ] Update `PATTERNS.md` with a new "Selective PImpl for module-boundary stability" pattern once stabilized.

#### Acceptance criteria

- Measurable reduction in incremental rebuild time for render/runtime internal changes (target: meaningful drop vs P0 baseline).
- No regressions in runtime correctness, frame construction contracts, or shutdown safety.
- Public module interfaces become materially smaller and less volatile for orchestration subsystems.

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
- [ ] Plan shader hot-reload boundaries by pass/stage (builds on P1/E1 foundation).
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

### C11. GMM Spectral Framework (Mesh-Free Spectral Analysis)

Gaussian Mixture Model based spectral methods for point cloud analysis without requiring a mesh (inspired by Engine24's Galerkin Laplacian assembly on Gaussian mixtures).

- [ ] Implement EM fitting with regularized covariances in a `Geometry.GaussianMixture` module.
- [ ] Implement Galerkin Laplacian assembly: Octree spatial indexing → pair discovery → Gaussian product → sparse matrix assembly.
- [ ] Integrate with existing `Geometry.Octree` for spatial queries and `Geometry.DEC` sparse matrix types.
- [ ] Store per-point GMM membership weights via existing `PropertySet` system.
- [ ] Add spectral eigensolve support (requires sparse eigensolver — evaluate Spectra or implement shift-invert Lanczos).
- [ ] **Hierarchical EM (HEM):** Progressive Gaussian mixture reduction (Engine24 CUDA `hem.cuh`). BVH-accelerated KL-divergence nearest-pair merging for multi-resolution GMM pyramids. Enables LOD for point cloud spectral analysis. GPU path via Vulkan compute (after D1 or C14); CPU fallback with Octree KNN.

### C12. ICP Point Cloud Registration

Point-to-point and point-to-plane Iterative Closest Point registration for scan alignment. See also C16 (CPD) for probabilistic non-rigid extension.

- [ ] Implement `Geometry.Registration` module following the operator pattern (Params struct + Result struct with convergence diagnostics).
- [ ] Use existing `Geometry.KDTree` for nearest-neighbor correspondence.
- [ ] SVD-based rigid alignment per iteration.
- [ ] Extend to point-to-plane ICP using estimated normals.
- [ ] Wire to editor UI as a geometry operator.

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

### C15. Point Cloud Robustness Operators

Edge-preserving filtering and statistical quality analysis for noisy/incomplete point cloud data (patterns from basics_lab).

- [ ] **Bilateral filter:** Edge-preserving point cloud smoothing using spatial + normal-space Gaussian weighting. Preserves sharp features that uniform Laplacian smoothing destroys. Implement in `Geometry.PointCloudUtils` following the operator pattern.
- [ ] **Outlier probability estimation:** Per-point outlier score from local density deviation. Flag statistical outliers using Mahalanobis distance from neighborhood covariance. Publish as `p:outlier_score` property.
- [ ] **Kernel density estimation:** Per-point density via Gaussian KDE with adaptive bandwidth (Silverman's rule). Publish as `p:density` property for downstream weighting (reconstruction, registration).
- [ ] Wire all three to editor UI as point cloud geometry operators (F1-style panel).

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

- [ ] **Heat kernel weights:** `w_ij = exp(-||p_i - p_j||² / 4t)` with automatic time parameter selection (e.g. mean edge length squared). Provides distance-adaptive smoothing for irregular graph/mesh connectivity.
- [ ] **GMM-weighted Laplacian:** Mahalanobis-distance edge weights from per-vertex covariance matrices. Enables anisotropy-aware spectral analysis. Requires per-vertex covariance computation from local neighborhoods.
- [ ] Integrate both variants into `LaplacianCache` as alternative `BuildOperators()` modes selectable via enum.
- [ ] Validate via `AnalyzeLaplacian()` — both variants must satisfy symmetry and row-sum invariants.

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

---

## 1.5. Compile-Time & Binary-Boundary Hardening — PImpl Refactor Program (NEW)

### Why now

The runtime module interfaces currently expose many heavyweight concrete members (`std::vector`, `std::mutex`, `std::unique_ptr` trees, Vulkan handles, large imported module surfaces) directly in exported class layouts. That increases compile fan-out whenever implementation details change, because importers must rebuild for ABI/layout deltas even when behavior/API is unchanged.

For high-churn orchestration classes, move detail-heavy state behind stable, thin exported shells using the PImpl idiom (`class X { struct Impl; std::unique_ptr<Impl> m_Impl; ... }`).

### Subagent design-review summary (architecture + build focus)

A dedicated peer-review pass was run against engine-facing module interfaces and subsystem boundaries. Consensus:

- PImpl is **most valuable** at composition-root/runtime seams where implementation churn is high and dependency fan-out is wide.
- PImpl is **not universally good**: avoid it for tiny POD-like types, hot per-entity data, and trivial re-export modules where indirection adds cost without reducing rebuild scope.
- For this codebase, PImpl should be applied selectively to **orchestration/manager** classes, not to data-oriented geometry kernels or ECS component structs.

### Candidate audit (priority-ordered)

#### Tier A — Do first (highest rebuild impact, best architecture win)

1. **`Runtime::RenderOrchestrator`** (`src/Runtime/Runtime.RenderOrchestrator.cppm`)
   - Problem: exported class currently carries many heavyweight members (`FrameGraph`, arenas, `GeometryPool`, `DebugDraw`, pipeline/registry ownership, frame-context ring).
   - Impact: churn in render orchestration internals likely triggers broad importer recompiles.
   - PImpl plan: keep only ctor/dtor + narrow accessor API exported; move all concrete state and helper methods into `Impl` in `.cpp`.

2. **`Graphics::RenderDriver`** (`src/Runtime/Graphics/Graphics.RenderDriver.cppm`)
   - Problem: exported surface includes multiple subsystem objects, cached debug vectors, pipeline retirement storage, and frame-state caches.
   - Impact: frequent rendering feature iteration causes high interface volatility.
   - PImpl plan: stabilize exported contract around frame lifecycle + query API; hide render-graph internals, cache vectors, and pipeline retirement policy in `Impl`.

3. **`Runtime::GraphicsBackend`** (`src/Runtime/Runtime.GraphicsBackend.cppm`)
   - Problem: Vulkan ownership graph is directly visible in exported layout (`Context`, `Device`, `Swapchain`, descriptors, bindless, texture manager, optional CUDA, `VkSurfaceKHR`).
   - Impact: backend maintenance or startup/shutdown ordering changes can force module rebuild cascades.
   - PImpl plan: move GPU stack ownership and teardown ordering into `Impl`; keep accessor-based API stable.

#### Tier B — Do next (good payoff, moderate complexity)

4. **`Runtime::AssetPipeline`** (`src/Runtime/Runtime.AssetPipeline.cppm`)
   - Problem: mutexes, vectors, pending-load structs, and queue internals are exported implementation detail.
   - Impact: async ingestion iteration invalidates dependents unnecessarily.
   - PImpl plan: hide synchronization containers and completion machinery behind `Impl`; preserve thread-safe API.

5. **`Graphics::PipelineLibrary`** (`src/Runtime/Graphics/Graphics.PipelineLibrary.cppm`)
   - Problem: pipeline maps, descriptor-set layouts, and compute pipeline ownership are in the exported class layout.
   - Impact: pipeline compilation/reload work changes headers often.
   - PImpl plan: expose lookup/build API only; move map/storage and layout lifecycle to `Impl`.

#### Tier C — Usually avoid / case-by-case

6. **`Graphics::GPUScene`** (`src/Runtime/Graphics/Graphics.GPUScene.cppm`)
   - Mixed case: could benefit from encapsulation, but this type is used in tight render loops and allocator/update hot paths.
   - Decision: defer PImpl unless compile metrics show it as top rebuild offender; prefer maintaining direct data-oriented clarity first.

7. **`Graphics::DebugDraw`** (`src/Runtime/Graphics/Graphics.DebugDraw.cppm`)
   - Avoid PImpl: immediate-mode container with hot per-frame append/query path. Extra indirection likely hurts without meaningful compile-time win.

8. **Thin/re-export modules** (example: `Graphics.MaterialRegistry.cppm`)
   - Avoid PImpl: no concrete layout to hide; zero practical benefit.

### Refactor strategy & invariants

- Preserve all existing public behavior and ownership semantics; this is a compile-boundary refactor, not a feature rewrite.
- Keep constructors explicit about borrowed vs owned dependencies.
- Ensure destructors remain in exactly one TU (vtable anchor rule still applies where virtual types exist).
- Maintain no-exception / `std::expected` error propagation style.
- Verify no extra allocations in per-frame hot paths (allocate `Impl` at subsystem construction only).

### Execution plan

#### P0 — Baseline measurement (before code changes)
- [ ] Capture clean build time and no-op incremental build time.
- [ ] Capture representative “touch one render source file” incremental build time.
- [ ] Record module fan-out for Tier A/B interfaces.

#### P1 — Tier A migration
- [x] Introduce PImpl for `Runtime::RenderOrchestrator`.
- [x] Introduce PImpl for `Graphics::RenderDriver`.
- [x] Introduce PImpl for `Runtime::GraphicsBackend`.
- [ ] Keep API signatures source-compatible where possible to minimize downstream churn.

#### P2 — Tier B migration
- [x] Introduce PImpl for `Runtime::AssetPipeline`.
- [x] Introduce PImpl for `Graphics::PipelineLibrary`.
- [ ] Re-run compile metrics and compare against P0 baseline.

#### P3 — Validation & hardening
- [ ] Run architecture tests + rendering integration tests for frame build/execute/present rhythm.
- [ ] Validate shutdown ordering and deferred destruction still pass under ASan/validation-enabled runs.
- [ ] Audit for accidental hot-path heap churn after PImpl introduction.
- [ ] Update `PATTERNS.md` with a new “Selective PImpl for module-boundary stability” pattern once stabilized.

### Acceptance criteria

- Measurable reduction in incremental rebuild time for render/runtime internal changes (target: meaningful drop vs P0 baseline).
- No regressions in runtime correctness, frame construction contracts, or shutdown safety.
- Public module interfaces become materially smaller and less volatile for orchestration subsystems.
