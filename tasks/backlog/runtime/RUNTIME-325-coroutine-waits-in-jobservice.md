---
id: RUNTIME-325
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive integration work with a delete-on-failure gate; evidence is the diff, CPU contract tests, the METHOD-056 Vulkan smoke, review and CI.
contract_schema: 1
contracts: [runtime.kernel-interface-locality, repo.task-contract-discovery, repo.source-documentation]
contract_review: Reviewed the catalog. A coroutine job kind changes the JobService job interface (kernel interface locality) and introduces a reusable runtime waiting contract (task contract discovery); module interfaces and READMEs change (source documentation). The CPD consumer keeps its existing RuntimeModule, config, agent and UI bindings, so method.engine-integration is not re-opened; property coherence is unchanged because publication stays on the existing CPD apply path.
---
# RUNTIME-325 — Coroutine waits in JobService, or delete the coroutine path

## Goal
- Give the existing coroutine waiting path in `Core.Tasks` a runtime home:
  `Tasks::Job` (coroutine type), `WaitFor(CounterEvent&)` / `WaitCounterAwaiter`
  and the sharded `WaitToken` park/unpark registry (`WaitShardCount = 16`,
  `Core.Tasks.Internal.cppm`). Today only `tests/unit/core/Test.CoreTasks.cpp`
  and `tests/benchmark/slo/Test.ArchitectureSLO.cpp` (`co_await` in
  `TaskSchedulerLocalStealAndWakeCompletionBudgets`) use it.
- Integrate it into the `Runtime.JobService` model and prove it with one real
  consumer: an iterative GPU↔CPU method that today blocks a worker or needs a
  hand-written state machine.
- Origin: REVIEW-007 C02 (2026-10-06). The operator deferred deletion and asked
  for integration with a decision gate.

## Acceptance criteria
- [ ] Decision gate first: pick the consumer and confirm it with the operator.
      If no consumer adopts the coroutine path, delete `Tasks::Job`, the
      `Job&&` `Dispatch`/`Reschedule` overloads, `WaitCounterAwaiter`/`WaitFor`, `YieldAwaiter`,
      the `WaitToken` park/unpark API and their tests. Rewrite the SLO wake
      measurement without `co_await`, then close this task. Keep
      `CounterEvent` and the worker park/unpark (REVIEW-007 refuted those).
- [ ] A coroutine job runs under JobService ownership with the same
      `JobDesc` semantics: `Scope` world, `CancellationGeneration`,
      `CancelAllForWorld`/`CancelAll`, `ValidateBeforeApply` and
      `IsReadyToApply`, and main-thread publication in the completion drain.
      A suspended coroutine never mutates world state; results publish only
      after main-thread revalidation (`JobApplyValidation::Current`).
- [ ] Cancellation and teardown: cancelling or `CancelAndDrain` resumes or
      destroys a suspended frame without a use-after-free (`Job::m_Alive`).
      World removal drops its suspended jobs. Shutdown leaves no parked
      `WaitToken` and no leaked frame.
- [ ] GPU interaction: a coroutine waits on a `CounterEvent` signalled by
      readbacks from `SpatialIndexCache::QueueGpuCompute` or a GpuQueue
      participant, instead of blocking a worker. Recording stays on the
      device-owner thread. Unregistering a participant while a coroutine waits
      on it cancels the job cleanly.
- [ ] First consumer: the METHOD-056 CPD device E-step broker
      (`Runtime.CoherentPointDriftGpuEStep`) or another iterative GPU↔CPU method
      the operator picks. The broker currently blocks the solver worker on a
      `condition_variable::wait_for` with `Timeout` until the main-thread `Pump`
      hands back the readback. It must keep CPU fallback on refusal, failure or
      timeout and keep its stats.
- [ ] CPU contract tests cover dispatch, suspend/resume, cancellation while
      suspended, stale-world discard and teardown. The METHOD-056 Vulkan smoke
      still matches the CPU.
- [ ] `src/core/README.md`, `src/runtime/README.md` and
      `docs/architecture/runtime.md` describe the result (integration or
      removal).

- [ ] **Operator rule (2026-10-06), applies to every coroutine adoption, here and
      in later tasks:** coroutines are harder to debug and maintain, so they are
      never rushed. Each commit adopts the coroutine path in **at most one method**.
      Before the next method is adopted, that commit is verified thoroughly:
      focused CPU contract tests, the method's real end-to-end workflow (Sandbox UI
      and agent/MCP lane, Vulkan where GPU is involved), its interaction with every
      other method already on the coroutine path (concurrent runs, cancellation,
      world switch, teardown), and engine-wide regression of the affected suites.
      The verification of each commit is recorded in its message or the task log.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests IntrinsicRuntimeContractTests IntrinsicBenchmarkTests
ctest --test-dir build/ci --output-on-failure --timeout 120 --no-tests=error -R '^(CoreTasks|RuntimeJobService|CoherentPointDriftOperations|CpdBrokerWorkspaceReuse|ArchitectureSLO|KernelCompilationLocality)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 180 --no-tests=error -L gpu -L vulkan -R '^METHOD056VulkanCpdEStep\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
```
New coroutine-job cases are to be added to `RuntimeJobService` in
`tests/contract/runtime/Test.RuntimeJobService.cpp` (target
`IntrinsicRuntimeContractTests`).

## Context
- `Scheduler::Dispatch(Job&&)` and `Reschedule` live in `src/core/Core.Tasks.cppm`;
  `WaitCounterAwaiter::await_suspend` calls `ParkCurrentFiberIfNotReady` on
  `CounterEvent::Token()` (`Core.Tasks.CounterEvent.cppm`).
- JobService (`src/runtime/Kernel/Runtime.JobService.cppm`) already provides
  world scope, generations, gates (`AwaitingGate`/`AwaitingApply`), GpuQueue
  participants and bounded main-thread apply. Reuse these; do not add a second
  scheduler (RUNTIME-194 retired the duplicates).
- Related: RUNTIME-311 (two-phase GPU Run/Accept transaction lifecycle).
  Coordinate, do not absorb.
- Out of scope: converting existing lambda jobs, TaskGraph/FrameGraph
  `WaitFor(label)` (unrelated name), performance claims beyond the SLO test.
