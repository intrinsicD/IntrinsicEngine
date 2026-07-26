---
id: RUNTIME-194
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-194 — Consolidate runtime work execution

## Session handoff (written 2026-07-26)

Everything a fresh session needs to resume this task. Read `AGENTS.md` and
`tasks/SESSION-BRIEF.md` first as usual; this section only covers what is not
derivable from the tree.

### Where the work stands

- Last commit: `2214ddf9` (RUNTIME-194 Slice A). Working tree clean.
- Default CPU gate: **4255/4255**, one skip
  (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, expected on a
  headless host). Strict layering, docs-sync, test-layout, root-hygiene, and all
  task validators pass.
- Slice A is landed and committed. **Slice B has not been started.**

### Verify the checkpoint before changing anything

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

### Known environment facts (do not re-diagnose these)

- **`BUG-121` fails CI but not here.** `full-cpu`, `ci-asan`, and `ci-ubsan` fail
  on a GLM anonymous-union copy-assignment defect through a C++23 module
  boundary under **clang-20**. This machine's preset resolves **clang++-23**, so
  it does not reproduce locally and is unrelated to RUNTIME-192/194. It is filed
  and unblocked as its own task — do not fold a fix into this one.
- **Quarantined files are local-only.** `experimental/` is gitignored, so the
  files listed under "Quarantine" below exist on this machine only and are
  absent from a fresh clone. They are already removed from git history by their
  retirement commits; the copies are purely so the owner can review before
  deleting. Never restore from them — they are retired surfaces.

### Quarantine (`experimental/to_delete/`, paths preserved)

Eleven files from `RUNTIME-199` / `RUNTIME-204`, awaiting the owner's manual
deletion:

- `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.{cpp,cppm}`
- `src/runtime/SpatialDebug/Runtime.SpatialDebugClosestFace.{cpp,cppm}`
- `src/ecs/Components/ECS.Component.SpatialDebugBinding.cppm`
- `src/runtime/Runtime.MethodFigureExport.{cpp,cppm}`
- `tests/contract/runtime/Test.SpatialDebugAdapters.cpp`
- `tests/contract/runtime/Test.SpatialDebugClosestFace.cpp`
- `tests/unit/runtime/Test.MethodFigureExport.cpp`
- `docs/methods/figure-data-export.md`

The owner asked that retired files be moved here rather than deleted outright.
Repo-root `to_delete/` is **not** an option: `check_root_hygiene.py --strict`
enforces an exact top-level allowlist and would fail. Apply the same convention
to Slice C's deletions.

### Prerequisites already satisfied by RUNTIME-192 (retired 2026-07-26)

- `RuntimeTaskKinds` **already moved** from `Runtime.StreamingExecutor` to
  `Runtime.JobService`; `StreamingExecutor` only re-exports it. Deleting
  `StreamingExecutor` in Slice C will not strand the taxonomy.
- `DerivedJobScope` lives in `Runtime.DerivedJobGraph` and exists solely for
  `DerivedJobKey` identity (it carries `MeshSurface`, which is a job scope and
  deliberately not a property element domain). It disappears with that module in
  Slice C — do not promote it to a general vocabulary.

### Doing Slice B

Migrate lane by lane, keeping the gate green and committing per lane; do not
attempt all consumers in one change. The `JobService` capabilities each lane
needs already exist (see Slice A below): dependencies, priority/kind/cost,
bounded apply, parked work, `ValidateBeforeApply` + world epoch, progress.

Start by re-running the census, since it drives the order:

```bash
grep -rln "StreamingExecutor" src/ tests/ --include=*.cpp --include=*.cppm
grep -rln "DerivedJobRegistry\|Runtime.DerivedJobGraph" src/ tests/ --include=*.cpp --include=*.cppm
```

Two behaviours the old surfaces have that migrations must preserve, both now
expressible on `JobService`:

- `DerivedJobRegistry::ValidateOnMainThread` returns a five-way staleness reason
  (missing entity, stale entity/geometry/source-property/binding generation).
  Map it onto `ValidateBeforeApply` returning `JobApplyValidation`; the result
  must be discarded, never applied, when it is not `Current`.
- `StreamingExecutor::ApplyMainThreadResults(maxApplyCount)` is the bounded
  apply the frame path relies on. Its replacement is
  `DrainCompletions(events, maxApplyCount)`.

`Runtime.AsyncWorkModule` currently composes both old executors; it is reduced to
the single-service lifecycle in Slice C, not Slice B.

### Do not

