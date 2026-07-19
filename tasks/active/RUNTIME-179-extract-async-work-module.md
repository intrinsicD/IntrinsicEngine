---
id: RUNTIME-179
theme: F
depends_on:
  - ARCH-016
maturity_target: Operational
---
# RUNTIME-179 — Extract the async-work composition module

## Goal
- Move global streaming-executor and derived-job ownership out of
  `Runtime.Engine` into one app-composed `AsyncWorkModule` that participates
  in the existing runtime-module lifecycle and maintenance phase.

## Non-goals
- No second worker pool, job system, executor, queue, or scheduling framework.
- No unrelated streaming, scheduling, apply-order, or derived-job semantic
  change. This task does own the ADR-0027-required world-qualified
  cancellation and stale-commit hardening where the live inventory lacks it.
- No migration of `JobService`; it remains kernel substrate.
- No wrapper around an unchanged `AsyncWorkService`.

## Context
- Owner/layer: `runtime`; the state is global across worlds.
- `AsyncWorkService` currently owns the persistent streaming executor,
  derived-job registry, maintenance drains, shutdown-and-drain, and reset
  behavior, but `Engine` owns its lifecycle and three domain facades.
- The right-sized shape is one concrete behavior owner: fold or rename the
  existing service into `AsyncWorkModule` rather than retaining a service plus
  a pass-through module.
- The existing scene-document and asset-import workflows are demonstrated
  consumers and become `SceneEditingModule`/`AssetWorkflowModule` in the
  downstream tasks. Before folding the current service, inventory ADR-0026's
  four axes: lifecycle, state scope, dependency/cancellation/commit ownership,
  and published-state/consumer reactions. Divergence on any axis requires a
  split; another job kind or internal execution mechanism alone does not.

## Status
- Implementation complete; owner: Codex team; branch:
  `codex/runtime-179-async-work-module`; activated 2026-07-18. Canonical
  aggregate CPU verification completed before integration/retirement.
- Four-axis cohesion and live-caller inventory completed 2026-07-18; the
  implementation gate is open.

## Four-axis cohesion and live-caller inventory

### 1. App composition and lifecycle

- `AsyncWorkService` is currently constructed with `Engine`, initialized once
  per `Engine::Initialize()`, driven once per rendered frame in Maintenance,
  shut down and drained before scene/assets teardown, reset before FrameGraph
  teardown, and initialized again on engine reuse. The `StreamingExecutor` and
  `DerivedJobRegistry` never have independent enable/disable, boot, frame, or
  shutdown paths.
- Sandbox is the production composition point for optional runtime modules.
  It currently composes `ClusteringModule`; it must compose
  `AsyncWorkModule` beside it. Contract/integration applications that exercise
  streaming or derived work must likewise compose the module explicitly.
- The current Engine boot order initializes async work before it wires
  `AssetImportPipeline` and `SceneDocument`, but invokes module registration
  only afterward. The behavior-preserving extraction therefore moves the
  generic module registration phase earlier: after the scheduler, renderer,
  FrameGraph, and boot world exist, but before consumers borrow published
  async capabilities. Engine then discovers `StreamingExecutor` and
  `DerivedJobRegistry` through `ServiceRegistry`; it does not name or
  special-case `AsyncWorkModule`. The existing all-providers-collected
  resolution/finalization phase remains after dependent subsystem wiring and
  before application initialization, preserving Clustering and two-phase boot
  behavior.

### 2. Durable state and scope

- The executor, derived registry, queues, diagnostics, and maintenance budget
  are one global owner spanning all live worlds. Individual tasks are the
  scoped records: raw streaming and derived descriptors currently lack
  `WorldHandle`, so queued/running/ready-to-apply work cannot be retired by
  world identity.
- Live worker callbacks already receive copied/shared snapshots rather than a
  registry from the executor. World/asset/ECS references appear only in
  main-thread apply/validation callbacks; GPU readback workers borrow
  renderer-owned transfer/command resources, not a live ECS world. The
  extraction adds a valid world scope to every descriptor and retains only
  that value in executor/registry records. No retirement check runs on a
  worker or borrows `WorldRegistry`.
