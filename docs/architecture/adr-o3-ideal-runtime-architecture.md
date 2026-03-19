# ADR O3 — Ideal Runtime Architecture Target

- **Status:** Accepted as a documented long-horizon target option
- **Date:** 2026-03-19
- **Owners:** Runtime / Rendering Architecture
- **Related backlog:** `TODO.md` → `B3. Engine Architecture Review Follow-Up`, `B3.7 Recommended Path (default = O2) + Migration Plan`
- **Related specs:** `docs/architecture/runtime-subsystem-boundaries.md`, `docs/architecture/rendering-three-pass.md`

## Context

The runtime has already moved away from the original monolithic rendering path, but the remaining orchestration is still centered too heavily in `Engine::Run()`. O1 documents the lowest-risk hardening path and O2 documents the recommended medium refactor, yet the B3 review package still needs a clear statement of the **ideal target** the engine would grow toward if scope, timing, and migration risk were unconstrained.

Option **O3** is that target. It treats the runtime as an explicitly staged pipeline with immutable handoff products, bounded frame ownership, and renderer/service subsystems that communicate through typed packets instead of late traversal of live ECS state.

The intended staged map is

$$
\mathcal{W}_{N+1}
\xrightarrow{\;\text{extract}\;}
\mathcal{R}_{N+1}
\xrightarrow{\;\text{prepare}\;}
\mathcal{F}_{k}
\xrightarrow{\;\text{submit}\;}
\mathcal{G}_{k}
\xrightarrow{\;\text{retire}\;}
\varnothing,
$$

where:

- $\mathcal{W}_{N+1}$ is the authoritative committed world state,
- $\mathcal{R}_{N+1}$ is an immutable render-facing snapshot,
- $\mathcal{F}_{k}$ is the explicit CPU/GPU frame context for in-flight frame $k$,
- $\mathcal{G}_{k}$ is submitted GPU work plus its synchronization state.

O3 aims to minimize cross-stage coupling rather than just reduce it locally. In graph terms, if $G = (V, E)$ is the runtime dependency graph, O3 seeks to minimize accidental edges

$$
\min \; |E_a|
$$

subject to preserving correctness, bounded latency, and the canonical three-pass rendering contracts. In practice that means every stage owns its inputs/outputs explicitly, and no downstream stage reaches back into mutable upstream state.

## Decision

Document **O3** as the ideal long-horizon runtime architecture option for the B3 review package.

Under O3 we would prefer the following direction:

1. split the runtime into first-class platform, simulation, extraction, render-preparation, submission, and maintenance stages,
2. introduce immutable render-facing packet/snapshot types as the only legal input to renderer preparation,
3. move per-frame ownership under explicit `FrameContext` rings with bounded frames in flight,
4. isolate streaming, upload retirement, readback, and deferred destruction behind typed maintenance services,
5. drive rendering and heavy geometry processing through queue-aware packet execution rather than ad hoc late traversal,
6. treat O3 as an aspirational target, not an immediate default migration plan.

O3 is intentionally more ambitious than O2. It is the architecture we compare against when judging whether incremental work is converging toward a coherent end state or merely accumulating adapters.

## Benefits

### 1. Strongest correctness boundaries

The primary invariant becomes explicit and enforceable:

$$
\frac{\partial \mathcal{R}_{N+1}}{\partial t} = 0
$$

after extraction completes. Render preparation, command recording, and maintenance may consume `RenderWorld`, but they cannot observe additional same-frame world mutation.

This sharply reduces hidden order dependence, late ECS reads, and frame-to-frame ambiguity.

### 2. Best fit for bounded frames in flight

O3 makes frame ownership a first-class runtime concept rather than an emergent convention. Upload allocators, transient render resources, readback staging, deferred destruction, and semaphore/timeline bookkeeping can all live under explicit frame contexts.

That improves reasoning about latency, reclamation, and correctness under double- or triple-buffered execution.

### 3. Highest ceiling for parallelism

Because stage inputs and outputs are explicit, CPU work can be distributed more aggressively across the task system and future queue domains. The renderer can prepare packets, culling, upload lists, and indirect dispatch metadata without leaning on shared mutable runtime state.

The theoretical orchestration cost becomes a composition of mostly linear stage costs,

$$
T_{frame} = T_{platform} + T_{sim} + T_{extract} + T_{prepare} + T_{submit} + T_{maint},
$$

which is a cleaner optimization surface than today's entangled main-loop cost.

### 4. Best testability model

O3 maximizes test seams:

- simulation can be tested independently of rendering,
- extraction can be snapshot-validated,
- renderer preparation can run from immutable packets,
- submission/retirement can be tested against explicit frame contexts,
- streaming/maintenance can be verified without a full editor/runtime boot.

This is the most robust architecture for deterministic contract testing.

### 5. Best long-term extensibility

O3 provides the cleanest home for future requirements such as:

- fixed-step simulation with stable commits,
- hybrid/deferred/forward-plus growth,
- GPU-driven packet compaction and indirect execution,
- queue-domain-aware scheduling,
- asynchronous geometry-processing services,
- richer streaming and readback pipelines.

It is the option least likely to require revisiting fundamental ownership boundaries later.

### 6. Better subsystem legibility at scale

As the engine grows, explicit stage-local responsibilities are easier to reason about than a single ever-smarter composition root. O3 favors clarity through typed boundaries instead of implicit knowledge in `Engine::Run()`.

## Drawbacks

### 1. Highest migration cost

