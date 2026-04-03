# IntrinsicEngine: Ground-Up Redesign Vision and Priority Stack

- **Status:** Proposed vision document
- **Date:** 2026-04-02
- **Audience:** Runtime, Rendering, Geometry, Tools
- **Intent:** Near- and mid-term architectural guidance based on the current codebase and architecture docs

---

## 0) How to read this document

This document is the **decision-making layer** between the current codebase and the long-horizon redesign blueprint.

Use it to answer:

- what should be preserved,
- what should change next,
- what should stay out of scope for now,
- and which ideas are strategic end-states rather than immediate migration prerequisites.

Relationship to the other architecture documents:

- **Current canonical docs** describe the runtime and rendering contracts that are true today.
- **This vision** prioritizes the next architectural moves and the order in which they should be approached.
- **`ground-up-redesign-blueprint-2026.md`** describes the eventual target shape and should be treated as an end-state reference, not as an immediate rewrite mandate.

The intended workflow is: vision item -> ADR -> phased implementation plan -> tests / telemetry / rollback gate.

---

## 1) Mathematical & Architectural Analysis

### 1.1 Global frame invariant

We model the engine frame as staged products:

$$
\mathcal{W}_{k} \xrightarrow{\text{extract}} \mathcal{R}_{k}
\xrightarrow{\text{prepare}} \mathcal{F}_{k}
\xrightarrow{\text{submit}} \mathcal{G}_{k}
\xrightarrow{\text{retire}} \varnothing.
$$

The key correctness invariant is:

$$
\frac{\partial \mathcal{R}_{k}}{\partial t} = 0 \quad \text{after extraction.}
$$

This is the practical form of immutable render input and it is the requirement that allows deterministic testing, queue overlap, and stable in-flight frame contexts.

### 1.2 Geometry operator invariant

For processing operators (smoothing, denoise, parameterization, fitting), use a consistent constrained minimization contract:

$$
\min_{\mathbf{x}}\; E(\mathbf{x}) = \frac{1}{2}\mathbf{x}^{\top}L\mathbf{x} - \mathbf{b}^{\top}\mathbf{x}
\quad \text{s.t.}\quad C\mathbf{x}=\mathbf{d}.
$$

- Prefer robust cotangent assembly with fallback stencils when local neighborhoods are degenerate.
- Treat non-manifold edges, zero-area triangles, duplicate vertices, and non-finite attributes as first-class validation states.

**Complexity targets:**

- Local topology/attribute passes: $O(n)$ time, $O(n)$ memory.
- Hierarchy rebuild (LBVH/BVH): $O(n\log n)$ build, $O(\log n)$ average query.
- Iterative sparse solves: $O(km)$ where $m=\text{nnz}(L)$.

### 1.3 Three-Graph assignment

- **CPU Task Graph (fibers):** simulation, gameplay, extraction assembly, frame-critical scheduling.
- **GPU Frame Graph (transient DAG):** virtual resources, aliasing, barrier synthesis, queue scheduling.
- **Async Streaming Graph:** IO, transcoding, background geometry jobs, cache preparation.

This 3-graph view is a useful architecture lens today. It should guide interfaces and ownership boundaries even where the concrete orchestration remains staged and partially separated.

---

## 2) What is already excellent (preserve as-is)

### 2.1 Geometry kernel quality

The geometry stack is already unusually strong: broad operator coverage, robust Params/Result contracts, diagnostic-rich results, and defensive iteration/validation patterns. This is a core differentiator and should be preserved.

### 2.2 BDA shared-buffer rendering model

The current BDA-oriented shared buffer strategy and topology-view reuse is modern and highly performant. Keep this as a strategic advantage.

### 2.3 Three-pass rendering model and dirty-sync flow

Surface/Line/Point parity and domain-specific dirty tracking with incremental upload policy are good architectural choices. Preserve the model and evolve implementation details.

### 2.4 Existing scheduler / allocator / subsystem composition

DAGScheduler usage, expected-based error handling, subsystem ownership boundaries, and frame allocators are sound foundations to build on.

---

## 3) What to change (10 prioritized areas)

### 3.1 Promote Geometry and ECS to top-level libraries

**Today:** Geometry and ECS are already logically reusable, but their physical placement still suggests they are subordinate to Runtime.

**Change:** Move toward standalone top-level libraries and CMake targets (`Geometry`, `ECS`) and split editor-heavy graphics code from the runtime graphics core.

**Why:** Better dependency hygiene, clearer layering, easier tool and Python integration, and more honest build-graph boundaries.

**Cost:** Medium.

### 3.2 Make extraction truly immutable

**Today:** Some extraction still depends on live registry views.

**Change:** `ExtractRenderWorld()` materializes flat SoA packets with no back-pointers into ECS.

**Why:** Enforces $\partial \mathcal{R}/\partial t = 0$ physically, not just by convention.

**Cost:** Medium.

### 3.3 Unify frame scheduling semantics

**Today:** CPU FrameGraph and GPU RenderGraph are separate orchestration layers with implicit sequencing at the top loop.

**Change:** Move toward a super-graph orchestration layer that schedules CPU and GPU work through typed products and explicit dependencies.

**Why:** Makes overlap and critical-path optimization explicit.

**Interpretation:** strategic end-state, not a prerequisite for the current staged runtime migration.

