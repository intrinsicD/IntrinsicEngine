---
id: ARCH-009
theme: F
depends_on:
  - ARCH-008
---
# ARCH-009 — Kernel JobService: snapshot-in/result-out background jobs

## Goal
- Give the runtime kernel a `JobService` for multi-frame background work on
  the shared worker pool: `Submit(JobDesc) -> JobToken`, cooperative
  cancellation scoped to a `WorldHandle`, completion observed via kernel
  events published from workers, and a reap step in the Maintenance phase,
  per [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md) D8.

## Non-goals
- No `GpuQueue` execution target in this task — the GPU path rides the
  GPU-job-participant frame contract and `RUNTIME-137`; this task ships
  `CpuPool` and reserves the target enum.
- No migration of `Runtime.DerivedJobGraph`, `Runtime.StreamingExecutor`,
  `Runtime.KMeansGpuJobQueue`, or `Runtime.AsyncBufferReadback` onto the
  service; `ARCH-012` and later extractions own consumer moves.
- No in-place mutation escape hatch: jobs never receive live-world references
  (iron rule, ADR-0024 D8). Rejected alternative (locking/checkout) stays
  rejected.
- No new scheduler machinery — reuse `Extrinsic.Core.Tasks`; scheduler
  hardening stays owned by `CORE-005`/`CORE-007`.

## Context
- Owner/layer: `runtime` (kernel spine per ADR-0024 D9); executes on the
  existing core task scheduler so the FrameGraph and jobs share one pool.
- Completion channel: the job's work function publishes its own domain event
  through the `ARCH-008` bus (thread-safe publish → next main-thread pump);
  module completion handlers commit results to the live world at pump B.
  The service itself only tracks token lifecycle.
- Cancellation contract: cancelling marks the token; the work function polls
  a `JobCancellation` view; a cancelled job's completion is dropped at the
  token layer so results are never half-applied. `ARCH-010` wires
  world-teardown cancellation onto this.
- Depends on `ARCH-008` for the worker→main-thread completion channel.

## Required changes
- [ ] New `Extrinsic.Runtime.JobService` module (interface `.cppm` +
      implementation `.cpp`): `JobDesc { DebugName, Target, Scope(WorldHandle),
      Work(const JobCancellation&) }`, `Submit`, `Cancel`, `IsComplete`,
      `CancelAllForWorld(WorldHandle)`.
- [ ] `JobToken` handle type usable by `ARCH-007`'s future `Pending` outcome
      and by parked `CommandSequence` links (declaration only; no sequence
      machinery).
- [ ] Reap step (`ReapCompleted`) wired into the Maintenance phase of
      `Engine::RunFrame()`.
- [ ] Diagnostics: in-flight/completed/cancelled counters.

## Tests
- [ ] Unit/contract tests (headless, `unit;runtime` labels): submit → work
      runs on a pool thread → completion event delivered at the next pump →
      commit handler runs on the main thread.
- [ ] Cancellation test: cancel before start (work never runs) and cancel
      mid-flight (cooperative poll observes it; completion dropped).
- [ ] World-scope test: `CancelAllForWorld` cancels only that world's jobs.
- [ ] Sanitizer run covers the submit/complete/cancel races.

## Docs
- [ ] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [ ] Record the FrameGraph-vs-JobService two-tier rule in the runtime
      architecture doc, citing ADR-0024 D8.

## Acceptance criteria
- [ ] Jobs receive no live-world references anywhere in the API surface.
- [ ] Completion commits demonstrably run main-thread at a pump point.
- [ ] All listed tests pass under the default CPU gate and sanitizers.
- [ ] `Operational` follow-up is owned by `ARCH-012`; the `GpuQueue` target
      is deferred to the `RUNTIME-137` follow-up line.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Any API accepting `ECS::SceneRegistry&` (or component references) into a
  job's work function.
- Blocking waits on job completion from the main loop.
- Creating a second worker pool.

## Maturity
- Target: `CPUContracted`. `Operational` owned by `ARCH-012`.
