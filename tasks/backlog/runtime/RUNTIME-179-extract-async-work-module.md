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

## Required changes
- [ ] Introduce one concrete `AsyncWorkModule` by folding the existing
      `AsyncWorkService` state and behavior into the runtime-module owner.
- [ ] Prove the streaming and derived-job responsibilities are cohesive on all
      four ADR-0026 axes above; split the implementation/task if the live
      inventory disproves that hypothesis.
- [ ] Register the existing streaming/derived-job maintenance work at the
      named Maintenance frame phase; preserve main-thread apply ordering.
- [ ] Publish the existing narrow streaming and derived-job capabilities
      through `ServiceRegistry` without adding a forwarding facade.
- [ ] Move initialization, shutdown-and-drain, and reset ordering from
      `Engine` into the module lifecycle.
- [ ] Keep every job snapshot-only and world-qualified. On world retirement,
      cancel scoped work and reject already-finished stale results at the
      main-thread commit gate without borrowing a live registry/world.
- [ ] Remove `Engine::SubmitDerivedJob`, `Engine::CancelDerivedJob`, and
      `Engine::GetDerivedJobQueueSnapshot` and migrate production/tests to the
      module service.
- [ ] Remove `Extrinsic.Runtime.AsyncWorkService` from the Engine interface and
      private state; do not leave a compatibility re-export.

## Tests
- [ ] Preserve streaming cancellation, main-thread apply, derived-job queue,
      shutdown drain, and reinitialize coverage through the module surface.
- [ ] Add world-retirement coverage proving queued/running/already-finished
      scoped work cannot commit after retirement and no worker retains or
      borrows live world state.
- [ ] Add an integration test proving the app-composed module executes its
      maintenance path during `Engine::Run()`.
- [ ] Run focused runtime async/job coverage, strict layering, and the complete
      default CPU-supported gate.

## Docs
- [ ] Update runtime module/lifecycle documentation and the kernel target-state
      row with global state scope and the new owner.
- [ ] Regenerate the module inventory after the public surface changes.

## Acceptance criteria
- [ ] `Engine` owns no streaming executor, derived-job registry, or async-work
      domain facade.
- [ ] One concrete module owns lifecycle, maintenance, diagnostics, and
      shutdown; no module-to-service forwarding layer is added.
- [ ] The cohesion inventory covers all ADR-0026 axes, and world-retired work
      is cancelled or dropped whole before commit.
- [ ] Existing async behavior is exercised through the canonical app runtime
      path and remains deterministic.
- [ ] The only intentional behavior change is fail-closed world-retirement
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

## Forbidden changes
- Adding another executor, scheduler, registry, or wrapper service.
- Moving asset-import, scene-document, or editor policy into this module.
- Changing async semantics while moving ownership.

## Maturity
- Target: `Operational`; the module must run through the canonical
  `Engine::Run()` maintenance phase, not only in a direct unit test.
