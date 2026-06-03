# GRAPHICS-037B — Cross-queue timeline-semaphore edge synthesis

## Goal
- Synthesize timeline-semaphore signal/wait edges at cross-queue producer→consumer
  boundaries in the render-graph compiler (`GRAPHICS-037` decision 3), with
  deterministic per-queue value assignment and the `CrossQueueCycle` fail-closed mode
  (decision 10), tested under the null RHI.

## Non-goals
- No queue-family ownership-transfer barriers (that is `GRAPHICS-037C`).
- No Vulkan recording (that is `GRAPHICS-037D`).
- No async-compute pass content changes.

## Context
- Owner layer: `graphics/framegraph` (compiler edge synthesis) + `graphics/rhi`
  (`ITimelineSemaphore::Signal/Wait` surface, already in the capability baseline).
- Depends on `GRAPHICS-037A` (`QueueAffinity` + partition).
- Decision 3: where a pass on queue A consumes a resource produced on queue B (B≠A),
  emit a timeline-semaphore signal on B after the producer and a wait on A before the
  consumer; one semaphore per producing queue; monotonically increasing per-queue
  values assigned deterministically at compile time.
- Decision 6 determinism: values assigned in `(topological rank, declared pass index)`
  order. Decision 7: culling runs before partitioning; a culled sole-producer removes
  its dependent edge. Decision 10: a cyclic cross-queue dependency fails compilation
  with a structured `RenderGraphValidationResult` finding (`Severity::Error`, kind
  `CrossQueueCycle`) and the renderer falls back to the single-queue path for that frame.

## Required changes
- [ ] Add the `RHI::ITimelineSemaphore` signal/wait surface (or confirm the existing one).
- [ ] Extend the compiler to emit per-queue signal/wait edges at cross-queue boundaries
      with deterministic per-queue value assignment.
- [ ] Add the `CrossQueueCycle` validation finding kind + fail-closed single-queue fallback.
- [ ] Surface the cross-queue-edge count on the `GRAPHICS-022` diagnostics result.
- [ ] `contract;graphics` tests for edge placement, value monotonicity, determinism,
      culling interaction, and the cycle failure mode.

## Tests
- [ ] `contract;graphics` — signal/wait placement at A↔B boundaries; per-queue value
      monotonicity; byte-identical schedules across two compiles; culled-producer edge
      removal; `CrossQueueCycle` fail-closed.
- [ ] CPU gate green.

## Docs
- [ ] Document the cross-queue edge model in `src/graphics/framegraph/README.md`.
- [ ] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [ ] Cross-queue edges are synthesized deterministically and validated under null RHI.
- [ ] The cycle failure mode is deterministic and diagnosable (no hang/partial submit).
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Non-deterministic value assignment (wall-clock / hash-iteration / pointer identity).
- Removing the single-queue fallback.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the edge-synthesis contract.
- `Operational` owned by `GRAPHICS-037D` (real multi-queue submission smoke).
