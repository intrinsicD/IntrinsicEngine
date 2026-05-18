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
- Status: done (2026-05-18, branch `claude/graphics-rendering-tasks-dKlmC`).
- Commit reference: pending current change.
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

## Recorded decisions

1. **`QueueAffinity` enum.** Locked as `enum class QueueAffinity : uint8_t { Graphics = 0, AsyncCompute = 1, Transfer = 2 }`, declared in `Extrinsic.Graphics.RHI` (RHI layer). Passes carry `std::optional<QueueAffinity> Affinity` on `RenderGraphPassDesc`; default `std::nullopt` is equivalent to `Graphics`. Silent migration of `Graphics`-tagged or unset-affinity passes to other queues is forbidden; the compiler must keep them on the graphics queue. Async-compute and transfer passes never silently demote to graphics either — they fall through the capability-gating rule (Decision 2). Rejected: a bitmask (e.g. `Graphics | AsyncCompute` "ok on either") — adds compiler complexity without enabling any concrete optimization in Phase 1.
2. **Capability gating.** Async-compute and dedicated transfer are optional capabilities exposed by `IDevice::Capabilities().HasAsyncCompute` / `HasDedicatedTransfer`. Compiler fallback rules per affinity: `AsyncCompute`-tagged passes execute on graphics queue when `!HasAsyncCompute`; `Transfer`-tagged passes execute on graphics queue when `!HasDedicatedTransfer`. The compiler emits no cross-queue edges when fallback applies. Diagnostic counter `AsyncComputeFallbackToGraphicsCount` and `TransferFallbackToGraphicsCount` increment per fallback decision. Rejected: failing the compile when capabilities are absent — kills CPU-only and null-RHI tests on every Phase 3+ feature that uses async compute.
3. **Cross-queue edge synthesis.** When pass on queue A reads a resource last written on queue B (A ≠ B), the compiler emits one timeline-semaphore edge: producer pass on B calls `ITimelineSemaphore::Signal(monotonic_value)` after its commands; consumer pass on A calls `ITimelineSemaphore::Wait(monotonic_value)` before its commands. Exactly one timeline semaphore per cross-queue resource-dependency edge; the compiler coalesces multiple consumer waits on the same value when possible. RHI surface: a new `IRenderGraphCompileSink::EmitTimelineSemaphoreEdge(producer_pass, consumer_pass, semaphore_id, value)` callback, populated when `HasAsyncCompute || HasDedicatedTransfer`. Rejected: per-pass semaphore allocation (semaphore count grows linearly with pass count; coalescing across consumers is the standard pattern).
4. **Resource ownership policy.** Per-frame transient resources (frame-graph allocator-owned, lifetime = single frame) use `VK_SHARING_MODE_CONCURRENT` across {graphics, compute, transfer} queue families. Rationale: transients are short-lived, the bandwidth cost of concurrent mode is bounded by their per-frame reuse window, and ownership-transfer barriers would add three additional barriers per transient per cross-queue edge. Retained resources (GpuWorld buffers, textures with multi-frame lifetimes, swapchain images) use `EXCLUSIVE` with explicit ownership transfer; bandwidth cost compounds across frames so the transfer cost amortizes. Rejected: uniform `EXCLUSIVE` everywhere (transient ownership-transfer cost dominates the frame for many small transients); uniform `CONCURRENT` everywhere (steady-state retained-resource bandwidth penalty observable on bandwidth-bound shaders).
5. **Barrier synthesis interaction.** Cross-queue ownership transfer for `EXCLUSIVE` resources produces *two* barriers, both injected by the existing Sync2 barrier compiler: a release barrier on the producing queue (last command of producer pass + `srcQueueFamilyIndex = producer, dstQueueFamilyIndex = consumer`, no access mask on the dst side, no stage mask change on dst) and an acquire barrier on the consuming queue (first command of consumer pass + matching family indices, dst access mask + stage mask = consumer). The Sync2 compiler sees these as paired barriers via the cross-queue-edge metadata; the existing access-mask/stage-mask synthesis pass remains unchanged for intra-queue barriers. Concurrent resources skip ownership-transfer barriers entirely and only need the normal Sync2 access/layout barriers per queue.
6. **Compiler determinism.** Two compiles of the same render graph + same capability profile produce byte-identical pass schedules. Tiebreaker between equally-eligible-to-schedule passes: ascending `RenderGraphPassDesc::Name` lexicographic order on the canonical UTF-8 byte sequence. The compiler is single-threaded over the schedule emission phase to guarantee this; parallel sub-phases (e.g. per-pass barrier synthesis) feed back into a final deterministic merge. Rejected: hash-based or address-based tiebreaker (non-reproducible across builds, breaks `RenderGraphValidationResult` snapshot comparison tests).
7. **Pass culling interaction.** Pass culling runs before queue partitioning. A culled pass produces no signals/waits, contributes no cross-queue edges, and is invisible to the timeline-semaphore allocator. Affinity hints have no effect on culling decisions. Per-queue pass counts in `RenderGraphValidationResult` (Decision 8) report post-culling counts.
8. **Diagnostic surface.** Extend `RenderGraphValidationResult` with `PerQueueSummary { QueueAffinity Queue; uint32_t PassCount; uint32_t CrossQueueIncomingEdges; uint32_t CrossQueueOutgoingEdges; uint32_t ExpectedTimelineLength; }` per queue. Add per-frame counters on `RenderGraphDiagnostics`: `AsyncComputeUtilizedFrames`, `CrossQueueOwnershipTransferCount`, `AsyncComputeFallbackToGraphicsCount`, `TransferFallbackToGraphicsCount`, `CrossQueueCyclicDependencyCount`. All counters are `std::atomic<uint64_t>` zeroed on engine `Initialize()`, mirroring the `GRAPHICS-022` diagnostics pattern.
9. **Null-RHI testability.** The null RHI exposes `NullGraphicsQueue`, `NullAsyncComputeQueue`, `NullTransferQueue` as mock implementations of `IQueue`. Capability profiles for tests can opt into any subset via `NullDeviceCreateInfo::Capabilities`. CPU contract tests inject pass DAGs with declared affinities and assert on (a) the partitioning result, (b) the emitted cross-queue edges (semaphore IDs + monotonic values), (c) the barrier release/acquire pair injected per `EXCLUSIVE` cross-queue resource, all without a real device.
10. **Failure mode.** A cyclic dependency across queues fails graph compilation deterministically: `RenderGraphCompileResult::Status = CompilationFailedCyclicCrossQueueDependency` and `RenderGraphValidationResult::Findings` carries a `CyclicCrossQueueDependency { CycleParticipants : span<RenderGraphPassId>; QueueParticipants : span<QueueAffinity>; }` finding. The single-queue compile path remains available as a recovery option: a fail-closed `CompileForceSingleQueue` flag re-runs the compile with all affinities pinned to `Graphics`, increments `CrossQueueCyclicDependencyCount`, and emits a once-per-startup warn breadcrumb. Rejected: silently re-partitioning to break the cycle (non-deterministic, surprises downstream test expectations).
11. **Test split.** `contract;graphics` tests cover partitioning, cross-queue edge synthesis, ownership-transfer barrier interaction with the Sync2 compiler, capability-fallback rules, determinism tiebreaker, and cyclic-dependency failure mode — all under null RHI. Opt-in `gpu;vulkan` smoke (`tests/integration/graphics/Test.MultiQueueRecording.cpp`) runs after Impl-D once Vulkan is operational on the host and verifies real timeline-semaphore signal/wait behavior on a synthetic two-queue graph. The smoke is excluded from the default CPU gate per AGENTS.md §7.
12. **Layering audit.** `graphics/framegraph` owns the compiler partitioning logic and the cross-queue-edge metadata data type; it does not import `graphics/vulkan/*`. `QueueAffinity` is declared at the RHI layer (`Extrinsic.Graphics.RHI`) and consumed by both framegraph and vulkan. Queue-family selection (mapping `QueueAffinity` → `VkQueueFamilyIndex`) lives in `src/graphics/vulkan/Vulkan.QueueSelection.cpp` and is invisible to framegraph. Per `AGENTS.md` §2 (`runtime -> all lower layers; owns composition/wiring`) the existing `runtime -> graphics/vulkan` import in `src/runtime/Runtime.Engine.cpp` (promoted-Vulkan device selection + `VulkanRequestedButNotOperational` fallback diagnostics owned by `GRAPHICS-033A/B`) is the canonical owner and remains untouched; this slice does not move backend selection out of runtime. The forbidden new edges for this work are: `graphics/framegraph -> graphics/vulkan`, `graphics/* -> runtime`, `graphics/* -> ecs`, and any `runtime -> graphics/vulkan` use that bypasses the public `IDevice` / `EvaluateVulkanOperationalStatus` seams (e.g., reaching for `Vk*` handles, `VkResult`, or queue-family indices from `src/runtime/`). Multi-queue scheduling state stays composed through those existing public seams.

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
- [x] Optional GPU smoke (after Impl-D): `-L 'gpu' -L 'vulkan'` — identified, gated on Impl-D landing.

## Docs
- [x] `docs/architecture/graphics.md` multi-queue scheduling and queue-family policy rows are deferred to Impl-A/B landing (acceptance criteria forbid code changes in this planning slice; doc rows describe behavior implementation children wire).
- [x] `src/graphics/framegraph/README.md` `QueueAffinity` semantics are deferred to Impl-A landing for the same reason.
- [x] `tasks/backlog/rendering/README.md` DAG entry already lists this task with its upstream gates (`GRAPHICS-022`, `GRAPHICS-018T`).

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

## Forbidden changes
- No silent migration of `Graphics`-tagged passes to other queues.
- No removal of the single-queue compile path.
- No bypassing of the Sync2 barrier compiler.
- No live ECS access from frame-graph code.
- No mixing of mechanical file moves with semantic refactors.
