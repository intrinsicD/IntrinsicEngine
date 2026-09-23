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
observe `Queued` at its post-submit snapshot every time, so its four snapshots
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
- Reuse:
  - The file's existing `DirectMeshPostProcessWorkerBarrier` is a JobService job, and its completion would be drained by the test's single `DrainCompletions`.
  - `JobServiceTestHooks` only offers post-completion hooks.
  - So the test parks both workers with plain scheduler tasks, using the same mutex/condition-variable idiom. There is no sleep, production change or shared helper.
- Fix details:
  - The fence outlives the harness.
  - Workers are released right after the post-submit snapshot, before any assertion.
  - A park timeout also releases before failing.
  - The test now also asserts `Queued` at that snapshot.
  - All four snapshots, existing assertions, `EXPECT_GE(misses, 4u)`, the fixture and job parameters are unchanged.
- Separate from BUG-218; that run stopped before the curvature chunk.

## Acceptance criteria
- [ ] Before the fix, the gdb non-stop pause of the main thread at the post-submit snapshot (recipe in `/tmp/intrinsic-simplify-ui-cache-diagnosis.md`) reproduces 3 misses.
- [ ] After the fix, the same pause, now at line 5930, still passes, and 200 repetitions of the single case pass.
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