O3 is not a cleanup pass. It implies new frame types, new extraction products, stronger renderer/service APIs, likely substantial restructuring of top-level orchestration, and a non-trivial transition period while legacy and target paths coexist.

### 2. Highest regression surface during migration

The same architectural ambition that makes O3 attractive long-term also makes it risky in-flight. Ordering, lifetime, synchronization, and ownership bugs can appear in many places while the architecture is crossing from live-state traversal to staged immutable handoff.

### 3. Transitional duplication is unavoidable

Until migration is complete, the codebase would likely carry temporary adapters, duplicate packet construction, rollback flags, and dual execution paths. That increases review overhead and can create drift if not aggressively removed.

### 4. Memory overhead is explicit rather than incidental

Immutable snapshots and frame contexts deliberately duplicate some state that is currently implicit or borrowed. The expected additional storage is approximately

$$
O(|\mathcal{R}_{N+1}| + F \cdot |\mathcal{F}|),
$$

where $F$ is the bounded frames-in-flight count. This is often a worthwhile trade, but it is still a real cost.

### 5. Higher coordination burden across subsystems

O3 cannot be landed entirely inside one module. Runtime, renderer, streaming, ECS extraction seams, tests, and telemetry all need to move in concert.

### 6. Scope-control risk

A common failure mode is turning O3 into an endless architecture program that delays user-visible wins. Without strict phase gates, the engine could spend too much time paying structural cost before harvesting practical value.

## Migration Cost

**High.**

Expected O3 work includes:

- explicit stage-owned APIs around the main loop,
- immutable `WorldSnapshot`, `RenderFrameInput`, `RenderWorld`, and `FrameContext` families,
- renderer lifecycle decomposition (`BeginFrame / Extract / Prepare / Execute / EndFrame` or equivalent),
- maintenance/service state machines for retirement, readback, uploads, and streaming completion,
- broad test harness work to validate each boundary,
- telemetry and rollback infrastructure for every structural cutover.

This is significantly larger than O2 and should be treated as a multi-phase program rather than a normal refactor PR sequence.

## Regression Risk

**High, but reducible with disciplined checkpoints.**

The main risk areas are:

- off-by-one-frame visibility changes in events, picks, async completions, or editor feedback,
- resource lifetime bugs as ownership migrates under frame contexts,
- stale or incomplete extraction products,
- synchronization mismatches between graphics/compute/transfer work,
- transitional divergence between legacy and target paths.

O3 requires feature flags, rollback seams, telemetry budgets, and stage-level tests before each major cutover.

## Performance Impact

**Mixed short-term, potentially strongest long-term.**

Short-term costs are likely:

- more packet/snapshot construction,
- more explicit frame bookkeeping,
- temporary duplication while migration is incomplete,
- possible cache pressure from newly materialized immutable products.

Long-term benefits can outweigh those costs because O3 gives the engine better control over stage-local parallelism, queue specialization, retirement timing, and GPU-driven preparation. In other words, O3 may initially raise constant factors while lowering the architectural ceiling on hidden synchronization and orchestration waste.

## Testability Impact

**Very high positive impact.**

O3 is the strongest option for:

- deterministic frame-stage ordering tests,
- snapshot-based extraction validation,
- renderer-preparation tests that avoid live ECS mutation,
- retirement and frames-in-flight reuse tests,
- headless subsystem tests for streaming, maintenance, and readback handling.

Among O1/O2/O3, this is the option with the best long-term verification story.

## Future Extensibility Impact

**Excellent.**

O3 best supports future architectural evolution because the runtime contracts are already shaped around immutable handoffs, bounded ownership, and queue-aware execution. It is the least likely option to require revisiting core orchestration assumptions when adding major rendering, geometry-processing, or streaming features.

## Consequences

### What becomes easier immediately after O3-style architecture lands

- proving render work only consumes extracted immutable state,
- reasoning about per-frame ownership and retirement,
- extending renderer stages without inflating `Engine::Run()`,
- adding queue-domain-aware preparation/submission policies,
- isolating platform, simulation, rendering, and maintenance tests.

### What becomes harder during transition

- landing small unrelated runtime fixes without touching adapters,
- keeping legacy and target paths behaviorally identical,
- reviewing large multi-subsystem changes,
- controlling memory/perf regressions while both paths temporarily coexist.

## Guardrails

If O3 is ever pursued, the following constraints apply:

1. Keep the canonical three-pass rendering contract intact unless a separate ADR changes it.
2. Introduce typed handoff products before deleting legacy access paths.
3. Add rollback flags and telemetry gates before each structural cutover.
4. Delete transitional adapters quickly once a stage is proven stable.
5. Keep frames in flight explicitly bounded and observable.
6. Do not allow live ECS traversal to creep back into late render preparation once extraction exists.

## Review Trigger

Escalate from O2 to O3 only if one or more of the following becomes true:

- medium-refactor adapters are accumulating faster than they can be removed,
- extraction/frame-context ownership cannot be expressed cleanly within the current subsystem split,
- future queue-domain or GPU-driven goals require deeper runtime decomposition,
- repeated correctness bugs trace back to late live-state traversal or ambiguous ownership,
- the cumulative cost of incremental O2 work approaches an O3-scale redesign anyway.

## Rationale Summary

O3 is the right ideal-target document because it states the cleanest end-state architecture the engine could pursue: explicit staged execution, immutable handoff products, bounded frame ownership, and typed service boundaries. It is not the recommended default today, but it is the clearest benchmark for judging whether nearer-term runtime work is converging toward a coherent long-term design.