**Cost:** High.

### 3.4 Material permutation architecture

**Today:** The material model is functionally narrow for long-term shading variety.

**Change:** Template + permutation-key system:

$$
K = H(\text{ShadingModel},\text{FeatureFlags},\text{LightingPath},\text{VertexLayout}).
$$

PSO cache keyed by $K$, instance parameters in SSBO slots.

**Why:** Scales to diversified shading models and future path additions.

**Cost:** High.

### 3.5 GPU-driven default submission path

**Today:** Infrastructure exists but CPU submission remains dominant in many paths.

**Change:** Default to compute culling + indirect count draws; keep CPU fallback behind feature flags.

**Why:** Better scaling for large scene and entity counts.

**Cost:** Medium.

### 3.6 Memory: thread-local arenas + upload coalescing

**Change:** Per-worker frame arenas from a pooled allocator and a coalescing upload ledger that minimizes copy and barrier churn.

**Why:** Lower synchronization pressure and better scale under parallel system execution.

**Cost:** Low-to-Medium.

### 3.7 Scheduler priorities + cooperative long jobs

**Change:** Two lanes (`High`, `Normal`) and cooperative yielding for long-running geometry tasks.

**Why:** Prevents background starvation of frame-critical work.

**Cost:** Medium.

### 3.8 Asset pipeline binary cache + budgets

**Change:** Raw parse once, binary cache for subsequent loads, plus budgeted and cancellable streaming priorities.

**Why:** Major load-time wins for large datasets and better responsiveness.

**Cost:** Medium-to-High.

### 3.9 Rendering verification in CI

**Change:** Headless GPU tests + golden-image regression + extraction-boundary contract tests.

**Why:** Rendering correctness currently lacks sufficient automated regression visibility.

**Cost:** Medium.

### 3.10 Physical module separation in build graph

**Change:** Enforce dependency order with physically separate targets and incremental build telemetry.

**Why:** Keeps architecture honest and improves developer throughput.

**Cost:** High.

---

## 4) What not to change

- Do **not** interpret these recommendations as a mandate to rewrite the language/runtime wholesale.
- Do **not** over-abstract away Vulkan-native strengths (BDA, explicit Sync2 semantics).
- Do **not** pursue multi-API support before core roadmap goals are reached.
- Do **not** discard EnTT without evidence of concrete bottlenecks in this workload class.

---

## 5) Data design target (SoA-first)

```cpp
// Runtime.RenderExtraction.Types.cppm (vision sketch)
module;
#include <cstdint>
#include <span>
export module Runtime:RenderExtraction.Types;

export struct StrongHandleBase { uint32_t index; uint32_t generation; };

export struct RenderWorldSoA {
    std::span<const float> world_tx; // x,y,z packed in SoA groups in production
    std::span<const float> world_ty;
    std::span<const float> world_tz;
    std::span<const uint32_t> geometry_handle_idx;
    std::span<const uint32_t> material_handle_idx;
    std::span<const float> bounds_radius;
};

export struct FrameContext {
    uint32_t frame_index{};
    uint64_t timeline_value{};
    RenderWorldSoA render_world{};
};
```

This illustrates the intended packet style: hot-path SoA buffers and handle IDs, not pointer-rich object graphs.

---

## 6) Testing & verification plan

```cpp
// Runtime.Tests.ExtractionContracts.cpp (vision sketch)
TEST(ExtractionContracts, RenderWorldHasNoRegistryPointerBackRefs)
{
    // 1) Build world
    // 2) Commit fixed tick
    // 3) Extract RenderWorld
    // 4) Assert packet owns/borrows only immutable frame-owned spans/handles
}
```

```cpp
// Graphics.Tests.GoldenFrame.cpp (vision sketch)
TEST(GoldenFrame, SurfaceLinePointReferenceImage)
{
    // Headless render of canonical scene
    // Compare image hash / perceptual threshold to baseline artifact
}
```

---

## 7) Telemetry requirements

```cpp
// Tracy/Nsight marker plan (vision sketch)
ZoneScopedN("Frame.ExtractRenderWorld");
ZoneScopedN("Frame.BuildPackets");
ZoneScopedN("GPU.CullAndCompact");
ZoneScopedN("GPU.IndirectSubmit");
```

Required counters:

- CPU critical-path frame time on the named benchmark profile.
- Packet build cost.
- Upload bytes per frame and copy command count.
- GPU queue occupancy and bubble time.
- Async streaming backlog depth and cancellation rate.

---

## 8) Priority summary

1. Immutable extraction packets (Medium, highest leverage).
2. GPU-driven default submission (Medium, immediate scale gain).
3. Thread-local arenas + upload coalescing (Low/Medium, predictable wins).
4. CI rendering regression stack (Medium, safety multiplier).
5. Physical module/library separation (High, long-term hygiene).
6. Material permutation system (High, rendering roadmap unlock).
7. Unified orchestration super-graph (High, strategic end-state).

---

## 9) Implementation plan

1. Keep the current staged runtime contracts as the active migration baseline.
2. Translate the top priorities into ADR-backed, phased milestones.
3. Gate each milestone with contract tests, performance budget checks, and a rollback switch.
4. Revisit strategic end-state ideas only after the staged path is landed and measured.
