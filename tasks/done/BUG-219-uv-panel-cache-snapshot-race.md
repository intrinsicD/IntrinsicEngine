---
id: BUG-219
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive test-synchronization fix; retained CI failure, source diagnosis, forced-schedule reproduction and local/hosted verification are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this fixes one contract test's worker synchronization, with no production cache, job, engine or reusable contract change.
---
# BUG-219 — Fix the UV panel cache test's post-submit snapshot race

## Completion — 2026-09-23
Resolved.
- The UV job's `Work` is gated through the existing `JobCommands.Submit` test seam until the post-submit snapshot has been taken. That snapshot therefore always sees a pre-completion state, and all four snapshots remain distinct cache misses.
- Production code is unchanged. The fix adds no sleeps and no dummy jobs.
- Before the fix, a forced schedule reproduced the hosted 3 vs 4 failure while 300 normal repeats passed.
- After the fix, a forced debugger pause, 300 repeats, the full CPU/ASan/UBSan gates and hosted pr-fast all pass.

PR/commit: [PR #1045](https://github.com/intrinsicD/IntrinsicEngine/pull/1045); fix `621566843`
(the first candidate `183d50c72` is superseded); the enclosing retirement commit is docs only.

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
- [x] After the fix, a forced schedule passes and normal repeats of the single case pass.
  - gdb non-stop paused the main thread for 1 s at the post-submit snapshot (line 5909) while the workers ran. This was a deliberate debugger probe, not a sleep in the test.
  - All original assertions plus the non-`AwaitingGate` check passed, and the process exited normally.
  - 300 normal CTest repeats passed in 7.22 s.
- [x] The changed runtime contract test target builds in all four presets:
  - full CPU: 5,013 tests, 0 failures, 1 expected LSan skip, 74.66 s
  - full ASan: 3,333, 0 failures, 0 skips, 727.84 s
  - full UBSan: 3,333, 0 failures, 1 expected skip, 349.02 s
  - The changed executable has no GPU/Vulkan registrations.
- [x] Normal hosted pr-fast passes for PR #1045: [run 35853323917](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35853323917) succeeded in 9m38s, with the case at 0.03 s. Docs-validation run 35853324007 also passed.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.UvRegenerationPanelModelTracksDerivedJobStateThroughCache$'
<binary> --gtest_filter=SandboxEditorUi.UvRegenerationPanelModelTracksDerivedJobStateThroughCache --gtest_repeat=200
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```
