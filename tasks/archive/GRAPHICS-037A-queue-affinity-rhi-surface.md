# GRAPHICS-037A — QueueAffinity RHI surface, capability demotion, null-queue mocks

## Goal
- Land the RHI-level `QueueAffinity` enum (`GRAPHICS-037` decision 1), a deterministic
  capability-aware demotion helper (decision 2), and null-RHI mock queues + a
  pure partition helper that buckets passes by affinity, with `contract;graphics`
  tests — reconciling the planned RHI enum with the framegraph's existing
  `RenderQueue` vocabulary.

## Non-goals
- No cross-queue timeline-semaphore edge synthesis (that is `GRAPHICS-037B`).
- No ownership-transfer barriers (that is `GRAPHICS-037C`).
- No Vulkan recording (that is `GRAPHICS-037D`).
- No removal of the single-queue compile path; it stays the default.

## Context
- Owner layers: `graphics/rhi` (the `QueueAffinity` enum + `QueueCapabilityProfile`),
  `graphics/framegraph` (the partition helper + reconciliation with the existing
  per-pass `RenderQueue`).
- **Vocabulary reconciliation (required):** the framegraph already defines
  `Extrinsic::Graphics::RenderQueue { Graphics, AsyncCompute, AsyncTransfer }` on
  `RenderPassRecord::Queue`, with `RenderGraphBuilder::SetQueue` and compiler
  queue-handoff tracking. `GRAPHICS-037` decision 1 places the canonical enum at the
  RHI level so `graphics/framegraph` and `graphics/vulkan` reference it with no upward
  edge. Resolve by introducing `RHI::QueueAffinity` as the canonical enum and aliasing
  or mechanically migrating `RenderQueue` to it — keep the mechanical rename in its own
  commit, separate from the demotion-helper addition (no mixed move+semantic commit).
- Depends on `GRAPHICS-037` (planning, done). Cross-links `GRAPHICS-022` (diagnostics),
  `GRAPHICS-018T` (transfer queue).
- Decision 2 fallback: `AsyncCompute → Graphics`, `Transfer → Graphics`,
  `Graphics → Graphics`; a valid single-queue schedule is always producible; demotions
  count `QueueAffinityDemotedCount` (decision 8).
- Decision 6 determinism: passes ordered by `(topological rank, declared pass index)`,
  assigned to queues by affinity; no wall-clock / hash-iteration / pointer identity.

## Status
- Commit references: `59c5a382` for the mechanical `RenderQueue` -> RHI
  reconciliation; this task-landing commit for demotion, partitioning, tests,
  docs, and retirement.
- Landed 2026-06-04 at maturity `CPUContracted`. `RHI.QueueAffinity` now owns
  the canonical `QueueAffinity` enum, `QueueCapabilityProfile`, and
  `ResolveQueueAffinity(...)` fallback policy. The framegraph `RenderQueue`
  name is an alias to the RHI enum, and `PartitionPassesByQueue(...)` buckets
  live compiled passes by resolved affinity in deterministic
  `(topological rank, declared pass index)` order while reporting
  `QueueAffinityDemotedCount`. The single-queue compile/execute path remains
  unchanged as the default; `GRAPHICS-037B/C/D` own cross-queue timeline edges,
  ownership-transfer barriers, and Vulkan recording.

## Required changes
- [x] Add `RHI::QueueAffinity { Graphics, AsyncCompute, Transfer }` to `graphics/rhi`.
- [x] Reconcile the framegraph `RenderQueue` with `RHI::QueueAffinity` (mechanical, own commit).
- [x] Add a `QueueCapabilityProfile` (which optional queues exist) and a pure
      `ResolveQueueAffinity(requested, profile)` demotion helper with the decision-2 fallback.
- [x] Add a deterministic `PartitionPassesByQueue(...)` helper bucketing passes by
      resolved affinity per the decision-6 tiebreaker.
- [x] Add null-RHI mock queues (graphics + optional async-compute + transfer, toggleable).
- [x] `contract;graphics` tests for demotion fallback, partition determinism, and
      the `QueueAffinityDemotedCount` accounting.

## Tests
- [x] `contract;graphics` — demotion per missing capability; deterministic partition
      under a fixed schedule; demotion counter accounting; `Graphics`-tagged passes
      never migrate (decision 1 hard rule).
- [x] CPU gate: `ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine'` green.

## Docs
- [x] Add `QueueAffinity` semantics to `src/graphics/framegraph/README.md` and a
      multi-queue scheduling note to `docs/architecture/graphics.md`.
- [x] Regenerate `docs/api/generated/module_inventory.md` for the new RHI surface.

## Acceptance criteria
- [x] `RHI::QueueAffinity` exists and the framegraph references it (no upward edge).
- [x] Demotion + partition helpers are pure, deterministic, and CPU-tested.
- [x] No new layering violations; single-queue path unchanged as the default.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Silent migration of `Graphics`-tagged passes to other queues.
- Removing the single-queue compile path.
- Mixing the mechanical `RenderQueue → QueueAffinity` rename with the demotion-helper
  semantic change in one commit.

## Maturity
- Target: `CPUContracted` under the null RHI for the affinity/demotion/partition contract.
- `Operational` owned by `GRAPHICS-037D` (Vulkan multi-queue recording + opt-in `gpu;vulkan` smoke).