- Delete `StreamingExecutor` or `DerivedJobGraph` before their production
  workflows and failure/lifetime tests run through `JobService` (see
  `## Forbidden changes`).
- Add a fourth scheduler, a per-feature worker pool, or a blocking frame wait.
- Weaken or exclude a gate to get green.

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

      Production `StreamingExecutor::Submit` sites, which are the actual lane
      list (6 total):
      - `Runtime.SandboxDefaultPolicies.cpp:246` — deferred mesh post-process
      - `Runtime.VisualizationAdapters.cpp:548`
      - `Runtime.SceneDocumentModule.cpp:805`, `:1015`
      - `Runtime.AssetImportPipeline.cpp:2126` (queued geometry import),
        `:2525` (queued model-texture import)

      - [x] **B0 — close the unpublished-finalizer contract gap.** Found while
            scoping B: four of those six sites use
            `StreamingTaskDesc::FinalizeCancellationOnMainThread` to reconcile
            control state they own (the ingest state machine, pending-request
            slots) when a task never applies, and `JobService` had no
            equivalent — a migration without it would leave importers waiting
            forever on a result that will never arrive. Added
            `JobDesc::FinalizeUnpublishedOnMainThread` with the contract
            **exactly one of `PublishCompletion` and
            `FinalizeUnpublishedOnMainThread` runs per submitted job, both on
            the main thread from a completion drain**.

            Deliberately a superset of the old hook: the old one fired on
            explicit cancellation only, but `JobService` can also terminate a
            job as `StaleDiscarded` (epoch/validation) or `Dropped`, and a
            cancellation-only hook leaks consumer state on those paths. Hence
            the name — it keys on "terminated unpublished", not "cancelled".

            All five terminal-unpublished paths claim the finalizer exactly once
            through `FinalizerClaimed`: worker pre-run cancel, worker post-run
            cancel, empty-envelope drop, drain cancel/stale-discard/publish
            reject, and dependency cancellation. Worker paths queue it for the
            main thread rather than running it inline. Finalizers run at the
            **end** of a drain, after `ReleaseSatisfiedDependencies`, so a
            dependency cancellation produced by that same drain reconciles now
            instead of costing an extra frame (same ordering reason as Slice A's
            dependency-release note), and outside the service lock so a
            finalizer may itself submit or cancel. Diagnostics:
            `FinalizedUnpublishedJobs`, `PendingUnpublishedFinalizers`,
            `LastDrainFinalizedUnpublished`.

            Five contract tests: pre-start cancel, post-finish cancel,
            stale discard, dependency cancellation in the same drain, and
            published-job-never-finalizes. CPU gate 4260/4260.

            **Also fixed a pre-existing race in Slice A's own test.**
            `DependentIsCancelledWhenUpstreamDoesNotPublish` (landed in
            `2214ddf9`) called `DrainCompletions` exactly twice and then asserted
            the dependent was `Cancelled`. The dependency-release pass only acts
            on upstreams that already reached a terminal state, and whether the
            cancelled upstream gets there without a drain (the worker observed
            the cancel flag) or needs one (the worker had already queued its
            result at the gate) is a worker interleaving the test does not
            control — so two fixed drains could both run before the upstream was
            terminal, leaving the dependent `AwaitingDependencies`.

            Measured on this machine, per-test binary, 40 runs each:
            **16/40 failures at `2214ddf9` with no source change**, and 32/40
            once this slice's edits perturbed the timing. It passed the single
            gate run recorded in the handoff by luck. Both this test and the new
            dependency-cancellation test now drain inside `WaitUntil` until the
            dependent is terminal instead of a fixed number of times: 0/50
            failures each, and 0/50 for the whole `RuntimeJobService.*` suite.

            The service itself was correct — dependents legitimately wait for a
            later drain when their upstream is still running — so this was a
            test-synchronisation defect only, and no production behaviour
            changed. No `BUG-` task was filed because the defect is in this
            task's own Slice A test and is fixed here; the evidence is recorded
            above rather than in a task that would open and close in one session.
            Lesson for the remaining lanes: assert on a drained-until-terminal
            condition, never on a fixed drain count.
      - [ ] **B1** — `SandboxDefaultPolicies` deferred mesh post-process.
      - [ ] **B2** — `VisualizationAdapters`.
      - [ ] **B3** — `SceneDocumentModule` (both sites).
      - [ ] **B4** — `AssetImportPipeline` (both sites).
      - [ ] **B5** — `DerivedJobRegistry` consumers.
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
