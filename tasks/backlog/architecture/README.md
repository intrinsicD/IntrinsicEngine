# Architecture Backlog

Architecture and layering decisions, ADR-shaped tasks, and architecture
governance work. The authoritative engine contract is
[`/AGENTS.md`](../../../AGENTS.md); this directory tracks proposed or pending
decisions that may extend or refine that contract.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

Opened from the 2026-07-03 main-loop/task-graph/render-graph review
([`docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`](../../../docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md)):

- [`CORE-005`](CORE-005-nonblocking-taskgraph-submit-api.md) — non-blocking
  TaskGraph submission/completion API (gated on `BUG-055`).
- [`CORE-006`](CORE-006-domain-free-core-task-vocabulary.md) — domain-free
  core task/DAG vocabulary (opaque task kinds; GPU/streaming naming out of
  core).
- [`CORE-007`](CORE-007-scheduler-priority-wait-wake-hardening.md) —
  scheduler priority lanes, waiter stealing, wake and hot-path hardening.
- [`CORE-008`](CORE-008-compiled-taskgraph-plan-reuse.md) — compiled
  task-graph plan reuse across executions (fixed-step FrameGraph and
  render-prep adoption).
- [`CORE-009`](CORE-009-app-owned-config-sections.md) — app-owned config
  sections out of core `EngineConfig` (Progressive Poisson block moves
  behind a generic section mechanism).
- [`ARCH-006`](ARCH-006-sandbox-editor-content-out-of-runtime.md) — move
  Sandbox application editor content from `runtime` to `app` (sliced
  migration; planning slice first) and own the top `.cppm` compile-hotspot
  candidates for `Runtime.SandboxEditorUi.cppm` / `Sandbox.cppm`.

Opened from the 2026-07-08 kernel/module architecture decision record
([`docs/adr/0024-kernel-module-architecture.md`](../../../docs/adr/0024-kernel-module-architecture.md));
seams-first migration order. All additive seams (`ARCH-007`..`ARCH-011`), the
proving extraction (`ARCH-012`), and the post-seam collision sweep (`ARCH-013`)
are retired to `tasks/done/`; `ARCH-014` remains the open umbrella:

- [`ARCH-014`](ARCH-014-kernel-convergence-tracking.md) — umbrella
  north-star: owns the [kernel target-state](../../../docs/architecture/kernel-target-state.md)
  convergence scorecard and the review/ratchet guardrail; stays open
  until the whole ADR-0024 migration is reached.
- [`ARCH-015`](ARCH-015-runtime-module-scope-by-result-consumer-contract.md) —
  grilling + ADR to decide the `IRuntimeModule` scoping rule (cluster methods by
  shared result-consumer contract, e.g. DBSCAN inside `ClusteringModule`, not a
  rigid per-algorithm interface). Decision record only; no engine code.

### Retired seam tasks

- [`ARCH-007`](../../done/ARCH-007-kernel-command-bus-single-drain-point.md) —
  kernel command bus with a single pre-sim drain point (plain-data payloads,
  correlation IDs, fail-closed handlers), retired 2026-07-08 at
  `CPUContracted`; `ARCH-012` closed its `Operational` proof.
- [`ARCH-008`](../../done/ARCH-008-kernel-event-bus-queued-only.md) —
  queued-only kernel event bus with two pump points, worker-safe publish, and
  next-pump cascade deferral, retired 2026-07-08 at `CPUContracted`;
  `ARCH-012` closed its `Operational` proof.
- [`ARCH-009`](../../done/ARCH-009-kernel-jobservice-snapshot-in-result-out.md) —
  kernel JobService for snapshot-in/result-out multi-frame background jobs,
  world-scoped cancellation, main-thread completion publication, and
  maintenance reaping, retired 2026-07-08 at `CPUContracted`; `GpuQueue`
  execution remains deferred to `RUNTIME-137`, and `ARCH-012` closed its
  `Operational` proof.
