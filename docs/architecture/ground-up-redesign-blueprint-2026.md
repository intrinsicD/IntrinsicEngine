# Ground-Up Redesign Blueprint (2026 End-State Reference)

- **Status:** Proposed end-state reference
- **Date:** 2026-04-02
- **Audience:** Runtime, Rendering, Geometry Processing, Tools
- **Scope:** Long-horizon target architecture that should align future ADRs and migrations without overriding the current canonical runtime contracts

## 0) How to read this document

This document describes the **target shape** of the engine, not the current implementation contract and not a mandate for a wholesale rewrite.

Use it for three things:

1. **Evaluate direction:** does a proposed change move the engine toward clearer staged products, more explicit ownership, and stronger verification?
2. **Prevent drift:** when local refactors are made, this document is the end-state reference that helps preserve long-term coherence.
3. **Inform ADRs:** any concrete milestone should be ratified by an ADR and a scoped migration plan before it is treated as active work.

This document should be read together with:

- current canonical contracts such as `rendering-three-pass.md`, `frame-loop-rollback-strategy.md`, and `runtime-subsystem-boundaries.md`,
- the near-term prioritization in `ground-up-redesign-vision.md`,
- and the active migration backlog / roadmap documents.

The default interpretation is therefore:

- **current docs** define what is true today,
- **vision** defines what to prioritize next,
- **this blueprint** defines the long-horizon destination.

## 1) What the engine already gets right (keep these)

Intrinsic already demonstrates the correct direction in several areas and these should be preserved as non-negotiable constraints:

1. **Staged frame-loop execution with fixed-step + render interpolation**, i.e. a simulation accumulator and explicit render $\alpha = \mathrm{clamp}(a/\Delta t, 0, 1)$ handoff.
2. **An explicit render-graph abstraction with per-pass resource access declarations** and Vulkan Sync2 barrier intent encoded in graph dependencies.
3. **A modular runtime split** where `Engine` composes owned subsystems instead of exposing global mutable singleton state.
4. **Feature-flagged migration seams** (`LegacyCompatibility` vs `StagedPhases`) that reduce migration risk.

These foundations are strong and should be treated as proven prior art, not discarded.

## 2) Long-horizon target: the 3-Graph execution fabric

The target architecture uses three orthogonal DAGs with strict product handoffs:

$$
\mathcal{W}_{k} \xrightarrow{\text{Extract}} \mathcal{R}_{k}
\xrightarrow{\text{Prepare}} \mathcal{P}_{k}
\xrightarrow{\text{Submit}} \mathcal{G}_{k}
\xrightarrow{\text{Retire}} \varnothing
$$

plus a persistent async streaming graph that continuously prepares data for future frames.

### Graph A — CPU Task Graph (fiber scheduler)

- Lock-free work-stealing workers.
- Dependency-tracked tasks with explicit priorities (`Critical`, `Frame`, `Background`).
- Cooperative yielding for long-running tasks (physics narrow-phase, mesh operators).
- Deterministic frame fences for fixed-step simulation commits.

### Graph B — GPU Frame Graph (transient per-frame DAG)

- Virtual resources + lifetime intervals + aliasing.
- Queue-domain aware pass scheduling (`Graphics`, `AsyncCompute`, `Transfer`).
- Barrier synthesis from declared resource state transitions.
- Optional graph compilation cache keyed by pipeline + layout + pass signature.

### Graph C — Async Streaming Graph (long-lived)

- Priority queues for IO, transcoding, mesh processing, BVH builds, and derived-data generation.
- Cancellation + backpressure + budgeted main-thread materialization.
- All outputs become immutable artifacts consumed by future frame extraction.

## 3) Hard architectural invariants

1. **Single-writer world:** authoritative world mutation only in simulation/commit stage.
2. **Immutable render input:** after extraction, $\partial \mathcal{R}_{k} / \partial t = 0$ for the remainder of frame $k$.
3. **Bounded in-flight contexts:** fixed $F \in \{2,3\}$ frame contexts, never unbounded growth.
4. **No live ECS traversal in render preparation:** preparation consumes only extracted packets.
5. **No blocking IO on frame path:** frame-critical lanes must never await storage/network.

## 4) Data-oriented runtime model

### 4.1 Hot-path memory rules

- **SoA-first** for hot simulation and render-extraction data.
- **Frame scratch**: linear arenas reset once per frame context.
- **Handle-based ownership**: generational handles for all engine objects referenced across systems.
- **No `shared_ptr` on frame-critical paths**; ownership boundaries remain explicit at subsystem roots.

The intent is not dogmatic uniformity; the rule is that hot-path data should justify any departure from SoA with a measurable reason.

### 4.2 Canonical frame products

```cpp
struct WorldSnapshot {
    uint64_t generation;
    // immutable views/handles into committed state
};

struct RenderWorld {
    CameraPacket camera;
    LightPacket lighting;
    DrawPacketSpan opaque;
    DrawPacketSpan transparent;
    DebugPacketSpan debug;
    PickPacket pick;
    GpuScenePacket gpuScene;
};

struct FrameContext {
    uint32_t index;
    uint64_t timelineValue;
    RenderWorld renderWorld;
    TransientAllocators transient;
    SubmissionLedger ledger;
};
```