- `WorldWillBeDestroyed` is published during deferred world Maintenance and
  delivered at the next kernel event pump, before the next async Maintenance
  commit. `AsyncWorkModule` subscribes once per boot, retires that exact
  generation-qualified handle, cancels its pending/running/readback/apply
  records, rejects later submissions for the retired handle, and rechecks the
  retired set immediately before main-thread apply. Reinitialization creates
  a fresh executor/registry and therefore a fresh retirement set.
- Active-world `AssetImportPipeline` and `SceneDocument` work freezes the
  submission `{WorldHandle, Scene::Registry*}` pair plus its binding epoch.
  Main-thread apply validates that exact target against both `WorldRegistry`
  and the active member binding; the epoch also detects an away-and-back
  switch whose final handle and address match. Switching worlds without
  retiring the old world fails closed rather than redirecting decoded results
  into the newly active scene.

### 3. Dependency, cancellation, and commit ownership

- `DerivedJobRegistry` is constructed over the one persistent
  `StreamingExecutor`; it translates derived dependencies/readbacks into
  streaming records and delegates launch, completion drain, and bounded apply
  to that executor. Both paths share cancellation generations, shutdown join,
  Maintenance cadence, and the same main-thread apply gate. A separate module
  would duplicate or split one authoritative cancellation/commit boundary.
- Cancellation is a terminal derived-job decision: late worker failure/success
  and main-thread apply bookkeeping preserve `Cancelled` instead of overwriting
  it with `Failed` or `Complete`.
- The existing core Maintenance contract is ordered
  transfer collect → async drain/apply → asset tick → async pump. A single
  generic post-contract frame hook would silently move apply after asset tick.
  `AsyncWorkModule` therefore directly implements and publishes the existing
  domain-free `Core::IStreamingFrameHooks` seam; Engine borrows that optional
  seam through `ServiceRegistry` and passes it to the unchanged Maintenance
  contract. This is the module's registered Maintenance contribution, not a
  forwarding object or duplicate/no-op schedule hook, and it preserves the
  exact apply and pump order.
- Raw streaming callers are `AssetImportPipeline`, `SceneDocument`, Sandbox
  default direct-mesh post-processing, and visualization HTEX recreation.
  Derived callers are the selected-mesh texture bake, Sandbox method/editor
  facades, model-scene progressive enrichment, and GPU readback helper. The
  Engine currently wires the raw executor into asset/document dependencies and
  forwards three derived-job facade methods; those facades have one production
  consumer (`SandboxEditorFacades`) plus contract tests.
- The module will publish the concrete `StreamingExecutor` and
  `DerivedJobRegistry` through `ServiceRegistry`, plus its own existing
  `Core::IStreamingFrameHooks` base for the kernel's existing Maintenance
  contract.
  Callers submit/cancel/read snapshots on the concrete capabilities directly;
  there is no retained forwarding service or Engine facade. The module alone
  performs drain/readback/apply, background pump, shutdown-and-drain, and
  reset.

### 4. Published state and consumer reactions

- Streaming publishes task state/diagnostics and optional main-thread apply
  payloads to asset, scene-document, and visualization consumers. Derived work
  publishes richer dependency/readback/apply status snapshots consumed by the
  Sandbox editor and invokes method-specific commit callbacks. These records
  intentionally retain different meanings and are not merged.
- There is nevertheless no independent standing reaction or lifecycle split:
  derived publication is an observable view over work scheduled and committed
  by the same executor lane, and both capabilities must quiesce atomically
  before their consumers tear down. The distinct public records justify two
  narrow `ServiceRegistry` entries, not two composition units.

### Cohesion verdict

All four axes are cohesive. Fold `AsyncWorkService` into one concrete
`AsyncWorkModule`; do not split the task and do not retain a wrapper. The only
intentional semantic hardening is fail-closed world-retirement cancellation
and stale-commit rejection.

## Required changes
- [x] Introduce one concrete `AsyncWorkModule` by folding the existing
      `AsyncWorkService` state and behavior into the runtime-module owner.
- [x] Prove the streaming and derived-job responsibilities are cohesive on all
      four ADR-0026 axes above; split the implementation/task if the live
      inventory disproves that hypothesis.
