---
id: BUG-231
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Intermittent test hang under CPU oversubscription; no method or performance claim.
contract_schema: 1
contracts: []
contract_review: Test-harness or frame-loop liveness defect; no new engine contract until diagnosed.
---
# BUG-231 — ICP progress-widget cancel test hangs under CPU load

## Goal
Find why `SandboxProcessingPanels.IcpProgressWidgetCancelStopsARunThatNeverConverges`
occasionally never finishes under heavy CPU load, and fix the cause (engine or test)
instead of raising its timeout.

## Context
- Seen 2026-10-02 during RUNTIME-279 verification: one `***Timeout 30.02 sec` in the full CPU
  suite run with `-j$(nproc)`; passes in isolation (~0.3 s).
- Reproduced on unchanged base `be165a294` (before RUNTIME-279): 1 of 30 runs exceeded 90 s with
  every core busy (3 parallel lanes, one busy-loop process per core). On the RUNTIME-279 tree:
  1 of 9 runs hung the same way.
- While hung, the main thread sits at ~96 % CPU and both scheduler workers wait on a futex for
  over 30 minutes, so the test's 3000-frame bound never fires: one frame never returns, or the
  driver hook stops running. A main-thread wait that helps run tasks
  (`Core.Dag.TaskGraph` with `Scheduler::TryRunOne` / `WaitForWorkProgress`) is the first
  suspect; ptrace is restricted on the host, so no stack was captured.
- Diagnosis 2026-10-02 (stack captured by running the test binary under gdb with 16 CPU hogs; hang on
  run 157 of the loop, earlier run 31 by wall clock). Main thread, SIGINT at 45 s:
  `AlignICP` <- `AlignPointClouds` <- `RunRegistrationCpuWorker` <- `MakeRegistrationCpuJobDesc` lambda <-
  `JobService::DispatchJob` <- `LocalTask` <- `Scheduler::TryRunOne` <- `TaskGraphCompletion::Wait`
  (Core.Dag.TaskGraph.cpp:1031). The lone scheduler worker is parked on its futex and never ran the job.
  Cause: a frame-graph `Wait()` on the main thread help-runs any queued scheduler task. When the worker is
  CPU-starved, the main thread pops the queued 100000-iteration ICP job and runs it inline inside the
  frame. Cancel is only activated from the frame's `OnFrame` hook on that same thread, so the frame never
  returns and the 3000-frame bound never fires. Product bug (any long editor job can be stolen into a frame
  and freeze the UI for its duration); the test is correct, and no probes were left in the tree.
  Proposed fix: JobService jobs must not be runnable by external help. Either dispatch them through a
  worker-only lane/flag that `TryRunOne` skips for non-worker callers, or have `TaskGraphCompletion::Wait`
  help only tasks owned by its own graph. Regression test (unit;core): block the one worker with a gate task,
  dispatch a worker-only spinner that records its thread id, `TaskGraph::Execute()` a trivial pass on the
  main thread; assert Execute returns and the spinner did not run on the main thread (a watchdog thread
  cancels the spinner after 2 s so the pre-fix failure is an assertion, not a hang).

## Acceptance criteria
- [ ] A stack (or a deterministic repro) shows where the hung frame spins.
- [ ] The cause is fixed; the test passes 100 runs under the same oversubscription.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels.IcpProgressWidgetCancel' --repeat until-fail:20 --timeout 120
```

## Forbidden changes
- Raising the test timeout, or quarantining it, without a diagnosis.