## 5) Geometry processing stack redesign

### 5.1 Robust operator contract

Each geometry operator exposes:

- `Validate(input) -> expected<ValidatedInput, ErrorSet>`
- `Execute(validated, execution_policy) -> expected<Result, ErrorSet>`
- `Publish(result, authority)`

Degeneracy policy is explicit:

- zero-area triangles,
- non-manifold adjacency,
- duplicate vertices/edges,
- non-finite attributes.

### 5.2 Common mathematical forms (first-class in API docs)

For smoothing / fairing / parameterization / diffusion class operators:

$$
\min_{\mathbf{x}} \; E(\mathbf{x}) = \frac{1}{2}\mathbf{x}^\top L \mathbf{x} - \mathbf{b}^\top \mathbf{x}
$$

with robust cotangent Laplacian assembly and fallback to uniform weights on invalid local stencils.

For constrained solves:

$$
\min_{\mathbf{x}} E(\mathbf{x}) \quad \text{s.t.} \quad C\mathbf{x} = \mathbf{d}
$$

solved via projected iterative methods or sparse KKT depending on condition number.

### 5.3 Complexity targets

- Local topology/attribute passes: $O(n)$ time, $O(n)$ space.
- BVH rebuild (SAH-lite LBVH GPU path): $O(n \log n)$ build, $O(\log n)$ query average.
- Spectral / diffusion iterative solves: $O(km)$ where $m$ is the number of nonzeros in the sparse system and $k$ is the iteration count.

## 6) GPU-driven rendering redesign

1. Bindless descriptors by default (descriptor indexing).
2. Buffer device address for scene and packet indirection.
3. GPU culling + compaction + indirect dispatch/draw as the preferred scalable path.
4. Pass packets generated from extracted `RenderWorld`, not live scene traversal.
5. Async compute overlap for culling, histogram, denoise, and post passes when barriers allow.

## 7) Recommended module topology (C++23)

- Core — allocators, job/fiber scheduler, telemetry, handles, containers.
- Runtime — frame-loop, extraction, orchestration, feature flags, subsystem composition.
- Geometry — topology kernel, operators, robust numerics, acceleration structures.
- Render — frame graph, packet compiler, pass registry, pipeline cache.
- RHI — Vulkan abstraction, queue/timeline management, memory allocators.
- Assets — importers, codecs, DDC, streaming graph orchestration.
- Tools — profiling UI, validation overlays, offline processors.

All interfaces are narrow module exports; implementations remain in non-exported partitions.

## 8) Verification and observability plan

### 8.1 Correctness gates

- Per-stage contract tests (simulation, extraction, prepare, submit, retire).
- Golden snapshot tests for `RenderWorld` packet stability.
- Geometry fuzz corpus (degenerate + adversarial meshes) with deterministic expected outcomes.

### 8.2 Performance gates

- Budget guidance: CPU frame-critical work < 2 ms on the named target desktop profile used by the benchmark suite.
- Continuous compile-hotspot and frame telemetry regression checks.
- Queue-overlap metrics: graphics/compute/transfer utilization and bubble durations.

These numbers are only meaningful when the benchmark profile, scene class, and hardware target are recorded alongside the result.

### 8.3 Fault containment

- Per-subsystem health states (Healthy, Degraded, Failed).
- Automatic feature fallback (for example, disable async compute denoiser while preserving the main render path).
- Crash-safe artifact journaling for streaming jobs.

## 9) What this blueprint would explicitly change from current architecture

1. Elevate the 3-graph model to first-class runtime contracts instead of mostly implicit coordination.
2. Replace ad hoc data flow with strict typed products (`WorldSnapshot`, `RenderWorld`, `FrameContext`, `SubmissionLedger`) everywhere.
3. Move expensive geometry processing out of frame-critical lanes into the streaming graph with deterministic publish points.
4. Standardize operator robustness contracts so each geometry algorithm has identical validation and error surfaces.
5. Push deeper GPU-driven execution (indirect-first, CPU fallback second) for scalability.
6. Institutionalize gate-based architecture evolution (correctness + performance + rollback) for every major migration.

## 10) How this blueprint should influence migration

This blueprint is not a prescription to stop current work and rewrite the engine around a new super-graph immediately.

The practical interpretation is:

1. Freeze and test the existing staged frame-loop contracts.
2. Introduce canonical typed frame products in parallel with adapters.
3. Move one feature domain at a time to extraction-only render preparation.
4. Shift heavy geometry tasks to the async streaming lane with explicit publish barriers.
5. Expand packet generation and indirect execution incrementally.
6. Consider deeper orchestration unification only after the staged runtime path is proven and measured.

This keeps the current migration strategy compatible with the long-horizon architecture while avoiding premature global rewrites.
