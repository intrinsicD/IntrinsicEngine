---
id: RUNTIME-194
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-194 — Consolidate runtime work execution

## Progress

Promoted to active 2026-07-26. Chunked so each step builds, passes the CPU gate,
and commits independently.

- [x] **Slice A — complete the `JobService` contract.** `JobService` already had
      submit/cancel/world-scope/typed-result/GPU-participant/stats. Slice A added
      the capabilities that previously existed only on `StreamingExecutor` and
      `DerivedJobRegistry`:
      - `JobDependency` edges, with release performed by the completion drain
        (so chain ordering is deterministic and main-thread-ordered instead of
        racing between workers), and a dependent **cancelled** when its upstream
        reaches a terminal state other than `Published` — a chain never applies
        half its work against inputs that were never produced;
      - `Core::Dag::TaskPriority` / `TaskKind` / `EstimatedCost` scheduling
        metadata, and `RuntimeTaskKinds` **moved** from `StreamingExecutor` to
        `JobService` (the surviving surface), re-exported by `StreamingExecutor`
        until it retires;
      - bounded main-thread apply, `DrainCompletions(events, maxApplyCount)`,
        which takes a budget off the front and leaves the remainder queued so a
        completion burst cannot stall a frame;
      - parked work via `IsReadyToApply`, for results that finished but cannot
        be applied yet (for example a GPU readback that has not landed);
      - fail-closed revalidation via `ValidateBeforeApply` plus a world/document
        epoch (`AdvanceWorldGeneration`/`CancellationGeneration`), so a result
        captured before a world replacement is discarded as `StaleDiscarded`
        rather than mutating the replacement;
      - `ReportProgress`/`GetProgress`, and lane diagnostics
        (`AwaitingDependencyJobs`, `AwaitingApplyJobs`, `StaleDiscardedJobs`,
        `LastDrainParked/StaleDiscarded/Budget`).
      Seven contract tests cover dependency gating, dependency cancellation,
      bounded apply, parking, stale-epoch discard, fail-closed validation with
      no double-apply, and progress. CPU gate 4255/4255.

      Implementation note found while testing: dependency release must run
      **after** publication within a drain, otherwise a dependency retired by
      that drain costs its dependents an extra frame.
- [ ] **Slice B — production migration.** Move asset read/import processing,
      selected-entity analysis, clustering/method jobs, and existing derived
      chains onto `JobService` lane by lane, with parity and shutdown tests.
      Consumer census: `StreamingExecutor` has 17 src/test files,
      `DerivedJobRegistry` 30.
- [ ] **Slice C — cleanup.** Delete `Runtime.StreamingExecutor`,
      `Runtime.DerivedJobGraph`, their bridges/DTOs and CMake/test entries, and
      reduce `AsyncWorkModule` to the single-service lifecycle. `RUNTIME-203`
      separately internalizes `JobServiceGpuQueueBridge`. Old files are
      quarantined under `experimental/to_delete/` rather than deleted outright.
      Note: `DerivedJobScope` (added by `RUNTIME-192` for `DerivedJobKey`
      identity) disappears with `Runtime.DerivedJobGraph`.

## Goal

- Make `JobService` the one runtime execution/lifecycle surface for background
  CPU work, dependency chains, GPU participants, cancellation, progress, and
  bounded main-thread result apply; migrate all production users and retire
  `StreamingExecutor` and `DerivedJobRegistry`.

## Non-goals

- No replacement of the domain-free core scheduler/task graph.
- No universal method service, per-feature queue, hidden thread pool, or
  synchronous wait in the frame path.
- No change to algorithm results or feature-specific request/result types.
- No execution of live ECS, AssetService, renderer, or ImGui state on workers.

## Context

- `JobService`, `StreamingExecutor`, and `DerivedJobRegistry` currently expose
  overlapping submit/cancel/dependency/completion/apply lifecycles.
  `AsyncWorkModule` composes the latter two while feature modules also publish
  `JobService` GPU participants.
- The duplication forces import, editor analysis, methods, and GPU readback to
  invent bridges and ordering rules between schedulers.
- The right-sized endpoint keeps the existing `JobService` and
  `AsyncWorkModule`: the module owns configuration/startup/shutdown and
  publishes one service; the old executors/registries disappear.

## Slice plan

- **Slice A — complete JobService contract.** Add immutable snapshot inputs,
  dependencies, priority, world scope, cancellation, typed result storage,
  progress, waiting states, and bounded main-thread apply.
- **Slice B — production migration.** Move import/IO, selected analysis,
  clustering, method work, and existing derived chains lane by lane with
  deterministic parity and shutdown tests.
- **Slice C — cleanup.** After all consumers use `JobService`, delete
  `StreamingExecutor`, `DerivedJobRegistry`, their bridges/DTOs, and obsolete
  CMake/tests; reduce `AsyncWorkModule` to the real single-service lifecycle.

## Required changes

- [ ] Extend `JobService` with the smallest plain request/result records needed
      for dependency edges, priority, world/document epoch, cancellation,
      progress, parked/waiting work, and bounded main-thread apply.
- [ ] Preserve immutable main-thread snapshot capture and fail-closed
      generation revalidation before any result mutation.
- [ ] Provide deterministic lane/budget diagnostics without exposing worker
      objects or adding feature-named queue APIs.
- [ ] Migrate asset reads/import processing, selected-entity analysis,
      clustering/method jobs, and follow-up chains to the one service.
- [ ] Preserve GPU participant recording through the existing renderer frame
      context; GPU completion/readback integration is completed by
      `RUNTIME-195`.
- [ ] Delete `Runtime.StreamingExecutor`, `Runtime.DerivedJobGraph` /
      `DerivedJobRegistry`, and all cross-scheduler adapters after migration.
      `RUNTIME-203` separately internalizes the remaining one-consumer
      `JobServiceGpuQueueBridge` public surface after this task fixes its final
      ownership.
- [ ] Keep `AsyncWorkModule` only as the app-composed owner of `JobService`
      lifecycle/configuration and remove every otherwise empty facade method.

## Tests

- [ ] Contract tests cover dependency ordering, priority, cancellation,
      world replacement, stale completion, progress, parked work, bounded
      apply, and shutdown with no result applied twice.
- [ ] Migration parity tests cover import IO, selected analysis, method
      follow-ups, and GPU participant scheduling through the same service.
- [ ] Frame-loop tests prove no global wait or unbounded completion drain is
      introduced.
- [ ] Structural tests prove the old executor/registry and bridge names have no
      production imports after deletion.

## Docs

- [ ] Update runtime kernel/work lifecycle documentation and remove the
      three-scheduler ordering instructions.
- [ ] Regenerate the module inventory and update all affected task/docs links.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every runtime asynchronous operation is submitted, observed, cancelled,
      and applied through `JobService`.
- [ ] `AsyncWorkModule` owns one real service lifecycle, not parallel worker
      systems.
- [ ] `StreamingExecutor`, `DerivedJobRegistry`, and cross-scheduler adapters
      are deleted after migrated tests pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'JobService|StreamingExecutor|DerivedJob|AsyncWork' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Adding a fourth scheduler, a per-feature worker pool, or a blocking frame
  wait.
- Letting worker callbacks touch live runtime/lower-layer ownership.
- Deleting an old execution surface before its production workflows and
  failure/lifetime tests run through `JobService`.

## Maturity

- Target: `Retired`; `CPUContracted` proves the complete service contract,
  `Operational` is owned by `RUNTIME-194` through the real app-composed
  workflows, and retirement requires all production migrations and deletion
  of both superseded execution systems.
