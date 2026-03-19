# ADR O2 — Pragmatic Medium Runtime Refactor Path

- **Status:** Proposed recommended default
- **Date:** 2026-03-19
- **Owners:** Runtime / Rendering Architecture
- **Related backlog:** `TODO.md` → `B3. Engine Architecture Review Follow-Up`, `B3.7 Recommended Path (default = O2) + Migration Plan`
- **Related specs:** `docs/architecture/runtime-subsystem-boundaries.md`, `docs/architecture/rendering-three-pass.md`

## Context

The current runtime split is already materially better than the pre-refactor monolith: ownership is separated across `GraphicsBackend`, `AssetPipeline`, `SceneManager`, `AssetIngestService`, and `RenderOrchestrator`, while rendering uses the canonical three-pass pipeline with recipe-driven resources and validated pass contracts.

However, the current orchestration still concentrates too much sequencing logic inside `Engine::Run()`. The main remaining pressure points are not primitive rendering correctness, but frame-loop evolution:

- simulation, extraction, render preparation, submission, and maintenance are not yet first-class runtime stages,
- late renderer work can still depend too directly on live ECS/runtime state,
- test seams exist, but they are weaker than they should be for future fixed-step, hybrid-lighting, and GPU-driven expansion,
- frame-temporary ownership still needs a cleaner trajectory toward explicit frame contexts and bounded frames in flight.

Option **O2** is the medium refactor path: keep the existing subsystem split, but introduce stronger execution-stage boundaries and typed handoff seams before the architecture reaches the point where O3 becomes necessary.

The target handoff can be described as a staged map

$$
\mathcal{W}_{N+1} \xrightarrow{\;\text{extract}\;} \mathcal{R}_{N+1} \xrightarrow{\;\text{prepare}\;} \mathcal{F}_{k} \xrightarrow{\;\text{execute}\;} \mathcal{G}_{k},
$$

where:

- $\mathcal{W}_{N+1}$ is the authoritative world state after a stable simulation/update commit,
- $\mathcal{R}_{N+1}$ is an immutable render-facing snapshot or packet set,
- $\mathcal{F}_{k}$ is the frame-context-owned preparation state for in-flight frame $k$,
- $\mathcal{G}_{k}$ is the submitted GPU work and its retirement timeline.

O2 does **not** require a wholesale rewrite of all subsystems. Instead it minimizes long-term coupling by introducing these boundaries incrementally, with safe checkpoints between phases.

## Decision

Document **O2** as the recommended path for the B3 architecture review package.

Under O2 we will prefer the following direction:

1. preserve the current subsystem ownership split,
2. split frame execution into explicit platform, simulation, extraction, render-preparation, submission, and maintenance stages,
3. move renderer-facing data access behind typed extraction seams rather than late live-state traversal,
4. evolve toward explicit `FrameContext` ownership and bounded frames in flight,
5. keep migration incremental, reversible, and guarded by tests plus telemetry.

This ADR intentionally stops short of claiming that O2 is already ratified in implementation. The separate B3.7 item remains the place where benchmark/test evidence can confirm or reject O2 as the active default.

## Benefits

### 1. Best balance between progress and risk

O2 addresses the real architectural pressure points without paying the cost of an idealized rewrite. It is large enough to improve the frame model, but small enough to land incrementally.

### 2. Stronger correctness boundaries

The most important invariant is that render preparation should consume a stable view of the world rather than mutable live state. O2 makes that invariant explicit and testable.

Expressed as a causality rule,

$$
\partial \mathcal{R}_{N+1} / \partial t = 0
$$

after extraction completes for frame $N+1$: render preparation and recording may read $\mathcal{R}_{N+1}$, but they must not observe additional world mutation inside the same frame.

### 3. Better testability with limited churn

Once extraction and per-stage ownership are explicit, tests can pin down frame ordering, packet contents, resource retirement, and event timing without constructing the entire runtime stack.

### 4. Clear path to bounded frames in flight

O2 creates the right seam for explicit `FrameContext` rings, which is the necessary foundation for predictable upload retirement, deferred destruction, and latency control.

### 5. Future features become easier without committing to O3

Hybrid lighting, richer post-processing, GPU-driven packet preparation, and streaming/service-state evolution all fit more naturally once platform/simulation/extraction/render-prep/maintenance boundaries exist.

### 6. Good performance trajectory

The time complexity of extraction and packet preparation remains linear in the size of the render-relevant view of the world:

- extraction: $O(|E_r| + |C_r|)$ for render-relevant entities/components,
- render packet build: $O(|D| + |P|)$ for draw packets and pass-local work,
- frame-context bookkeeping: $O(1)$ per frame for ring reuse, plus retirement costs already paid today.

The space cost grows by the size of the immutable extracted state,

$$
O(|\mathcal{R}_{N+1}|),
$$

which is a deliberate trade: bounded extra memory buys cleaner synchronization and stronger testing.

