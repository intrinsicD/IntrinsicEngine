# GRAPHICS-037 — Async compute and multi-queue scheduling in the frame graph (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for partitioning frame-graph passes across multiple GPU queues (graphics, async-compute, transfer) with timeline-semaphore signal/wait synthesis at cross-queue edges, queue-family ownership transfer for cross-queue resources, and a single deterministic compile result that remains CPU-testable under the null RHI. Planning only — no scheduler bodies land here.

## Non-goals
- No implementation of multi-queue command recording in `src/graphics/vulkan/`.
- No new RHI surfaces beyond a `QueueAffinity` annotation on passes.
- No async-compute pass *content* changes (shadow rendering, BVH refit, post FX bodies are owned elsewhere).
- No removal of the existing single-queue path; it remains the default.
- No new external dependencies.

## Context
- Owner layers: `graphics/framegraph` (compiler partitioning + barrier synthesis), `graphics/rhi` (`QueueAffinity` enum + `ITransferQueue`/`IComputeQueue` capability surfaces), `graphics/vulkan` (queue-family selection + ownership transfer recording).
- `GRAPHICS-022` established structured rendergraph diagnostics; this task extends them with per-queue timeline visibility.
- `RenderGraph::Compiler` produces a single linear timeline today. Modern engines (Frostbite FrameGraph, UE RDG, Granite) overlap shadow rendering with G-buffer, BVH refit with shading, post-FX with next-frame setup, and denoise with rasterization.
- Vulkan path: cross-queue ownership transfer is required for `VK_SHARING_MODE_EXCLUSIVE` resources, with the bandwidth/latency trade-off vs `VK_SHARING_MODE_CONCURRENT`.
- Cross-links: `GRAPHICS-018T` (transfer queue), `GRAPHICS-022` (diagnostics), `GRAPHICS-033` (operational gate, queue-family rules), `GRAPHICS-046` (GI denoise wants async compute), `GRAPHICS-047` (VSM page rendering benefits from async).

