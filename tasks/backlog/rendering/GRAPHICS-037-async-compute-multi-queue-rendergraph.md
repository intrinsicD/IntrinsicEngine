# GRAPHICS-037 — Async compute and multi-queue scheduling in the frame graph (planning)

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

## Design decisions to record
1. **`QueueAffinity` enum.** Define `{ Graphics, AsyncCompute, Transfer }`. Passes carry an optional affinity hint; default is `Graphics`. Forbid silent migration of `Graphics`-tagged passes to other queues.
2. **Capability gating.** Async-compute and dedicated transfer are optional device capabilities. The compiler must produce a valid single-queue schedule when capabilities are absent. Record the fallback rule per affinity.
3. **Cross-queue edge synthesis.** Where a pass on queue A consumes a resource produced on queue B, emit a timeline-semaphore signal on B and wait on A. Record the exact RHI API shape (`ITimelineSemaphore::Signal(value)`, `Wait(value)`).
4. **Resource ownership policy.** Decide between `VK_SHARING_MODE_CONCURRENT` (simpler, cost on bandwidth) and `EXCLUSIVE` with explicit ownership transfer (cheaper, complex). Record per-resource-class rules (per-frame transient vs retained).
5. **Barrier synthesis interaction.** Cross-queue ownership transfer requires a *release* barrier on the producing queue and an *acquire* barrier on the consuming queue. Record how these interact with the existing Sync2 barrier compiler.
6. **Compiler determinism.** Two compiles of the same render graph with the same capability profile produce identical schedules. Record the canonical pass-ordering tiebreaker.
7. **Pass culling interaction.** Affinity hints do not affect pass culling. A culled pass produces no signals/waits. Record the rule.
8. **Diagnostic surface.** Per-queue timeline summary in `RenderGraphValidationResult`: passes per queue, cross-queue edges count, expected timeline length. Per-frame counters: `AsyncComputeUtilizedFrames`, `CrossQueueOwnershipTransferCount`.
9. **Null-RHI testability.** The null RHI exposes deterministic mock queues. CPU contract tests verify partitioning, edge synthesis, and barrier interaction without a real device.
10. **Failure mode.** Cyclic dependency across queues fails graph compilation with a structured `RenderGraphValidationResult` finding. Record the diagnostic shape.
11. **Test split.** `contract;graphics` for partitioning, edge synthesis, and barrier composition under null RHI; opt-in `gpu;vulkan` smoke once Vulkan is operational.
12. **Layering audit.** `graphics/framegraph` does not import `vulkan/*`. Queue-family selection lives in `graphics/vulkan`. `QueueAffinity` is RHI-level.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-037-Impl-A** — `QueueAffinity` enum + RHI surface + null-queue mocks + `contract;graphics` partitioning tests.
- **GRAPHICS-037-Impl-B** — Compiler partitioning + cross-queue edge synthesis + `contract;graphics` edge tests.
- **GRAPHICS-037-Impl-C** — Ownership-transfer barrier synthesis + interaction tests with the existing barrier compiler.
- **GRAPHICS-037-Impl-D** — Vulkan recording bodies + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- [ ] Optional GPU smoke (after Impl-D): `-L 'gpu|vulkan'`.

## Docs
- [ ] Update `docs/architecture/graphics.md` to record the multi-queue scheduling rule and the queue-family policy.
- [ ] Update `src/graphics/framegraph/README.md` with `QueueAffinity` semantics.
- [ ] Update `tasks/backlog/rendering/README.md` DAG.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] No new dependency edges.
- [ ] Single-queue compile path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No silent migration of `Graphics`-tagged passes to other queues.
- No removal of the single-queue compile path.
- No bypassing of the Sync2 barrier compiler.
- No live ECS access from frame-graph code.
- No mixing of mechanical file moves with semantic refactors.
