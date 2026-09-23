---
id: BUG-219
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive test-synchronization fix; retained CI failure, source diagnosis and the pending reproduction/verification are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this fixes one contract test's worker synchronization, with no production cache, job, engine or reusable contract change.
---
# BUG-219 — Fix the UV panel cache test's post-submit snapshot race

## Goal
Make `SandboxEditorUi.UvRegenerationPanelModelTracksDerivedJobStateThroughCache`
observe a pre-completion job state (`Queued` or `Running`, never
`AwaitingGate`) at its post-submit snapshot every time, so its four snapshots
remain four distinct selected-analysis cache misses.

## Context
- Hosted pr-fast
  [run 35851119431](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35851119431)
  at `4cdf626f0` failed `SelectedAnalysisCacheMisses >= 4` (actual 3). The test
  predates this branch; see the [retained evidence](../evidence/BUG-219/failure-evidence.txt).
- Mechanism:
  - `JobService::DispatchJob` stores `Queued` and dispatches to the harness's two workers immediately.
  - `DerivedJobStateSignatureForEntity` mixes each job's `State` and progress into the cache key.
  - If a worker finishes the tiny UV job before the post-submit snapshot, that snapshot already sees `AwaitingGate`.
  - The post-`WaitForAll` snapshot then has an identical key and is a correct cache hit.
  - The production cache is correct; the test does not pin the job state.
- Reproduction (root, same binary): with a breakpoint at the first post-submit
  snapshot, root called the actual `Scheduler::WaitForAll()` from gdb and
  continued. This reproduced exactly 3 vs 4 misses while every state/content
  assertion passed. 300 normal CTest repeats passed, so only a forced schedule
  exposes the race.
- Fix, using the existing `context.JobCommands.Submit` test seam:
  - `std::mutex workGate` is declared before the harness.
  - A `std::unique_lock` is taken after it, so the gate is released before
    harness teardown on any return or exception.
  - The wrapper moves `desc.Work` into a `mutable` lambda. That lambda briefly
    acquires and releases the gate, then calls the original `Work`; the gate
    is never held during `Work`.
  - JobService stores `Running` before calling `Work`, and calls it with no
    JobService lock held. The gated snapshot therefore sees `Queued` or
    `Running` and cannot deadlock.
  - The lock is released right after that snapshot. The test keeps the
    `IsActive` assertion and adds `EXPECT_NE(state, AwaitingGate)`.
  - The four snapshots, `EXPECT_GE(misses, 4u)`, the fixture and job
    parameters are unchanged. There are no dummy jobs, worker parking, sleeps,
    timeouts or production changes.
- The first candidate, `183d50c72`, parked both workers with blocking scheduler
  tasks. Review found it larger than needed and not exception-safe; it remains
  in history and is replaced by the gate.
- Separate from BUG-218; that run stopped before the curvature chunk.

## Acceptance criteria
- [x] Before the fix, a forced schedule on the unchanged binary reproduces the failure: gdb at the post-submit snapshot calls `Scheduler::WaitForAll()`, and the test fails with 3 vs 4 misses.
- [ ] After the fix, forcing `WaitForAll()` right after the gate is released (line 5911) passes with all assertions and 4 misses, and normal repeats of the single case pass.
- [ ] The changed runtime contract test target builds; the full CPU, ASan and UBSan gates pass.
- [ ] Normal hosted pr-fast passes for PR #1045.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.UvRegenerationPanelModelTracksDerivedJobStateThroughCache$'
<binary> --gtest_filter=SandboxEditorUi.UvRegenerationPanelModelTracksDerivedJobStateThroughCache --gtest_repeat=200
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```