## Recorded decisions
1. **`QueueAffinity` enum.** `enum class QueueAffinity { Graphics, AsyncCompute, Transfer }` defined at the RHI level (`graphics/rhi`). Passes carry an optional affinity hint, default `Graphics`. A `Graphics`-tagged pass is **never** silently migrated to another queue (compiler-forbidden). Rationale: an RHI-level enum lets both `graphics/framegraph` and `graphics/vulkan` reference it with no upward edge, and defaulting to `Graphics` preserves today's single-queue behavior for every unannotated pass.
2. **Capability gating.** `AsyncCompute` and dedicated `Transfer` are optional device capabilities surfaced through the existing capability/operational query. When a capability is absent the compiler **demotes** the affinity to the graphics queue and still produces a correct (serial) schedule. Fallback per affinity: `AsyncCompute → Graphics`, `Transfer → Graphics` (or the graphics queue's transfer capability), `Graphics → Graphics`. A valid single-queue schedule is always producible. Demotions are counted (`QueueAffinityDemotedCount`, decision 8).
3. **Cross-queue edge synthesis.** Where a pass on queue A consumes a resource produced on queue B (B ≠ A), the compiler emits a timeline-semaphore **signal** on B after the producer and a **wait** on A before the consumer. RHI shape: `RHI::ITimelineSemaphore::Signal(std::uint64_t value)` / `Wait(std::uint64_t value)`, with monotonically increasing per-queue values assigned deterministically at compile time. One semaphore per producing queue suffices; values encode ordering. Rationale: timeline semaphores are already in the RHI capability baseline, so no new primitive beyond the `ITimelineSemaphore` surface is needed.
4. **Resource ownership policy.** Per resource class: **per-frame transient** framegraph resources used cross-queue default to `VK_SHARING_MODE_CONCURRENT` (no ownership-transfer barriers; accept the modest per-access bandwidth cost for short-lived aliased resources); **retained** cross-queue resources use `EXCLUSIVE` with explicit queue-family ownership transfer (cheaper steady-state, worth the complexity for long-lived resources). Single-queue resources stay `EXCLUSIVE` with no transfer. Rationale: `CONCURRENT`'s only cost is per-access bandwidth, which matters for hot retained resources, not for short-lived transients — so the split optimizes the common case while keeping the compiler simple.
5. **Barrier synthesis interaction.** For `EXCLUSIVE` cross-queue resources the compiler emits a **release** barrier (`srcQueueFamily = producer`, `dstQueueFamily = consumer`) on the producing queue paired with an **acquire** barrier on the consuming queue, both expressed through the existing Sync2 barrier compiler as queue-family-ownership-transfer barriers (release carries src access/stage, acquire carries dst). The decision-3 timeline wait sequences the acquire after the release. `CONCURRENT` resources need only the semaphore edge plus normal layout/access barriers, no ownership transfer. The Sync2 compiler gains a queue-family field on its barrier descriptors — **not** a second barrier path.
6. **Compiler determinism.** Two compiles of the same graph under the same capability profile produce byte-identical schedules. Canonical tiebreaker: passes ordered by `(topological rank, declared pass index)`, assigned to queues by affinity, timeline values assigned in that order. No wall-clock, hash-map iteration order, or pointer identity participates. Rationale: matches the GRAPHICS-022 deterministic-validation contract and keeps null-RHI tests reproducible.
7. **Pass culling interaction.** Affinity does not affect culling; culling runs before queue partitioning. A culled pass produces no signals/waits, and if culling removes the sole producer of a cross-queue resource the dependent edge is removed with it. Rationale: keeps culling a pure graph-reduction step independent of scheduling.
8. **Diagnostic surface.** `RenderGraphValidationResult` gains a per-queue timeline summary (passes-per-queue, cross-queue-edge count, expected per-queue timeline length). Per-frame counters: `AsyncComputeUtilizedFrames` (≥1 pass actually ran async that frame), `CrossQueueOwnershipTransferCount`, and `QueueAffinityDemotedCount` (capability-absent demotions, decision 2). Atomic increments; no per-frame strings.
9. **Null-RHI testability.** The null RHI exposes deterministic mock queues (graphics + optional async-compute + transfer, toggleable per test) and a mock `ITimelineSemaphore` recording signal/wait values. `contract;graphics` tests verify partitioning, edge synthesis, ownership-transfer barrier placement, demotion fallback, and determinism without a real device. Rationale: keeps the whole scheduler on the CPU-first gate.
10. **Failure mode.** A cyclic cross-queue dependency (A waits on B which transitively waits on A) fails compilation with a structured `RenderGraphValidationResult` finding (`Severity::Error`, kind `CrossQueueCycle`, carrying the participating pass/queue identifiers). No schedule is returned; the renderer fails closed to the single-queue path for that frame and counts the failure. Rationale: deterministic and diagnosable — never a hang or partial submit.
11. **Test split.** `contract;graphics` (null RHI) for partitioning, edge synthesis, ownership-transfer barrier composition, demotion, determinism, and the cycle failure mode; opt-in `gpu;vulkan` smoke for real multi-queue submission once Vulkan is operational (gated by `GRAPHICS-033`, landed in `GRAPHICS-037-Impl-D`).
12. **Layering audit.** `graphics/framegraph` owns partitioning + barrier/edge synthesis and does **not** import `graphics/vulkan/*`; queue-family selection and the concrete `VkQueue` / ownership-transfer recording live in `graphics/vulkan`; `QueueAffinity` and `ITimelineSemaphore` are RHI-level (`graphics/rhi`). No edge in AGENTS.md §2 is crossed; no live ECS access from any frame-graph code.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-037-Impl-A** — `QueueAffinity` enum + RHI surface + null-queue mocks + `contract;graphics` partitioning tests.
- **GRAPHICS-037-Impl-B** — Compiler partitioning + cross-queue edge synthesis + `contract;graphics` edge tests.
- **GRAPHICS-037-Impl-C** — Ownership-transfer barrier synthesis + interaction tests with the existing barrier compiler.
- **GRAPHICS-037-Impl-D** — Vulkan recording bodies + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- [x] Optional GPU smoke (after Impl-D): `-L 'gpu' -L 'vulkan'`.

## Docs
- [x] Multi-queue scheduling-rule / queue-family-policy docs for `docs/architecture/graphics.md` and `QueueAffinity` semantics for `src/graphics/framegraph/README.md` are deferred to the implementation children (`GRAPHICS-037-Impl-A/B/C`): the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, and the architecture-doc/README sections land with the child that makes them current-state per AGENTS.md §9.
- [x] `tasks/backlog/rendering/README.md` DAG entry updated to point at the retired task in `tasks/done/`.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] No new dependency edges.
- [x] Single-queue compile path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve multi-queue scheduling decisions are recorded with explicit answers and trade-off rationales (RHI-level `QueueAffinity` enum, capability-absent demotion fallback, timeline-semaphore cross-queue edge synthesis, transient-`CONCURRENT` / retained-`EXCLUSIVE` ownership policy, Sync2 release/acquire ownership-transfer barriers, deterministic schedule tiebreaker, culling independence, the per-queue diagnostic surface + three counters, null-RHI mock queues, the `CrossQueueCycle` fail-closed mode, the test split, and the layering audit). Implementation children `GRAPHICS-037-Impl-A/B/C/D` are identified but not opened; the single-queue compile path remains the unconditional default until Impl-D. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state; no code or dependency-edge change lands here.

## Forbidden changes
- No silent migration of `Graphics`-tagged passes to other queues.
- No removal of the single-queue compile path.
- No bypassing of the Sync2 barrier compiler.
- No live ECS access from frame-graph code.
- No mixing of mechanical file moves with semantic refactors.
