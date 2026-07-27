---
id: RUNTIME-194
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-194 — Consolidate runtime work execution

## Session handoff (written 2026-07-27)

Everything a fresh session needs to resume this task. Read `AGENTS.md` and
`tasks/SESSION-BRIEF.md` first as usual; this section only covers what is not
derivable from the tree.

### Where the work stands

Updated 2026-07-27 (fourth session).

- Default CPU gate: **4264/4264**, one skip
  (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, expected on a
  headless host). Strict layering, docs-sync, test-layout, root-hygiene, and all
  task validators pass.
- Slice A landed in `2214ddf9`. **Slice B is in its last lane**: `B0`
  (`73dedb2a`), `B1` (`6cb2152b`), `B3` (`b09cee58`), `B2`+`B4` (`6db202c6`),
  `B5-0` (`0eb46f32`, `JobService::SnapshotAll()`), `B5a` (`7f78963d`,
  `GpuReadbackJob`), `B5b` (`15beaef5`, `AssetModelSceneHandoff`), and `B5c`
  (`SelectedMeshTextureBake`) are landed. **All six production
  `StreamingExecutor::Submit` sites are migrated.** `B5d` and `B5e` remain.

### Next action

Take the next unchecked batch in `## Progress` -> `B5d+e` -> "Batch plan and
status". **`B5d` and `B5e` were re-planned into one lane on 2026-07-27** — they
share `SandboxEditorDerivedJobCommandSurface`, and the dedup guard couples the
submit path to the queue snapshot, so neither the two files nor the
submit/presentation halves separate. The owner chose the **dual-surface, batched
route**: both submit paths live side by side, one batch at a time, green gate
per batch, closed by `B5d-1z`.

Read that section before starting — the measured coupling, the temporary
exception's terms, and the list of registry fields the consumer must now record
itself are all there. Then read `FindActiveEditorDerivedJob`
(`SandboxEditorFacades.cpp:4787`) with its call sites: it is the only part of
the lane with real behaviour.

After `B5d-2`, `StreamingExecutor` has no consumer left and Slice C can delete
both modules.

Note that `B5a`–`B5c` were all lanes with **no production caller**, which is why
they moved cleanly and are weaker evidence than they look. `B5d` is the live
editor path, so its existing contract tests are behaviour tests, not shape
tests.

Two behaviours to preserve, both already expressible on `JobService`:
`DerivedJobRegistry::ValidateOnMainThread`'s five-way staleness reason maps onto
`ValidateBeforeApply` returning `JobApplyValidation` (discard, never apply, when
not `Current`), and `ApplyMainThreadResults(maxApplyCount)` maps onto
`DrainCompletions(events, maxApplyCount)`.

Read `## Progress` -> Slice B for the lane list, the two standing obligations
every lane owes (the `ProductionAsyncSubmissionsCarryOwningWorldScope`
source-text assertions and `src/runtime/README.md`), and these accumulated
lessons:

- **B0** — never assert on a fixed drain count; drain until terminal. This bites
  hardest on dependency chains, which release one level per drain.
- **B3** — only assert an exact terminal `JobState` when nothing else in the
  test can reach the consumer first. An observer that revalidates its own
  binding can cancel the job before the drain classifies it.
- **B4** — `ValidateBeforeApply` is the right home for a staleness check only
  when the consumer wants a *silent* discard. When the consumer instead reports
  the stale apply with its own diagnostic, keep the check inside the publish
  body, or the finalizer's terminal diagnostic replaces it.
- **B4** — `JobService` claims a job's unpublished finalizer at the terminal
  transition, not at the cancel request, so a consumer's visible state
  terminalizes only once the worker returns. Any test that cancels a *blocked*
  worker and then waits for terminal before releasing it will deadlock.
- **B5b** — **`JobService` has no inline fallback.** `StreamingExecutor` ran its
  work inline when `Core::Tasks::Scheduler` was uninitialized;
  `JobService::DispatchJob` always calls `Scheduler::Dispatch`, and
  `DispatchInternal` silently drops the task with no scheduler, so the job hangs
  in `Queued`. Any migrated test in a binary without a global scheduler needs a
  `SchedulerScope`.
- **B5b** — a job whose apply body ignores its payload still needs a populated
  result envelope; an empty envelope is how `JobService` reports a dropped job.
- **B5b** — `DerivedJobDesc` defaulted `Kind` to `GeometryProcess` while
  `JobDesc` defaults to `Generic`. Set `Kind` explicitly or the lane taxonomy
  silently changes.
