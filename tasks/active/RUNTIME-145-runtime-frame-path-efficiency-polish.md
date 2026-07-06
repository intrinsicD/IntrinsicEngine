---
id: RUNTIME-145
theme: F
depends_on: []
---
# RUNTIME-145 — Runtime frame-path steady-state efficiency polish

## Status
- Active on local `main`.
- Slices A-D are implemented and locally verified. Slice A removed the
  steady-state `StableEntityLookup` rebuild; Slice B bounds
  `StreamingExecutor` record storage with recycled slots, replaces the
  full-vector ready scan with priority ready queues, and batches import-queue
  streaming state reads through one executor snapshot; Slice C gates the
  pre-render transform flush on pending post-sim transform work; Slice D reuses
  render extraction's live-key scratch storage. Later slices own import payload
  moves and the final default CPU gate.

## Slice plan
- Slice A: wire `StableEntityLookup` to scene `StableId` component events,
  keep full rebuilds only at whole-scene replacement boundaries, and prove
  `RunFrame` no longer rebuilds the lookup in steady state.
- Slice B: recycle `StreamingExecutor` records, add an O(1) ready queue, and
  batch queue snapshot state reads. Implemented 2026-07-06.
- Slice C: add a conservative pre-render transform dirty bit and skip idle
  transform re-sweeps. Implemented 2026-07-06.
- Slice D: reuse live renderable key storage during extraction. Implemented
  2026-07-06.
- Slice E: move/share import payload handoff data where ownership allows.
- Slice F: run the default CPU gate, retire the task, and append the
  retirement log entry.

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
- [x] Make `StableEntityLookup` event-driven: incremental `Track`/`Forget`
      on entity create/destroy/stable-id change + the existing lazy
      self-heal; delete the per-frame `Rebuild` call (keep `Rebuild` for
      scene replacement only).
- [x] Add slot recycling + a ready queue to `StreamingExecutor` (free-list
      reuse of completed records; O(1) ready pop); batch
      `GetAssetImportQueueSnapshot` state reads under one lock.
- [x] Guard `FlushPreRenderTransformState` with a needs-flush dirty bit set
      by post-sim mutation sources (gizmo drag, inspector edits,
      `OnVariableTick` transform writes); BUG-024's read-fresh-bounds
      guarantee must hold whenever the bit is set.
- [x] Reuse a member container for `liveRenderableKeys`.
- [ ] Convert import payload handoff to moves/`shared_ptr` where the payload
      is not mutated after handoff.

## Tests
- [x] Contract: entity create/destroy/stable-id-change keeps
      `ResolveByStableId` correct without per-frame rebuilds (including the
      scene-replacement path).
- [x] Contract: `StreamingExecutor` record count stays bounded across many
      submit/complete cycles; ordering/dependency semantics unchanged.
- [x] Contract: a post-sim transform edit still reaches extraction the same
      frame (camera-focus/BUG-024 regression stays green); an idle frame
      skips the redundant flush (counter probe).
- [x] Existing extraction suite stays green after live-key scratch reuse.
- [x] Existing import suites stay green.

## Docs
- [x] Update `src/runtime/README.md` for the lookup lifecycle change.
- [x] Update `src/runtime/README.md` for the executor lifecycle change.
- [x] Update `src/runtime/README.md` for the extraction live-key scratch
      reuse.

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

Slice B focused verification run locally (2026-07-06):
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='RuntimeStreamingExecutor.*'
```

Slice C focused verification run locally (2026-07-06):
```bash
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
build/ci/bin/IntrinsicRuntimeGraphicsCpuTests --gtest_filter='RuntimeSandboxAcceptance.IdleFrameSkipsPreRenderTransformFlush:RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSandboxAcceptance|RuntimeRenderExtraction|RuntimeEcsSystemBundle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
```

Slice D focused verification run locally (2026-07-06):
```bash
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
build/ci/bin/IntrinsicRuntimeGraphicsCpuTests --gtest_filter='RuntimeRenderExtraction.ReusesLiveRenderableKeyScratchAcrossExtractions'
build/ci/bin/IntrinsicRuntimeGraphicsCpuTests --gtest_filter='RuntimeRenderExtraction.*'
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='AssetImportFormatCoverage.*:MeshGeometryExtraction.*:GraphGeometryExtraction.*:PointCloudGeometryExtraction.*:MeshPrimitiveViewExtraction.*:ProceduralGeometryExtraction.*'
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.*'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction|RuntimeAssetImportFormatCoverage|MeshGeometryExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction|MeshPrimitiveViewExtraction|ProceduralGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
```

## Forbidden changes
- Trading correctness for speed: the dirty-bit flush must be conservative
  (flush when uncertain).
- Restructuring `StreamingExecutor`'s public API (internal lifecycle only).
- Bundling `RUNTIME-138`'s editor-model work into this task.