- [`ARCH-010`](../../done/ARCH-010-kernel-worldregistry-deferred-world-ops.md) —
  kernel WorldRegistry with boot world #0, deferred active-world changes,
  two-phase world destruction, scoped job cancellation, and explicit extraction
  world handles, retired 2026-07-08 at `CPUContracted`; preview/switch policy
  remains out of scope, and `ARCH-012` closed its `Operational` proof.
- [`ARCH-011`](../../done/ARCH-011-runtimemodule-contract-service-registry.md) —
  `IRuntimeModule`, `EngineSetup`, two-phase ServiceRegistry, module sim-system
  registration, and frame-phase hooks, retired 2026-07-08 at `CPUContracted`;
  `ARCH-012` closed its `Operational` proof.
- [`ARCH-012`](../../done/ARCH-012-clusteringmodule-proving-extraction.md) —
  ClusteringModule proving extraction onto the kernel seams, retired
  2026-07-08 at `Operational`; Sandbox composes the module through
  `RunKMeans` command → `JobService` snapshot → completion event →
  main-thread label commit → `ClusterLabelsChanged` visualization reaction.
  `Runtime.Engine.cppm`/`.cpp` no longer import or name `KMeans*`; the remaining
  Vulkan queue move is owned by `RUNTIME-137`.
- [`ARCH-013`](../../done/ARCH-013-post-seam-collision-rereview.md) —
  post-seam collision re-review, retired 2026-07-08 as task-governance work:
  every inventory row carries a dated decision, `RUNTIME-129` is re-gated on
  `RUNTIME-137`, and no open backlog task now prescribes rejected ADR-0024
  mechanisms without a recorded justification.

## Retired Legacy Program

`src/legacy/` is retired. The final sweep first retired the remaining legacy
consumer tests under
[`LEGACY-012`](../../done/LEGACY-012-migrate-legacy-consumer-tests.md), then
deleted subtrees consumers-first and foundation-last:
[`LEGACY-010`](../../done/LEGACY-010-delete-src-legacy-runtime.md) →
[`LEGACY-008`](../../done/LEGACY-008-delete-src-legacy-graphics.md) →
[`LEGACY-001`](../../done/LEGACY-001-delete-src-legacy-interface.md) /
[`LEGACY-006`](../../done/LEGACY-006-delete-src-legacy-ecs.md) /
[`LEGACY-004`](../../done/LEGACY-004-delete-src-legacy-asset.md) →
[`LEGACY-009`](../../done/LEGACY-009-delete-src-legacy-rhi.md) →
[`LEGACY-005`](../../done/LEGACY-005-delete-src-legacy-core.md).