## Drawbacks

### 1. Higher migration cost than O1

O2 introduces new typed seams and likely touches `Engine::Run()`, renderer entry points, and lifecycle/retirement boundaries. That increases coordination cost across runtime modules.

### 2. Transitional duplication is likely

During migration there may be a short-lived overlap between legacy orchestration and extracted stage helpers. This is manageable, but it must be aggressively cleaned up once each phase lands.

### 3. Some packet/snapshot work is unavoidable

O2 intentionally introduces intermediate immutable render-facing data. That is architecturally healthy, but it is not free in memory or authoring effort.

### 4. Requires discipline to stop at “medium refactor”

A common failure mode is starting from O2 and accidentally drifting into O3-scale redesign. Review discipline and explicit phase gates are required to keep scope bounded.

## Migration Cost

**Medium.**

Expected O2 work includes:

- explicit stage helpers around the frame loop,
- extracted render-input/render-world types,
- renderer lifecycle entry points such as `BeginFrame / Extract / Prepare / Execute / EndFrame`,
- targeted migration of retirement/readback/maintenance duties,
- contract tests and telemetry snapshots for each cutover point.

This is a material refactor, but still compatible with phased PRs rather than a branch-long rewrite.

## Regression Risk

**Medium, controllable with checkpoints.**

The main risk areas are:

- off-by-one-frame ordering changes in events, picks, or completion visibility,
- resource-lifetime regressions when ownership moves under frame contexts,
- accidental continued access to live ECS state after extraction,
- temporary duplication creating divergence between old and new paths.

These risks are why O2 must be executed with rollback shims, phase-by-phase validation, and telemetry/test gates.

## Performance Impact

**Slight short-term overhead, stronger long-term headroom.**

In the short term, O2 may add modest CPU and memory overhead because immutable extraction data and frame-context ownership are more explicit.

In the medium term, O2 should improve performance engineering because it:

- localizes per-frame ownership,
- enables clearer parallelism boundaries,
- reduces hidden synchronization hazards,
- gives the renderer cleaner inputs for future packet compaction, GPU-driven dispatch, and queue-domain-aware scheduling.

The expected frame model becomes

$$
T_{frame} = T_{platform} + T_{sim} + T_{extract} + T_{prepare} + T_{submit} + T_{maint},
$$

which is a better optimization surface than today’s more entangled orchestration cost.

## Testability Impact

**High positive impact.**

O2 materially improves the ability to test:

- frame-stage ordering,
- extraction boundary correctness,
- renderer preparation without live ECS mutation,
- bounded frame-context reuse and retirement,
- maintenance-stage behavior in headless or reduced-runtime harnesses.

This is the strongest reason to prefer O2 over O1.

## Future Extensibility Impact

**Strong.**

O2 leaves the current subsystem graph recognizable while creating the exact seams needed for:

- fixed-step simulation with stable commits,
- immutable render snapshots,
- bounded multi-frame resource ownership,
- hybrid/deferred/post-process growth,
- future compute/graphics/transfer queue specialization,
- more GPU-driven preparation and indirect execution.

It therefore improves extensibility without requiring the full structural ambition of O3.

## Consequences

### What becomes easier immediately after O2-style staging lands

- freezing and testing frame-lane ordering,
- isolating renderer preparation from live runtime mutation,
- moving maintenance and retirement out of ad hoc call sites,
- reasoning about frames in flight as explicit runtime objects,
- extending the renderer without further bloating `Engine::Run()`.

### What remains intentionally out of scope for O2

- a full top-to-bottom runtime rewrite,
- replacing every subsystem boundary at once,
- prematurely introducing a mega-framework for all orchestration,
- redesigning the rendering architecture beyond the already-established three-pass contracts.

## Guardrails

If O2 is pursued, the following constraints apply:

1. Preserve current runtime ownership boundaries unless a change is justified by measurable friction.
2. Never let render preparation or pass recording walk arbitrary live ECS state after extraction completes.
3. Add a safe checkpoint and rollback strategy before each migration phase.
4. Delete transitional adapters after the migration window closes.
5. Keep telemetry and integration tests ahead of structural cutovers.
6. Do not let O2 silently expand into O3 without an explicit new decision.

## Review Trigger

Escalate from O2 toward O3 only if one or more of the following becomes true:

- the current subsystem split cannot express the needed frame ownership cleanly,
- repeated adapter layers are obscuring the architecture more than helping it,
- fixed-step/extraction/frame-context work reveals a deeper ownership mismatch,
- queue-domain-aware scheduling or GPU-driven preparation requires larger-scale subsystem rearrangement,
- the cumulative O2 migration cost approaches a rewrite anyway.

## Rationale Summary

O2 is the recommended path because it solves the real next-order runtime problems — stage boundaries, extraction correctness, frame-context ownership, and testability — while preserving the engine’s existing subsystem split and rendering contracts. It is the highest-leverage change that remains safely incremental.
