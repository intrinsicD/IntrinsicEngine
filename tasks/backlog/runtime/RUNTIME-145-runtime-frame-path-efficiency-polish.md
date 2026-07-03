---
id: RUNTIME-145
theme: F
depends_on: []
---
# RUNTIME-145 — Runtime frame-path steady-state efficiency polish

## Goal
- Remove the recurring per-frame waste in the runtime frame path: full
  `StableEntityLookup` rebuilds, unbounded `StreamingExecutor` task-record
  growth with linear ready scans, unconditional pre-render transform
  re-sweeps, per-frame extraction set allocation, and full geometry payload
  copies during import applies.

## Non-goals
- Editor-context/per-frame UI model rebuild cost — owned by `RUNTIME-138`.
- Task-graph compile caching — `CORE-008`. Render-graph per-frame costs —
  `GRAPHICS-117`/`GRAPHICS-120`.
- No behavioral changes; every item is an equivalent-output optimization.

## Context
- Owner/layer: `runtime`.
- Items (origin:
  `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R19):
  - `m_StableEntityLookup.Rebuild(*m_Scene)` runs every frame, clearing and
    re-inserting two `unordered_map`s over all stable-id entities
    (`src/runtime/Runtime.Engine.cpp:3022`,
    `Runtime.StableEntityLookup.cpp:57-71`) although incremental
    `Track`/`Forget` and a lazy self-heal already exist.
  - `StreamingExecutor::Impl::Tasks` never reclaims completed records and
    `FindNextReadyIndex` linearly scans all records ever submitted on every
    `PumpBackground` launch attempt, 8 attempts/frame
    (`Runtime.StreamingExecutor.cpp:96-124, 178-204`); cost grows over a
    long editing session. Per-entry `GetState` locking in
    `GetAssetImportQueueSnapshot` compounds it
    (`Runtime.Engine.cpp:3445-3495`).
  - `FlushPreRenderTransformState` unconditionally re-runs the
    TransformHierarchy/BoundsPropagation/RenderSync sweeps every frame after
    the fixed-step bundle already ran them (`Runtime.Engine.cpp:2915-2927`)
    — redundant in the common no-post-sim-mutation frame.
  - `ExtractAndSubmit` allocates a fresh
    `std::unordered_set<uint32_t> liveRenderableKeys` per frame
    (`Runtime.RenderExtraction.cpp:2305`).
  - Import applies copy whole geometry payloads multiple times
    (`Runtime.Engine.cpp:1453, 1676, 1749, 1758, 1806, 1815`) — spike cost,
    fixable with moves/shared payload handoff.

## Required changes
- [ ] Make `StableEntityLookup` event-driven: incremental `Track`/`Forget`
      on entity create/destroy/stable-id change + the existing lazy
      self-heal; delete the per-frame `Rebuild` call (keep `Rebuild` for
      scene replacement only).
- [ ] Add slot recycling + a ready queue to `StreamingExecutor` (free-list
      reuse of completed records; O(1) ready pop); batch
      `GetAssetImportQueueSnapshot` state reads under one lock.
- [ ] Guard `FlushPreRenderTransformState` with a needs-flush dirty bit set
      by post-sim mutation sources (gizmo drag, inspector edits,
      `OnVariableTick` transform writes); BUG-024's read-fresh-bounds
      guarantee must hold whenever the bit is set.
- [ ] Reuse a member container for `liveRenderableKeys`.
- [ ] Convert import payload handoff to moves/`shared_ptr` where the payload
      is not mutated after handoff.

## Tests
- [ ] Contract: entity create/destroy/stable-id-change keeps
      `ResolveByStableId` correct without per-frame rebuilds (including the
      scene-replacement path).
- [ ] Contract: `StreamingExecutor` record count stays bounded across many
      submit/complete cycles; ordering/dependency semantics unchanged.
- [ ] Contract: a post-sim transform edit still reaches extraction the same
      frame (camera-focus/BUG-024 regression stays green); an idle frame
      skips the redundant flush (counter probe).
- [ ] Existing extraction/import suites stay green.

## Docs
- [ ] Update `src/runtime/README.md` for the lookup and executor lifecycle
      changes.

## Acceptance criteria
- [ ] Steady-state idle frame performs no stable-lookup rebuild, no
      transform re-sweep, and no unbounded executor scans (counter probes in
      contract tests).
- [ ] No observable behavior change in any covered path.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Trading correctness for speed: the dirty-bit flush must be conservative
  (flush when uncertain).
- Restructuring `StreamingExecutor`'s public API (internal lifecycle only).
- Bundling `RUNTIME-138`'s editor-model work into this task.