The current record lives in
[`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md)
and the narratives in [`tasks/done/RETIREMENT-LOG.md`](../../done/RETIREMENT-LOG.md).

## Convergence

These tasks contribute to **Theme F — Architecture/runtime/UI foundation
seeds** in the convergence map. Layering invariants remain owned by
`AGENTS.md`; any change here that introduces a new dependency edge or source
root must update the relevant `docs/architecture/*` doc set in the same PR per
[`docs/agent/docs-sync-policy.md`](../../../docs/agent/docs-sync-policy.md).

## Related

- [`docs/architecture/index.md`](../../../docs/architecture/index.md).
- [`docs/agent/architecture-review-checklist.md`](../../../docs/agent/architecture-review-checklist.md).

## Retired

- [ARCH-007 — Kernel command bus with a single pre-sim drain point](../../done/ARCH-007-kernel-command-bus-single-drain-point.md)
  (done, 2026-07-08, `CPUContracted`, PR #1010): first ADR-0024 kernel seam.
  `Extrinsic.Runtime.CommandBus` ships plain-data commands with correlation
  IDs, thread-safe enqueue, a single pre-sim drain in `Engine::RunFrame()`,
  fail-closed missing-handler diagnostics, a history-hook seam with
  re-enqueueable inverse envelopes, `DiscardPending()` on shutdown, and the
  built-in `QuitRequested` command. No-RTTI/no-exceptions compliant via
  `Core::TypeToken` type identity. `Operational` owned by `ARCH-012`.

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [LEGACY-011 — Value-gated legacy feature reimplementation map](../../done/LEGACY-011-src-legacy-feature-reimplementation-map.md)
  (done, 2026-06-18, `Scaffolded` planning map):
  closes the cross-domain child-task map after every retained/deferred legacy
  feature candidate gained a named done-task decision or explicit future
  trigger. The final legacy-removal blockers later retired under `LEGACY-012`
  and the ordered subtree-deletion tasks.
- [LEGACY-013 — Migrate promoted Core imports off legacy modules](../../done/LEGACY-013-promoted-core-import-migration.md)
  (done, 2026-06-18, `CPUContracted`):
  moved promoted geometry/runtime imports from bare legacy `Core.*` modules to
  `Extrinsic.Core.*` and removed the `IntrinsicCore` link from promoted
  geometry. Four directly affected geometry tests now consume promoted Core
  types; `LEGACY-014` later reduced the remaining Core test-consumer set to 43
  files.
- [LEGACY-014 — Remove unused RuntimeGraph legacy Core test import](../../done/LEGACY-014-runtimegraph-core-test-import.md)
  (done, 2026-06-18, `CPUContracted`):
  removed the unused bare `Core` import from
  `tests/unit/geometry/Test_RuntimeGraph.cpp`; `LEGACY-015` later reduced the
  remaining Core test-consumer set to 42 files.
- [LEGACY-015 — Migrate CoreError test to promoted Core](../../done/LEGACY-015-core-error-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the CoreError unit test to `Extrinsic.Core.Error` and renamed it to
  `tests/unit/core/Test.CoreError.cpp`; `LEGACY-016` later reduced the
  remaining Core test-consumer set to 41 files.
- [LEGACY-016 — Migrate LogRingBuffer test to promoted Core](../../done/LEGACY-016-log-ring-buffer-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the LogRingBuffer unit test to `Extrinsic.Core.Logging` and renamed
  it to `tests/unit/core/Test.LogRingBuffer.cpp`; `LEGACY-017` later reduced
  the remaining Core test-consumer set to 40 files.
- [LEGACY-017 — Retire duplicate legacy CoreHash test](../../done/LEGACY-017-core-hash-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  retired duplicate legacy `Test_CoreHash.cpp` coverage in favor of promoted
  `Extrinsic.Core.Hash` coverage now named `tests/unit/core/Test.CoreHash.cpp`;
  `LEGACY-019` later reduced the remaining Core test-consumer set to 39 files.
- [LEGACY-019 — Migrate StrongHandle test to promoted Core](../../done/LEGACY-019-strong-handle-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the full strong-handle unit test to promoted
  `Extrinsic.Core.StrongHandle`, renamed it to
  `tests/unit/core/Test.CoreStrongHandle.cpp`, and removed the smaller
  legacy-suffixed wrapper test; `LEGACY-020` later reduced the remaining Core
  test-consumer set to 38 files.
- [LEGACY-020 — Migrate CoreTasks test to promoted Core](../../done/LEGACY-020-core-tasks-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the full scheduler/counter-event unit test to promoted
  `Extrinsic.Core.Tasks` and promoted telemetry stats, renamed it to
  `tests/unit/core/Test.CoreTasks.cpp`, and removed the smaller
  legacy-suffixed wrapper test; `LEGACY-021` later reduced the remaining Core
  test-consumer set to 37 files.
- [LEGACY-021 — Migrate profiling test to promoted Core](../../done/LEGACY-021-core-profiling-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the profiling/telemetry unit test to promoted
  `Extrinsic.Core.Telemetry` and `Extrinsic.Core.Hash`, renamed it to
  `tests/unit/core/Test.CoreProfiling.cpp`; `LEGACY-022` later reduced the
  remaining Core test-consumer set to 35 files.
- [LEGACY-022 — Migrate CoreFrameGraph test to promoted Core](../../done/LEGACY-022-core-framegraph-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the Core frame-graph unit test and type-token helper to promoted
  `Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Hash`, and
  `Extrinsic.Core.Tasks`, renamed them to `Test.CoreFrameGraph.*`;
  `LEGACY-023` later reduced the remaining Core test-consumer set to 34 files.
- [LEGACY-023 — Retire legacy Core.Commands test](../../done/LEGACY-023-retire-core-commands-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy-only `tests/unit/core/Test_CoreCommands.cpp` after `CORE-002`
  and `RUNTIME-102` assigned promoted command-history ownership to runtime;
  `LEGACY-024` later reduced the remaining Core test-consumer set to 32 files.
- [LEGACY-024 — Retire legacy Core feature-catalog tests](../../done/LEGACY-024-retire-core-feature-catalog-tests.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy-only `tests/unit/core/Test_FeatureRegistry.cpp` and
  `tests/unit/core/Test_SystemFeatureCatalog.cpp` after `CORE-002` decided not
  to promote the global feature catalog shape; `LEGACY-025` later reduced the
  remaining Core test-consumer set to 31 files.
- [LEGACY-025 — Retire legacy Core.InplaceFunction test](../../done/LEGACY-025-retire-core-inplace-function-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy-only `tests/unit/core/Test_InplaceFunction.cpp` because
  `Core.InplaceFunction` has no promoted `Extrinsic.Core` endpoint; remaining
  `LEGACY-026` later reduced the remaining Core test-consumer set to 30 files.
- [LEGACY-026 — Retire legacy Core.DAGScheduler test](../../done/LEGACY-026-retire-core-dagscheduler-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/core/Test_DAGScheduler.cpp` compatibility coverage
  because promoted `Extrinsic.Core.Dag.Scheduler` and graph-compiler tests own
  the retained DAG scheduling contract; `LEGACY-027` later reduced the
  remaining Core test-consumer set to 29 files.
- [LEGACY-027 — Migrate CoreMemory test to promoted Core](../../done/LEGACY-027-core-memory-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated retained memory allocator coverage from
  `tests/unit/core/Test_CoreMemory.cpp` to promoted
  `tests/unit/core/Test.CoreMemory.cpp` and removed the smaller
  `Test.Core.MemoryLegacy.cpp` parity file; `LEGACY-028` later reduced the
  remaining Core test-consumer set to 28 files.
- [LEGACY-028 — Migrate ArchitectureSLO test to promoted Core](../../done/LEGACY-028-architecture-slo-test-promoted.md)
  (done, 2026-06-18, `CPUContracted`):
  migrated the architecture SLO benchmark test from
  `tests/benchmark/slo/Test_ArchitectureSLO.cpp` to promoted
  `tests/benchmark/slo/Test.ArchitectureSLO.cpp`; `LEGACY-029` later reduced
  the remaining Core test-consumer set to 27 files.
- [LEGACY-029 — Retire legacy Core.Benchmark test](../../done/LEGACY-029-retire-core-benchmark-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy-only `tests/benchmark/Test_Benchmark.cpp` coverage for
  `Core.Benchmark::BenchmarkRunner` while moving promoted pass-timing telemetry
  assertions to `tests/unit/core/Test.CoreProfiling.cpp`; remaining Core
  deletion blockers are 27 tests and legacy-internal subtree ordering.
- [LEGACY-030 — Retire legacy ECS entity-command test](../../done/LEGACY-030-retire-ecs-entity-commands-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired duplicate legacy `tests/unit/ecs/Test_EntityCommands.cpp` coverage
  because promoted `Test.EditorCommandHistory.cpp` and ECS scene/hierarchy
  tests own the retained undo/redo, lifecycle, and hierarchy contracts;
  remaining Core deletion blockers are 26 tests and remaining ECS external
  blockers are 24 tests.
- [LEGACY-031 — Retire legacy ECS frame-graph systems test](../../done/LEGACY-031-retire-ecs-framegraph-systems-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/ecs/Test_FrameGraphSystems.cpp` coverage because
  promoted ECS transform/bounds/render-sync and
  `Test.RuntimeEcsSystemBundle.cpp` own the retained system-bundle contracts,
  while legacy `AxisRotator` stays sample-only behavior; remaining Core
  deletion blockers are 25 tests and remaining ECS external blockers are 23
  tests.
- [LEGACY-032 — Resolve legacy `Runtime.SystemBundles` test migration](../../done/LEGACY-032-resolve-runtime-system-bundles-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/runtime/Test_RuntimeSystemBundles.cpp` coverage
  after mapping retained fixed-step ECS activation to
  `Test.RuntimeEcsSystemBundle.cpp`, graphics lifecycle names to existing
  graphics/runtime contracts, and the old global feature-catalog ordering to an
  explicit retirement decision; remaining Core deletion blockers are 24 tests,
  remaining ECS external blockers are 22 tests, and remaining Runtime external
  blockers are 18 tests.
- [LEGACY-033 — Retire legacy RuntimeEngineConfig test](../../done/LEGACY-033-retire-runtime-engine-config-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/runtime/Test_RuntimeEngineConfig.cpp` coverage
  because its old scalar validation fields do not map to promoted
  `Extrinsic.Core.Config.Engine`; remaining Runtime external blockers are 17
  tests.
- [LEGACY-034 — Resolve legacy runtime frame-loop and maintenance tests](../../done/LEGACY-034-resolve-runtime-frame-loop-maintenance-tests.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/runtime/Test_RuntimeFrameLoop.cpp` and
  `tests/unit/runtime/Test_MaintenanceLane.cpp` after mapping retained CPU/null
  frame-loop and maintenance ordering to promoted core/runtime contracts,
  retiring the legacy feature-catalog rollback mode, and splitting Vulkan
  deferred-destruction behavior to `LEGACY-035`; remaining Core deletion
  blockers are 22 tests, remaining Graphics external blockers are 38 tests,
  remaining RHI external blockers are 17 tests, and remaining Runtime external
  blockers are 15 tests.
- [LEGACY-035 — Resolve legacy RHI deferred-destruction tests](../../done/LEGACY-035-resolve-legacy-rhi-deferred-destruction-tests.md)
  (done, 2026-06-18, `CPUContracted`):
  retired the old `MaintenanceLaneGpuTest` insertion-order, real-buffer
  deferred-destroy, and multi-frame retirement assertions as legacy RHI
  implementation details because promoted Vulkan keeps deferred deletion
  backend-private; remaining RHI external blockers stay at 17 tests.
- [LEGACY-036 — Retire legacy EventBus test](../../done/LEGACY-036-retire-legacy-event-bus-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/core/Test_EventBus.cpp` because promoted ECS owns
  event payloads but not the old scene dispatcher, while promoted runtime owns
  selection mutation through `SelectionController`; remaining ECS external
  blockers are 21 tests and remaining Runtime external blockers are 14 tests.
- [LEGACY-037 — Retire legacy AssetIngestService test](../../done/LEGACY-037-retire-legacy-asset-ingest-service-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/assets/Test_AssetIngestService.cpp` because the
  old dependency-heavy `Runtime.AssetIngestService` constructor surface is not
  a promoted endpoint; promoted ingest ownership is the
  `Extrinsic.Runtime.AssetIngestStateMachine` plus asset/runtime handoff
  contracts. Remaining external blockers are 9 Asset tests, 21 Core tests, 37
  Graphics tests, 16 RHI tests, and 13 Runtime tests.
- [LEGACY-038 — Retire legacy runtime selection modes test](../../done/LEGACY-038-retire-runtime-selection-modes-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/contract/runtime/Test.RuntimeSelectionModes.cpp` after
  preserving retained add/toggle/replace/background multi-selection behavior in
  promoted `SelectionController` tests; remaining ECS external blockers are 20
  tests and remaining Runtime external blockers are 12 tests.
- [LEGACY-039 — Retire legacy element-selection test](../../done/LEGACY-039-retire-legacy-element-selection-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/integration/runtime/Test_ElementSelection.cpp` because
  promoted primitive selection uses `PrimitiveSelectionRefinement`,
  engine-owned refined-pick caching, and editor selection models rather than
  mutable per-entity sub-element sets; the remaining ECS/Runtime external
  blockers later retired under `LEGACY-012` and the final deletion sweep.
- [LEGACY-040 — Retire legacy Asset.Manager safety test](../../done/LEGACY-040-retire-legacy-asset-manager-safety-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/assets/Test_CoreAssetSafety.cpp` because its
  `Asset.Manager` loader-safety and pointer-returning error-path compatibility
  surface is replaced by promoted `AssetService`, `AssetRegistry`,
  `AssetPayloadStore`, and `AssetLoadPipeline` contracts; the remaining
  external blockers later retired under `LEGACY-012`.
- [LEGACY-041 — Retire legacy Asset.Manager core test](../../done/LEGACY-041-retire-legacy-asset-manager-core-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/assets/Test_CoreAssets.cpp` because its
  async/cache/lease/clear/TryGetFast manager compatibility surface is replaced
  by promoted asset service, registry, payload-store, load-pipeline, event-bus,
  and runtime handoff contracts; the remaining external blockers later retired
  under `LEGACY-012`.
- [LEGACY-042 — Retire legacy Asset.Pipeline test](../../done/LEGACY-042-retire-legacy-asset-pipeline-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy `tests/unit/assets/Test_AssetPipeline.cpp` and its grouped
  `AssetPipelineHeadless` CTest because promoted streaming/upload ownership is
  split across `AssetLoadPipeline`, `AssetService`, `GpuAssetCache`, and
  runtime model/texture handoffs; the remaining external blockers later retired
  under `LEGACY-012`.
- [LEGACY-018 — Retire legacy Interface panel-registration test](../../done/LEGACY-018-retire-interface-panel-registration-test.md)
  (done, 2026-06-18, `CPUContracted`):
  retired legacy-only `tests/contract/ui/Test_PanelRegistration.cpp`. The
  remaining legacy-internal Interface consumers later retired with
  `LEGACY-010`, `LEGACY-008`, and `LEGACY-001`.
- [CORE-002 — Command and feature catalog contract](../../done/CORE-002-command-feature-catalog-contract.md)
  (done, 2026-06-09, `CPUContracted` / explicit retirement decision):
  resolves remaining legacy `Core.Commands`, `Core.FeatureRegistry`, and
  `Core.SystemFeatureCatalog` behavior as promoted narrow contracts, runtime/UI
  follow-ups, or explicit retirements.
- [ARCH-005 — Resolve graphics/RHI platform layering violations](../../done/ARCH-005-resolve-graphics-platform-layering-violations.md)
  (done 2026-05-17): removed the four strict-layering failures where
  promoted graphics/RHI targets imported or linked `platform` for
  window/surface inputs. Landed jointly with [`WORKSHOP-002`](../../done/WORKSHOP-002-remove-platform-window-from-rhi.md);
  `RHI::IDevice::Initialize` now takes a platform-neutral
  `RHI::DeviceCreateDesc`, `ExtrinsicRHI` no longer links
  `ExtrinsicPlatform`, and the strict layer check runs unguarded in
  `pr-fast` / `ci-linux-clang`.
- [RORG-036 — Layer ownership audit for misplaced concepts](../../done/RORG-036-layer-ownership-audit.md)
  (done 2026-06-06): whole-tree audit of promoted module placement against the
  `/AGENTS.md` §2 table. Outcome was a **clean baseline** — `RORG-034`/`RORG-035`
  already moved the previously-misplaced frame-loop and extent/rect value types
  to `core`, and no remaining promoted module imports a higher/sibling layer for
  a value-type-only reason. Examined the named candidates
  (`Runtime.StreamingExecutor`, the geometry/procedural packers, the
  dependency-free `Runtime.RenderWorldPool`) and decided each stays; zero moves
  accepted, so no follow-up move/split tasks were filed. `RenderWorldPool` is
  recorded in the task as a latent `core`-split candidate to revisit on a second
  consumer.
- [HARDEN-069 — Rebind legacy layering allowlist entries to active retirement tasks](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  (done 2026-06-02): metadata-only rebinding of legacy allowlist rows from the retired `HARDEN-010`
  ID to current legacy-retirement task IDs, preserving the allowlisted edge set.
- [HARDEN-070 — Drop dead null guards on reference-initialised helpers](../../done/HARDEN-070-drop-dead-null-guards-on-reference-initialised-helpers.md)
  (done 2026-06-06): hygiene cleanup of the nine internal-boundary
  `m_X == nullptr` guards in `SpatialDebugAdapters` (4),
  `TransientDebugUploadHelper` (3), and `VisualizationOverlayUploadHelper` (2)
  whose constructor-reference precondition already makes the null branch
  unreachable; replaced with one-line lifetime-contract notes in each `.cppm`.
  Filed from
  [`docs/reports/2026-05-26-agent-output-audit.md`](../../../docs/reports/2026-05-26-agent-output-audit.md)
  Row 5.
- [HARDEN-074 — Make markdown link checking see inline-code labels](../../done/HARDEN-074-doc-link-checker-inline-code-labels.md)
  (done 2026-06-02): fixed the doc-link checker blind spot where links such as
  ``[`TASK-ID`](...)`` can be skipped before validation, then cleans up the
  stale links exposed by the stricter parser.
- [HARDEN-075 — Validate task-state links and stale status claims](../../done/HARDEN-075-task-state-link-consistency-checker.md)
  (done 2026-06-02): added deterministic task lifecycle cross-reference checking so docs cannot
  keep claiming a task is active/backlog/done when the file lives elsewhere or
  no longer exists.
- [HARDEN-076 — Enforce open task owners for layering allowlist rows](../../done/HARDEN-076-enforce-open-task-layering-allowlist-owners.md)
  (done 2026-06-02):
  follows [`HARDEN-069`](../../done/HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  by making `check_layering_allowlist_quality.py` fail strict mode when a
  temporary allowlist exception points at a missing or retired task owner.
- [HARDEN-077 — Enforce operational follow-ups for ambiguous maturity closures](../../done/HARDEN-077-enforce-operational-followups-for-ambiguous-maturity.md)
  (done 2026-06-02):
  turns the review-only "CPUContracted is not Operational" rule into a focused
  task-policy check for backend-facing graphics/Vulkan/runtime slices.
- [DOCS-001 — Reduce `docs/architecture/graphics.md` to contract + status](../../done/DOCS-001-reduce-graphics-architecture-prose.md)
  (done 2026-05-17): shrank the 793-line `graphics.md` to 118 lines by
  extracting 15 embedded decision records into ADRs `0004..0018` and adding a
  Pointers section. Slice 1 (classification), slice 2 (15 per-ADR commits
  ADR-0004 through ADR-0018), slice 3 (no-op; the only migration-inventory
  row cross-linked to the existing parity matrix via ADR-0006), and slice 4
  (final tightening + Pointers) all landed.
- [ARCH-004 — Pin first legacy-deletion target and sequencing](../../done/ARCH-004-legacy-retirement-first-deletion-target.md)
  (done 2026-05-17): pinned `src/legacy/Interface/` as the first deletion
  target, recorded `src/legacy/Asset/` and `src/legacy/EditorUI/` as the
  second and third, and added the Sequencing table to
  [`docs/migration/legacy-retirement.md`](../../../docs/migration/legacy-retirement.md)
  so retirement is a tracked program rather than an indefinite "blocked" row.
  The program completed on 2026-07-01.
- [REVIEW-001 — Establish weekly human-led review of agent-authored slices](../../done/REVIEW-001-human-led-agent-week-review-cadence.md)
  (done 2026-05-17): landed `docs/agent/agent-output-review-checklist.md` with
  the nine-row failure-mode checklist, added the cadence pointer to
  `docs/agent/contract.md` and the reviewer rotation note to
  `docs/agent/roles.md`, and ran the first calibration audit on the
  GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091 window
  (`docs/reports/2026-05-17-agent-output-audit.md`, ≈ 15 minutes, eight rows
  pass, one self-corrected historical finding, no new follow-up filed).
- [REVIEW-002 — Recurring repo-state drift and inconsistency audit](../../done/REVIEW-002-recurring-drift-and-inconsistency-audit.md)
  (done 2026-06-06): installed the whole-tree drift audit
  ([`docs/agent/drift-audit-checklist.md`](../../../docs/agent/drift-audit-checklist.md))
  composing existing validators and semantic spot-checks for inventory drift,
  stale task links, allowlist owner drift, planned-marker drift, dead seams, and
  naming inconsistency, and ran the first calibration
  ([`docs/reports/2026-06-06-drift-audit.md`](../../../docs/reports/2026-06-06-drift-audit.md),
  ≈ 20 min). Eight rows clean; Row 7 (untracked TODO/temporary markers) filed
  the follow-up `HARDEN-078` (done 2026-06-10).
- [HARDEN-079 — Core module implementation splits](../../done/HARDEN-079-core-module-implementation-splits.md):
  module-interface hygiene follow-up for promoted `src/core/*.cppm` targets
  found by the 2026-06-06 implementation-body audit, including
  `Core.FrameGraph.cppm` and other core interfaces with non-trivial bodies that
  should live in matching `.cpp` implementation units.
- [LEGACY-003 — Delete `src/legacy/Apps/`](../../done/LEGACY-003-delete-src-legacy-apps.md)
  (done 2026-06-07): removed the legacy Sandbox leaf binary and its CMake
  wiring after `ExtrinsicSandbox` became canonical.
- [LEGACY-007 — Delete `src/legacy/EditorUI/`](../../done/LEGACY-007-delete-src-legacy-editorui.md)
  (done 2026-06-07): removed the legacy editor UI subtree after promoted
  `SandboxEditorUi` coverage replaced its remaining consumers.
- [RORG-031A — Architecture foundation backlog seed](../../done/RORG-031A-architecture-foundation.md)
  (done, 2026-06-10): the architecture backlog is fully structured and the
  layering/docs-sync/module-inventory governance tooling runs in CI; the
  seed retired once its tracked work existed as independent tasks.
- [HARDEN-078 — Track or resolve untracked TODO / temporary markers in promoted src](../../done/HARDEN-078-track-untracked-todo-temporary-markers.md)
  (done, 2026-06-10): resolved the `Core.Filesystem` dead-import TODO with an
  explicit per-watch callback policy and gave the `GetStreamingGraph()`
  temporary bridge its tracked removal owner `RUNTIME-105`.
- [HARDEN-082 — Rebind legacy allowlist umbrella rows to per-subtree owners](../../done/HARDEN-082-rebind-legacy-allowlist-umbrella-rows.md)
  (done, 2026-06-10): metadata-only rebind of the 54 `LEGACY-002` umbrella
  allowlist rows to `LEGACY-004..006`/`LEGACY-008..010`, unblocking the
  `LEGACY-002` seed retirement. Those rows are now gone after final legacy
  deletion.
- [LEGACY-002 — Seed retirement tasks for remaining `src/legacy/` subtrees](../../done/LEGACY-002-seed-src-legacy-retirement-backlog.md)
  (done, 2026-06-10): seeded `LEGACY-003..010` (landed 2026-06-06) and
  retired once `HARDEN-082` rebound the umbrella allowlist rows to the
  per-subtree owners.