- **B5c** — **declare `SchedulerScope` last in a test.** Its destructor
  quiesces the pool, so it must run while every object a worker can still
  reach is alive. A live scheduler also flips `AssetService::Load` from inline
  to a worker decode, so a scheduler outliving its `AssetService` is a real
  use-after-free — it cost ~35% hung/aborted runs before the ordering was
  fixed.

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
the single-service lifecycle in Slice C, not Slice B. After `B2`+`B4` it no
longer borrows `StreamingExecutor` for the import pipeline, so the executor's
only remaining consumer is `DerivedJobGraph`.

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
      - ~~`Runtime.SandboxDefaultPolicies.cpp:246`~~ — migrated by `B2`
      - ~~`Runtime.VisualizationAdapters.cpp:548`~~ — migrated by `B1`
      - ~~`Runtime.SceneDocumentModule.cpp:805`, `:1015`~~ — migrated by `B3`
      - ~~`Runtime.AssetImportPipeline.cpp:2126`, `:2525`~~ — migrated by `B4`

      All six production `StreamingExecutor::Submit` sites are now on
      `JobService`; `B5` (`DerivedJobRegistry` consumers) is the only lane left,
      and `StreamingExecutor` survives only because `DerivedJobGraph` composes
      it.

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
      - [x] **B1 — `VisualizationAdapters` Htex-recreate receipt.** Taken first
            because it is the only lane with no cross-module coupling:
            `HtexMetadataAdapter`'s executor pointer is null on every production
            path (the only non-null construction is one contract test), and the
            submitted task is a pure scheduling receipt with no
            `ApplyOnMainThread` and no cancellation finalizer.
            `StreamingExecutor*` -> `JobService*`, `StreamingTaskHandle
            LastHtexRecreateTask` -> `JobToken`, and the submission uses an
            explicit `JobDesc{...}` rather than `MakeCpuJobDesc` so the
            `.Scope = world` designator survives — see the structural-test note
            below. CPU gate 4260/4260; the migrated test 0/30 under stress.

            Two obligations this lane surfaced that every later lane also owes:
            - `RuntimeEngineLayering.ProductionAsyncSubmissionsCarryOwningWorldScope`
              asserts per-file source-text counts of `StreamingTaskDesc{` and
              `.Scope = ...`. Its invariant (every production async submission
              carries its owning world scope) is unchanged by the migration, so
              keep the `.Scope =` designator form in migrated code and update
              only the desc-type token, one lane at a time.
            - `src/runtime/README.md` describes these seams by executor name and
              must be updated in the same commit (docs-sync).
      - [x] **B3 — `SceneDocumentModule`, both submit sites.** Taken before the
            coupled `B2`+`B4` lane because it is the first production consumer
            of `FinalizeUnpublishedOnMainThread`, so it validates `B0` against a
            real consumer while staying inside one module.

            The old shape mapped across cleanly: `.Execute` -> `.Work`,
            `.ApplyOnMainThread` -> `.PublishCompletion`,
            `.FinalizeCancellationOnMainThread` ->
            `.FinalizeUnpublishedOnMainThread`. The envelope carries only a
            `QueuedSceneFileWorkDone` marker — the operation's outcome already
            lives in the shared `QueuedScene{Load,Save}State` the callbacks
            capture, and the envelope's real job here is to be non-empty, since
            an empty envelope is how `JobService` reports a dropped job.

            The one non-mechanical change: the captured-binding check moved out
            of the apply callback into `JobDesc::ValidateBeforeApply`, which is
            the seam the task's required changes name. It maps
            `MatchesCapturedBinding` onto `JobApplyValidation` (`Current` /
            `StaleGeneration`, and `MissingTarget` when the module's state is
            already gone), so a stale result is discarded *before* it can mutate
            anything instead of being defended against inside the mutating
            callback. Behaviour is preserved either way, because the finalizer
            re-checks the same binding and records nothing when it no longer
            holds.

            **The lane also retired the module's "optional async" concept.**
            The executor arrived through
            `setup.Services().Find<StreamingExecutor>()`, an optional module's
            service; `JobService` is a kernel facility reached through
            `setup.Jobs()`, so it is never absent after resolution and the
            null-executor branch had no production reachability left. The
            remaining null window — registered but not yet resolved, when the
            module is already published as a service — is real and still fails
            closed, now covered by
            `SceneDocumentModule.QueuedDocumentIoFailsClosedBeforeResolution`
            through a new register-without-resolve harness entry point. The old
            `PublishesExactServicesSupportsOptionalAsyncAndWithdraws` lost its
            middle clause and its name.

            `RuntimeSceneFileEvent::Task`,
            `RuntimeQueuedSceneFileOperation::Task`, and
            `SandboxEditorSceneFileResult::Task` change from
            `StreamingTaskHandle` to `JobToken`; that last one is why
            `SandboxEditorFacades` is in this diff at all, and its now-unused
            `StreamingExecutor` imports are dropped (it still gets
            `RuntimeTaskKinds` from `JobService` directly).

            Test-side, three lifecycle tests drove the executor by hand
            (`PumpBackground` + `DrainCompletions` + `ApplyMainThreadResults`).
            `JobService` dispatches at submit, so they now wait for
            `AwaitingGate` and drain until terminal, per B0's lesson.
            `RetireWorld` became `CancelAllForWorld` — production already routes
            world destruction there through `WorldRegistry::ApplyMaintenance`.

            Two of those tests could no longer assert an exact terminal state:
            their `GetLastSceneFileEvent()` call itself revalidates the binding,
            which rebinds and cancels the owned task before the drain sees it,
            so the job ends `Cancelled` rather than `StaleDiscarded`. Both
            routes run the finalizer and record no event, so they assert the
            invariant that actually matters (never `Published`, no event). To
            keep the new revalidation seam directly covered rather than
            incidentally, `QueuedSceneLoadIsDiscardedByDrainRevalidationAlone`
            switches the world and drains without touching the module or
            pumping `ActiveWorldChanged`, and asserts `StaleDiscarded` plus the
            stale-discard/published/finalized counters.

            CPU gate 4262/4262 (+2 tests), one expected headless skip;
            `SceneDocumentModule.*` + `RuntimeSceneLifecycle.*` 0/40 failures
            under stress. `src/runtime/README.md` updated in the same commit;
            module inventory unchanged.
      - [x] **B2 + B4 — the import lane, migrated as one change.** Taken
            together as planned: `SandboxDefaultPolicies`' deferred mesh
            post-process receives its executor through
            `RuntimePostImportProcessorServices`, which `AssetImportPipeline`
            populates, so both structs flip from `StreamingExecutor* Streaming`
            to `JobService* Jobs` at once. No dual-field shim was needed —
            `AssetWorkflowModule` already held `state.Jobs = &setup.Jobs()`, so
            the wiring point just passes the pointer it already had, and its
            `Streaming` member, its `Find<StreamingExecutor>()` resolve, and its
            shutdown clear all disappear.

            Three submit sites moved (`Runtime.ImportGeometry`,
            `Runtime.ImportModelTexture`, `Runtime.DirectMeshPostProcess`) with
            the same mapping as B3. `QueuedImportDecodeDone` /
            `DirectMeshPostProcessDone` envelopes name the ingest handle /
            entity they belong to, and each `PublishCompletion` rejects a
            foreign payload instead of applying it against the wrong request.

            **Unlike B3, the staleness check stays inside the publish body.**
            `IsCurrentSubmissionTarget` is not a silent discard here: the import
            first drives the ingest state machine (`CompleteDecode`,
            `BeginApply`) and then records a `FailApply` with an observable
            diagnostic. Routing that through `ValidateBeforeApply` would replace
            the visible failure with the finalizer's `Cancelled` diagnostic, so
            the fail-closed revalidation seam is deliberately not used on this
            lane.

            Renames that follow the surface change:
            `m_StreamingExecutor` -> `m_Jobs`,
            `RuntimeAssetImportStreamingTask{Ingest, Streaming}` ->
            `RuntimeAssetImportJobRecord{Ingest, Job}`,
            `StreamingTaskStateCanCancel` -> `JobStateCanCancel`,
            `QueueStageCanUseStreamingCancellation` ->
            `QueueStageCanUseAsyncCancellation`,
            `FinalizeCancelledStreamingImport` -> `FinalizeUnpublishedImport`.
            Cancellability maps `Pending/Ready/Running/WaitingForReadback` onto
            `AwaitingDependencies/Queued/Running/AwaitingApply`, and
            `WaitingForMainThreadApply` — the state `CancelActiveAssetImportsForShutdown`
            is allowed to cancel from — onto `AwaitingGate`.

            `GetAssetImportQueueSnapshot` used the executor's batched
            `GetStates(handles)`, which `JobService` does not have. The two-pass
            collect-then-query loop collapses into one loop over per-token
            `GetState`; a queue snapshot for the import UI holds a handful of
            entries, so this does not earn a new batch API on the service.

            **One real behaviour change, in world retirement.**
            `StreamingExecutor::RetireWorld` marked a *running* task `Cancelled`
            immediately and made its finalizer drainable while the worker was
            still inside `Execute`. `JobService` claims the unpublished
            finalizer at the terminal transition instead — deliberately, since
            running consumer reconciliation while the worker still owns the
            shared state record is the race `B0` was written to avoid. So a
            retired world now cancels the decode immediately but the visible
            ingest record terminalizes once the worker returns and observes the
            flag. The import still cannot materialize (cancel plus
            submission-target revalidation), and shutdown is unaffected because
            `CancelActiveAssetImportsForShutdown` terminalizes the ingest record
            directly rather than waiting on the job.
            `RetiredWorldImportTerminalizesQueueState` blocked its decode worker
            and waited for the terminal *before* releasing it, which now
            deadlocks by construction; it releases the worker once the world is
            gone and then waits, which additionally exercises the post-run
            cancel path. Every other assertion in it is unchanged.

            Test scaffolding that became vacuous was removed rather than left
            asserting on a service nothing reads: `DirectHarness`'s
            `provideStreaming` flag and `Streaming` member, the
            `Find<StreamingExecutor>()` identity checks, a now-dead
            `PumpBackground` drive, and the structural assertions that
            `AssetWorkflowModule` resolves and clears a `StreamingExecutor`.
            `OmittedAssetWorkflowPlatformDropFailsClosedWithout...` measured the
            executor's diagnostics to prove a drop queued nothing; it now
            measures `JobServiceStats` and is renamed accordingly.

            CPU gate 4262/4262, one expected headless skip;
            `RuntimeAssetImportFormatCoverage.*` + `AssetWorkflowModule.*` 0/30
            failures under stress. `src/runtime/README.md` updated in the same
            commit; module inventory unchanged.
      - [ ] **B5** — `DerivedJobRegistry` consumers. Census re-run after
            `B2`/`B4` landed; this lane is a different shape from `B1`–`B4` and
            must be sub-sliced.

            **`DerivedJobRegistry` is not a second executor — it is a derived-work
            domain layer built on one.** `B1`–`B4` were submit-site swaps because
            `StreamingExecutor` and `JobService` model the same thing. The
            registry additionally owns vocabulary no consumer can drop by
            swapping a `Submit` call:
            - `DerivedJobKey` identity (entity id + `DerivedJobScope` +
              `ProgressiveSlotSemantic` + four generations + output name), used
              for dedup and staleness;
            - `CancelForEntity(entityId)` and `SnapshotEntity(entityId)`;
            - `SnapshotAll()` -> `DerivedJobQueueSnapshot`
              (`DerivedJobSnapshot` per job: name, status, resolved job domain,
              progress, elapsed ms, diagnostic, dependencies) plus
              `DerivedJobQueueDiagnostics`;
            - `SubmitFollowUp(parent, desc, reason)`;
            - `HasPreviousOutput` retention;
            - readback parking (`IsReadbackReady` / `DrainReadbacks`).

            `JobService` already covers follow-ups (`DependsOn`), readback
            parking (`IsReadyToApply`), and staleness (`ValidateBeforeApply`).
            It has **no** keyed identity, no entity-scoped cancel, and no job
            enumeration — it exposes `GetState`/`GetProgress`/`Stats` per token
            only.

            Production consumer census (~30 `DerivedJobDesc` construction sites
            in 5 files, plus two app panels):
            - `Runtime.GpuReadbackJob.cpp` — 1 desc; uses
              `ValidateOnMainThread`, `IsReadbackReady`, `HasPreviousOutput`
            - `Runtime.AssetModelSceneHandoff.cpp` — 4 descs; dependency chains
            - `Runtime.SelectedMeshTextureBake.cpp` — 1 desc
            - `Runtime.SandboxMethodFacade.cpp` — 2 descs + `DerivedJobSnapshot`
              for editor messages
            - `Runtime.SandboxEditorFacades.{cpp,cppm}` — ~23 desc sites plus
              the whole editor presentation layer: `DerivedJobCommands`,
              `DerivedJobHandleToMessage`, `DerivedJobStateSignature`, and
              `SandboxEditorSession::m_DerivedJobSnapshot`
            - `src/app/Sandbox/Editor/Sandbox.{DomainPanels,EditorShell}.cpp` —
              render `SandboxEditorBoundRenderStateRowKind::DerivedJob` rows
            - `Runtime.AsyncWorkModule.cpp` — drains, then cancels every
              survivor returned by `SnapshotAll()` at the shutdown boundary

            **The blocking design question is queue visibility.** The Sandbox
            editor renders one queue view fed by `SnapshotAll()` across *every*
            consumer's jobs, and `AsyncWorkModule` uses the same enumeration to
            cancel survivors at shutdown. No consumer can move off the registry
            in isolation without deciding where that enumeration lives
            afterwards, so this is not sliceable per consumer until it is
            settled. Options considered:
            - **(a)** `JobService` grows a read-only `SnapshotAll()` returning
              generic per-job records (token, debug name, state, scope,
              progress, elapsed). It already stores all of that except elapsed.
              One surface, and the editor row model maps from the generic
              snapshot; key identity and entity-scoped cancel move into the
              consumers that need them (they already hold per-request state).
            - **(b)** A thin runtime-owned `DerivedJobIndex` that composes
              `JobService` and keeps key -> `JobToken` plus the snapshot
              projection. Smallest service change, but it is a new named runtime
              type doing exactly the indirection this task exists to remove.
            - **(c)** Push keying and snapshot state entirely into each
              consumer. Keeps `JobService` untouched, but each consumer
              re-implements enumeration and the single editor queue view has to
              be stitched from several sources.

            **Owner chose (a)** (2026-07-27), with key identity and
            entity-scoped cancel pushed to the consumers that already hold
            per-request state, and the lane sub-sliced with a commit per
            consumer.

            - [x] **B5-0 — `JobService::SnapshotAll()`.** The enabling change,
                  landed on its own so the consumer sub-slices are pure
                  migrations. Adds `JobSnapshot` (token, debug name, state,
                  scope, progress, elapsed) and
                  `std::vector<JobSnapshot> SnapshotAll() const`, plus a
                  `SubmittedAt` steady-clock stamp on the job record.

                  Deliberately generic: no key, no entity id, no domain. Which
                  entity or output a job belongs to is the submitting
                  consumer's business, and putting it on the execution service
                  is what made `DerivedJobRegistry` a second scheduler in the
                  first place.

                  `ElapsedMilliseconds` is age-since-submit measured when the
                  snapshot is taken, and keeps growing after a job is terminal —
                  the retired registry's exact semantics, so the editor's queue
                  rows do not change meaning. Results are sorted by token so a
                  queue view does not reshuffle when the hash map rehashes.
                  Terminal jobs stay visible until `ReapCompleted()`, which is
                  what lets `AsyncWorkModule`'s shutdown sweep still see
                  survivors.

                  Two contract tests: enumeration/ordering/reap lifecycle, and
                  progress + age propagation. CPU gate 4264/4264.
            - [x] **B5a — `Runtime.GpuReadbackJob`.** Taken first: the census
                  showed it has **no production caller at all** — the helper is
                  exercised only by its own CPU contract test and the opt-in
                  `gpu;vulkan` smoke — so it migrates without touching a live
                  workflow.

                  `SubmitGpuReadbackJob(DerivedJobRegistry&, ...)` becomes
                  `SubmitGpuReadbackJob(JobService&, ...)` returning a
                  `JobToken`. Readback parking maps onto `IsReadyToApply` and
                  staleness onto `ValidateBeforeApply`, and the property write
                  moves into `PublishCompletion`.

                  **The separate readback drain disappears.** The registry
                  needed `DrainReadbacks()` to re-poll parked records;
                  `JobService::DrainCompletions` re-polls the readiness gate
                  itself, so the caller sequence collapses from
                  `DrainCompletions` + `DrainReadbacks` + `ApplyMainThreadResults`
                  to one drain.

                  Dropped from `GpuReadbackJobDesc`: `DerivedJobKey Key` (no
                  caller needed it; identity belongs to the consumer, per the
                  B5 decision) and `HasPreviousOutput` (retention was a registry
                  snapshot concern with no `JobService` counterpart and no
                  caller). `ApplyAfterWrite` loses its `DerivedJobApplyContext`
                  parameter — no caller read it — and becomes
                  `move_only_function<Core::Result()>`, with a failure now
                  failing the completion rather than being recorded as a job
                  error, since `PublishCompletion` returning false is the
                  service's equivalent.

                  Test-side: the harness swaps `StreamingExecutor` +
                  `DerivedJobRegistry` for `JobService` + `KernelEventBus` and
                  gains a `SchedulerScope`, since `JobService` dispatches at
                  submit. Assertions move from `DerivedJobStatus` /
                  `StreamingTaskState` to `JobState` (`AwaitingApply` for a
                  parked readback, `Published` on apply) and from
                  `Readbacks.Issued/Waiting/Completed` to `Stats().LastDrainParked`
                  / `AwaitingApplyJobs` plus the new `SnapshotAll()`. The
                  follow-up test's `SubmitFollowUp` becomes a plain `JobDesc`
                  with `DependsOn`. Because the readback body now runs on a
                  worker, the tests wait for `AwaitingGate` before reading the
                  command context it recorded into — that state transition is
                  the synchronising edge.

                  CPU gate 4264/4264; `GpuReadbackJob.*` 0/40 under stress. The
                  `gpu;vulkan` smoke is migrated in the same commit but is
                  opt-in and did not run on this host.
            - [x] **B5b — `Runtime.AssetModelSceneHandoff`.** The progressive
                  enrichment chain: two optional leaf jobs (`generate mesh uv
                  atlas`, `compute mesh vertex normals`) and three dependents
                  (`schedule normal GPU bake request`, `bake normal texture`,
                  `bake albedo texture`). Like `B5a` it has **no production
                  caller** — `AssetModelSceneHandoffOptions::ProgressiveJobs` is
                  set only by this module's four contract tests — so the lane
                  proves the dependency-chain shape without touching a live
                  workflow.

                  `DerivedJobRegistry* ProgressiveJobs` -> `JobService*`;
                  `.Name` -> `.DebugName`, `.Execute` -> `.Work`,
                  `.ApplyOnMainThread` -> `.PublishCompletion`,
                  `DerivedJobDependency` -> `JobDependency`. `DerivedJobDesc`
                  defaulted `Kind` to `GeometryProcess` and `JobDesc` defaults
                  to `Generic`, so every desc sets `Kind` explicitly to keep the
                  lane taxonomy.

                  **All four `DerivedJobKey` constructions are gone**, per the
                  B5 decision. Nothing looked these jobs up by entity, semantic,
                  or generation — the key existed only for the registry's dedup
                  and per-entity enumeration — so `QueueProgressiveNoopJob` also
                  loses its now-unused `entity` and `semantic` parameters.

                  Two apply bodies read `DerivedJobApplyContext::Output`
                  (`PayloadToken`, `Diagnostic`), which `JobService` has no
                  counterpart for because its results are typed. They get a
                  local `ProgressiveEnrichmentResult` record carried in the
                  envelope; the jobs whose apply ignores the payload still
                  return a populated envelope, since an empty one is how
                  `JobService` reports a dropped job. Apply failure maps from
                  `Core::Err` onto `PublishCompletion` returning false.

                  **Test-side, the lane hit a `JobService` property worth
                  recording.** `StreamingExecutor` runs its work inline when
                  `Core::Tasks::Scheduler` is not initialized;
                  `JobService::DispatchJob` calls `Scheduler::Dispatch`
                  unconditionally, and `DispatchInternal` silently drops the
                  task when there is no scheduler. The runtime contract binary
                  initializes no global scheduler, so all four migrated tests
                  gained a `SchedulerScope` — without it the jobs would sit in
                  `Queued` forever rather than failing loudly.

                  `SnapshotEntity(renderId)` becomes `SnapshotAll()` plus a
                  `FindJob(name)` helper — sound here because each test
                  materializes exactly one entity. `JobSnapshot` carries no
                  dependency list, so the two `Dependencies.size() == 1`
                  assertions become the behaviour that list stood for: the
                  dependent sits in `AwaitingDependencies` until a drain
                  releases it (checked against `Stats().AwaitingDependencyJobs`
                  too). That is deterministic because materialization never
                  drains. `PumpDerivedJobs(jobs, 2)` called twice becomes
                  `DrainProgressiveJobsUntilTerminal`, per B0's lesson — a
                  dependency releases from a drain, so a chain needs one drain
                  per level and a fixed count is a race.

                  `Test.RuntimeEngineLayering`'s source-text assertion moves
                  from `DerivedJobDesc ` to `JobDesc `; the `.Scope = ` count is
                  unchanged at 4.

                  CPU gate 4264/4264, one expected headless skip;
                  `RuntimeAssetModelSceneHandoff.*` 0/40 failures under stress.
                  `src/runtime/README.md` updated in the same commit (module row
                  plus the "Streaming integration" migration state); module
                  inventory unchanged.
            - [x] **B5c — `Runtime.SelectedMeshTextureBake`.** One desc, and the
                  first B5 lane whose `ValidateOnMainThread` staleness check has
                  real content. It maps onto `ValidateBeforeApply`: a vanished
                  entity or missing `ProgressivePresentationBindings` becomes
                  `MissingTarget`, a bumped `BindingGeneration` becomes
                  `StaleGeneration`. This is the *silent-discard* case B4's
                  lesson describes — the consumer publishes no diagnostic of its
                  own for a stale result — so `ValidateBeforeApply` is the right
                  home, unlike the import lane.

                  Like `B5a`/`B5b` there is no production caller:
                  `SelectedMeshTextureBakeContext::Jobs` is set only by this
                  module's two contract tests, since `TextureBakeModule` binds
                  the context with `.AssetService` alone.

                  Renames that follow the surface change:
                  `SelectedMeshTextureBakeContext::DerivedJobs` -> `Jobs`,
                  `SelectedMeshTextureBakeRequest::PreferDerivedJob` ->
                  `PreferAsyncJob`,
                  `SelectedMeshTextureBakeExecutionMode::DerivedJob` ->
                  `AsyncJob`, and `SelectedMeshTextureBakeResult::Job` /
                  `SandboxEditorTextureBakeCommandResult::Job` from
                  `DerivedJobHandle` to `JobToken`. That last one is the only
                  reason `SandboxEditorFacades` is in this diff; nothing reads
                  the field, and the editor's other progressive-job models still
                  speak `DerivedJobHandle` until `B5e`.

                  The bake payload itself already travelled to the main thread
                  in the shared `bakeState` slot rather than in the job output,
                  so the envelope only needs to be non-empty and typed: a local
                  `SelectedMeshBakeJobResult` carries the retired
                  `DerivedJobOutput`'s two observable fields.

                  **A test-only defect this lane surfaced, worth its own note.**
                  Making the tests real (a `SchedulerScope`, per B5b's lesson)
                  turned `AssetService::Load` from an inline call into a worker
                  decode, and the first version declared `SchedulerScope` first
                  — so it was destroyed *last*, after the `AssetService` it
                  feeds. A worker inside `AssetLoadPipeline::OnCpuDecoded` then
                  ran against a destroyed pipeline: ~35% of runs hung in
                  `Scheduler::WaitForAll` or aborted. Rule, now stated in both
                  files: **declare `SchedulerScope` last**, so the pool is
                  quiesced while everything a worker can reach is still alive.
                  The B5b tests already satisfied it incidentally; they were
                  reordered to satisfy it deliberately.

                  CPU gate 4264/4264, one expected headless skip;
                  `RuntimeSelectedMeshTextureBake.*` +
                  `RuntimeAssetModelSceneHandoff.*` 0/40 under stress (and
                  0/60 for the bake suite alone after the ordering fix).
                  `src/runtime/README.md` updated in the same commit; module
                  inventory unchanged.
            - [ ] **B5d+e — the editor lane. Re-planned 2026-07-27: `B5d` and
                  `B5e` cannot land separately.** The original split assumed
                  `SandboxMethodFacade` was its own consumer. It is not: both
                  files submit through the *same* surface,
                  `SandboxEditorDerivedJobCommandSurface`, whose members are
                  `std::function<DerivedJobHandle(DerivedJobDesc)> Submit` and
                  `std::function<void(DerivedJobHandle)> Cancel`
                  (`Runtime.SandboxEditorFacades.cppm:2091`). Retyping it to
                  `JobDesc`/`JobToken` breaks every desc site at once, and
                  leaving it typed on the registry means `SandboxMethodFacade`
                  cannot move at all.

                  Measured coupling:
                  - `DerivedJobDesc` construction sites: 23 in
                    `SandboxEditorFacades.cpp`, 2 in `SandboxMethodFacade.cpp`
                  - `DerivedJobCommands.Submit` calls: 14 + 2
                  - `DerivedJobKey` uses: 9 + 3
                  - `DerivedJob` references in the app panels: 2 in
                    `Sandbox.DomainPanels.cpp`, 2 in `Sandbox.EditorShell.cpp`

                  **The submit path and the presentation path are also
                  inseparable**, because the dedup guard reads both.
                  `FindActiveEditorDerivedJob(context, key)`
                  (`SandboxEditorFacades.cpp:4787`) scans
                  `context.DerivedJobs->Entries` — the editor's
                  `DerivedJobQueueSnapshot` — for an active entry whose
                  `DerivedJobKey` names the same entity+output, and refuses a
                  duplicate submission. So the desc's `Key` (submit path) and
                  the queue snapshot (presentation path) are one mechanism. The
                  key disappears with `JobDesc`, so both move together or
                  neither does.

                  **Where the identity goes.** Per the Slice B5 decision, into
                  the consumer that already holds per-request state:
                  `SandboxEditorSession`, which owns `m_DerivedJobSnapshot`
                  today. It grows a `key -> JobToken` index refreshed from
                  `JobService::SnapshotAll()`; `SandboxEditorContext` exposes an
                  "is this entity+output already busy?" query plus the queue-row
                  projection instead of a raw registry snapshot. This is the
                  first B5 lane where that decision has to be *implemented*
                  rather than simply applied by deleting an unused key.

                  #### Chosen route: dual surface, batched (owner, 2026-07-27)

                  The one-commit route was rejected: the tree would not build
                  until the whole lane was done, with no intermediate green
                  gate. Instead `SandboxEditorDerivedJobCommandSurface` carries
                  **both** submit paths for the duration of the lane:

                  ```cpp
                  std::function<DerivedJobHandle(DerivedJobDesc)> Submit{};        // retiring
                  std::function<void(DerivedJobHandle)> Cancel{};                  // retiring
                  std::function<JobToken(JobDesc, SandboxEditorJobIdentity)> SubmitJob{};
                  std::function<void(JobToken)> CancelJob{};
                  ```

                  This is a **documented temporary migration exception** under
                  `AGENTS.md`: it lives in this active task, its removal task ID
                  is `RUNTIME-194` itself (sub-slice `B5d-1z` below), it is
                  bounded to this lane, and it adds no violation to a promoted
                  final layer. The dedup guard and the queue view **union** both
                  sources while the window is open, so a duplicate submission is
                  refused no matter which path queued the original.

                  #### Batch plan and status

                  - [x] **B5d-1a — editor-owned job vocabulary + first
                    consumer.** Landed. Added `SandboxEditorJobScope` (the
                    editor-local successor to `DerivedJobScope`, `MeshSurface`
                    included), `SandboxEditorJobIdentity`,
                    `SandboxEditorJobDependency`, `SandboxEditorJobRecord`,
                    `SandboxEditorJobQueueSnapshot`, and the `SubmitJob` /
                    `CancelJob` pair. Retyped the presentation models
                    (`SandboxEditorProgressiveJobModel`, its dependency model,
                    and `SandboxEditorBoundRenderStateRow::Job`/`JobStatus`)
                    onto that vocabulary so later batches move desc sites
                    without touching presentation again. `JobState` became the
                    editor's status vocabulary outright — `Runtime.JobService`
                    gained `ToString(JobState)` for it — and the two Sandbox app
                    panels only needed their `DerivedJobGraph` import swapped.

                    `SandboxEditorSession` now builds one
                    `SandboxEditorJobQueueSnapshot` from both surfaces and owns
                    an `m_JobIdentities` (`JobToken -> identity`) table filled at
                    submit and pruned against `SnapshotAll()` each frame, so a
                    reaped job's identity cannot leak.
                    `ToSandboxEditorJobQueueSnapshot(DerivedJobQueueSnapshot)` is
                    exported for the window so registry-path tests can still
                    inject a queue; it goes with `B5d-1z`.

                    `Runtime.SandboxMethodFacade` is fully migrated (K-Means CPU,
                    Progressive Poisson point-cloud and mesh-surface CPU): its
                    two descs, both worker bodies, the five-way validation
                    mapping onto `JobApplyValidation`, and its three
                    `Available()` gates, which became `JobsAvailable()`.

                    Findings worth keeping:
                    - **The `Available()` gate is the trap in this lane.** It
                      tests `Submit` only, so a migrated call site silently falls
                      through to the *synchronous* path instead of failing —
                      the K-Means test caught it because labels appeared
                      immediately. Every batch must flip its own gates.
                    - The retired `DerivedJobKey::SourcePropertyGeneration` was
                      never part of the dedup comparison; the staleness it stood
                      for is re-checked by `ValidateBeforeApply`. Dropping it
                      from the identity is safe, and the same will hold for the
                      remaining batches.
                    - The registry wrote its validation reason into the job's
                      diagnostic; `JobService` has no per-job diagnostic
                      channel, so `JobState::StaleDiscarded` is the observable
                      fact a stale-discard test can assert. One assertion on
                      `"StaleSourcePropertyGeneration"` was dropped for this.
                    - Pre-drain `Queued`/`AwaitingGate` assertions are racy
                      under `JobService` (it dispatches at submit); they became
                      `IsActiveSandboxEditorJobState(...)`, which is what the
                      test actually meant.

                    New shared test helper
                    `tests/support/SandboxEditorJobHarness.hpp` mirrors the
                    session's identity/join for contract tests that drive editor
                    facades without a session. It owns its scheduler as its
                    **last** member, per the B5c ordering rule.

                    CPU gate 4264/4264, one expected headless skip;
                    `SandboxEditorUi.*` 0/40 under stress.
                    `src/runtime/README.md` updated in the same commit; module
                    inventory unchanged.
                  - [x] **B5d-1b — `SandboxEditorFacades` mesh-method descs.**
                    Landed. **The "23 desc sites" are really 5 desc factories**,
                    which is the useful batching unit:
                    `MakeMeshCpuJobDesc` (5 callers: curvature, denoise, remesh,
                    subdivide, simplify), `MakeVertexNormalsCpuJobDesc` (3),
                    `MakePointCloudOutlierRemovalCpuJobDesc` (1),
                    `MakeRegistrationCpuJobDesc` (1),
                    `MakeUvRegenerationCpuJobDesc` (1).

                    This batch took `MakeMeshCpuJobDesc`: split into
                    `MakeMeshCpuJobIdentity` + a `JobDesc` builder, converted
                    the five worker bodies and `RunMeshCpuWorker` to
                    `JobResultEnvelope` over a local `SandboxEditorJobResult`,
                    mapped `ValidateMeshCpuJobApply` onto `JobApplyValidation`,
                    and flipped the five `Available()` gates.

                    Same three test adjustments as `B5d-1a`, for the same
                    reasons: harness instead of registry wiring, racy pre-drain
                    `Queued`/`AwaitingGate` assertions relaxed to
                    `IsActiveSandboxEditorJobState`, and three
                    `"StaleSourcePropertyGeneration"` diagnostic assertions
                    dropped in favour of `JobState::StaleDiscarded`.

                    `ProductionAsyncSubmissionsCarryOwningWorldScope` now counts
                    both desc types in `SandboxEditorFacades` (4 registry + 1
                    service = the same 5); the `.Scope = context.World` total is
                    unchanged, which is the invariant it actually guards.

                    CPU gate 4264/4264; `SandboxEditorUi.*` (143 tests) 0/40
                    under stress.
                  - [x] **B5d-1c — vertex-normals, point-cloud
                    outlier-removal, and registration descs.** Landed. Three
                    factories (`MakeVertexNormalsCpuJobDesc` with 3 callers,
                    `MakePointCloudOutlierRemovalCpuJobDesc`,
                    `MakeRegistrationCpuJobDesc`) split into identity + `JobDesc`
                    builders, six worker bodies converted, three validators
                    mapped onto `JobApplyValidation`, five submit sites and five
                    `Available()` gates flipped. Registration's key carried both
                    the source *and* target geometry signatures; neither was in
                    the dedup comparison and both are re-checked by
                    `ValidateRegistrationCpuJobApply`, so both leave the
                    identity. Same three test adjustments as the earlier
                    batches. CPU gate 4264/4264; `SandboxEditorUi.*` 0/30 under
                    stress.
                  - [ ] **B5d-1d — `MakeUvRegenerationCpuJobDesc`**, the last
                    factory. It is the only remaining `return DerivedJobDesc{`
                    in `SandboxEditorFacades.cpp` and owns the one surviving
                    `DerivedJobCommands.Available()` gate
                    (`SubmitUvRegenerationCpuJob`), plus
                    `RunUvRegenerationCpuWorker` /
                    `ValidateUvRegenerationCpuJobApply` and the four
                    `UvRegeneration*` contract tests. Note it also has a
                    *synchronous* caller path that reads
                    `const DerivedJobWorkerResult worker = ...` directly, which
                    the earlier factories did not.
                  - [ ] **B5d-1z — close the window.** Delete `Submit`/`Cancel`,
                    `context.DerivedJobs`, `m_DerivedJobSnapshot`, the union in
                    the dedup guard and the queue view, and the registry-backed
                    adapters. After this `SandboxEditorFacades` and
                    `SandboxMethodFacade` no longer name `DerivedJobGraph`.
                  - [ ] **B5d-2 — `AsyncWorkModule`'s shutdown survivor sweep.**
                    Separable: it needs only `SnapshotAll()` plus `Cancel`, both
                    of which already exist.

                  Each batch keeps the default CPU gate green and commits on its
                  own. **A batch that cannot go green is reverted, not
                  weakened.**

                  #### What has real behaviour

                  Everything except the dedup guard is mechanical. Re-read
                  `FindActiveEditorDerivedJob` (`SandboxEditorFacades.cpp:4787`)
                  and its ~11 call sites before touching it: it refuses a
                  duplicate submission when an entry with a **non-terminal**
                  status matches on `EntityId` + `Domain` + `OutputSemantic` +
                  `OutputName` (`SameEditorDerivedJobOutput`, `:4776`) — the
                  four generations on `DerivedJobKey` are *not* part of that
                  comparison; they were staleness data, and staleness now lives
                  in each job's own `ValidateBeforeApply`.

                  Fields the registry snapshot carried that `JobService` has no
                  counterpart for, and which the consumer must therefore record
                  at submit: `RequestedJobDomain` / `ResolvedJobDomain`,
                  `Dependencies`, `Diagnostic`, `PayloadToken`,
                  `PreviousOutputRetained`.

                  Unlike `B5a`–`B5c`, this lane has live production callers, so
                  the existing `SandboxEditorUi.*` / `SandboxEditor*` contract
                  tests are behaviour tests, not shape tests.
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