- [x] Register the existing streaming/derived-job maintenance work by directly
      providing `Core::IStreamingFrameHooks` to the canonical Maintenance
      contract; preserve main-thread apply ordering.
- [x] Publish the existing narrow streaming and derived-job capabilities
      through `ServiceRegistry` without adding a forwarding facade.
- [x] Move initialization, shutdown-and-drain, and reset ordering from
      `Engine` into the module lifecycle.
- [x] Keep every job snapshot-only and world-qualified. On world retirement,
      cancel scoped work and reject already-finished stale results at the
      main-thread commit gate without borrowing a live registry/world.
- [x] Remove `Engine::SubmitDerivedJob`, `Engine::CancelDerivedJob`, and
      `Engine::GetDerivedJobQueueSnapshot` and migrate production/tests to the
      module service.
- [x] Remove `Extrinsic.Runtime.AsyncWorkService` from the Engine interface and
      private state; do not leave a compatibility re-export.

## Tests
- [x] Preserve streaming cancellation, main-thread apply, derived-job queue,
      shutdown drain, and reinitialize coverage through the module surface.
- [x] Add world-retirement coverage proving queued/running/already-finished
      scoped work cannot commit after retirement and no worker retains or
      borrows live world state.
- [x] Add an integration test proving the app-composed module executes its
      maintenance path during `Engine::Run()`.
- [x] Run focused runtime async/job coverage, strict layering, and the complete
      default CPU-supported gate.

## Docs
- [x] Update runtime module/lifecycle documentation and the kernel target-state
      row with global state scope and the new owner.
- [x] Regenerate the module inventory after the public surface changes.

## Acceptance criteria
- [x] `Engine` owns no streaming executor, derived-job registry, or async-work
      domain facade.
- [x] One concrete module owns lifecycle, maintenance, diagnostics, and
      shutdown; no module-to-service forwarding layer is added.
- [x] The cohesion inventory covers all ADR-0026 axes, and world-retired work
      is cancelled or dropped whole before commit.
- [x] Existing async behavior is exercised through the canonical app runtime
      path and remains deterministic.
- [x] The only intentional behavior change is fail-closed world-retirement
      cancellation/stale-commit rejection required by the accepted state-scope
      contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AsyncWork|DerivedJob|Streaming|RuntimeModule' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

Evidence recorded 2026-07-18:

- `ci-fast` expanded runtime selection: 237/237 passed; targeted slow
  direct-mesh shutdown drain and Sandbox presentation seams: 2/2 passed.
- Fresh canonical `ci` configure and runtime contract/integration build:
  1571/1571 build steps completed.
- Canonical focused selector
  `AsyncWork|DerivedJob|Streaming|RuntimeModule`: 86/86 passed.
- Strict kernel convergence, layering, test layout, task policy, root hygiene,
  documentation links, diff whitespace, and gate-routing self-tests (19/19)
  passed. Kernel-convergence checker regressions also passed 19/19. The
  generated module inventory contains 388 modules.
- Independent source review reported no blocking findings. The canonical
  `IntrinsicTests` aggregate build completed 1562/1562, and the complete
  CPU-supported CTest selector passed 4121/4121 selected tests with one
  expected GLFW/LSan capability skip.
- The additional live `IntrinsicTests` gate-routing audit exposed a
  pre-existing baseline mismatch also present on `main`: four retired
  `CoreGraphInterfaces` case names remain in the baseline while the registry
  exposes their `TaskPlanGraph` replacements, and
  `RuntimeStreamingExecutor.RuntimeTaskKindTokensRemainStable` is absent.
  RUNTIME-179 keeps only its scoped routing additions; the coordinator is
  tracking the shared baseline repair separately before integration.

## Forbidden changes
- Adding another executor, scheduler, registry, or wrapper service.
- Moving asset-import, scene-document, or editor policy into this module.
- Changing async semantics beyond the explicit world-retirement cancellation
  and stale-commit hardening owned by this task.

## Maturity
- Target: `Operational`; the module must run through the canonical
  `Engine::Run()` maintenance phase, not only in a direct unit test.
