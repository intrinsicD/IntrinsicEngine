---
id: BUG-253
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, the repeated in-process runs, and CI
contract_schema: 1
contracts: []
contract_review: Test-only wait predicate fix in a runtime contract test; engine job semantics stay unchanged.
---
# BUG-253 — Editor job tools test queries a job it never waited for

## Goal
- `EditorKeypointAgent.EditorJobToolsListWaitAndCancelThroughTheSurface` waits for every job
  it later queries, so it no longer fails when one drain completes the editor run before the
  foreign job's result is applied.

## Context
- Hosted `ci-linux-clang` push run 40100277575 (`cca289328`), job `full-cpu` 120897945170,
  failed at `tests/contract/runtime/Test.SandboxEditorSessionLifecycle.cpp:3449` (null vs
  `"published"`) and `:3451` (`""` vs `"unknown_job"`).
- Cause: the `WaitFor` predicate after `release.store(true)` returns `waited && ran` and never
  waits for `foreign`. With one scheduler worker, a `JobService::DrainCompletions`
  (`src/runtime/Kernel/Runtime.JobService.cpp`) can apply the cancelled editor run while the
  foreign job's result is not yet in `CompletionQueue`; `jobs_wait` on `foreign` then returns
  a continuation with empty text instead of a `published` answer. Every sibling test in the
  file waits with `jobs.IsComplete(foreign)`.
- Reproduction (diagnosis agent, 2026-10-24): in-process `--gtest_repeat` about 16 failures in
  8000, always the CI signature; fresh-process `ctest` 0/500. The measured in-process rate
  varied between about 0.06% (5/8000, the fix's before run) and 0.2% (16/8000, this run).
- Test added in `750cb5855` (RUNTIME-279, 2026-10-02); no 2026-10-24 commit touched the test
  or `Runtime.JobService.cpp`.

## Acceptance criteria
- [x] The test waits for every job it later queries (`foreign` included). Review: "met (`:3430`)".
- [x] A repeated in-process run shows 0 failures at a repetition count where the old predicate
      failed. Review: "met (5/8000 before, 0/8000 twice after, plus my 0/3000)".
- [x] No engine change. Review: "met (the diff touches only the test and task files)".

## Resolution
- 2026-10-24: the post-release `WaitFor` predicate now returns
  `waited && ran && jobs.IsComplete(foreign)`, the file's sibling pattern. Test-only; no
  engine file changed.
- Prediction written before the run: the old predicate shows at least 1 failure in 8000
  in-process repetitions (expected about 16), all with the 3449/3451 signature.
- Before (old predicate, `IntrinsicRuntimeContractTests`, `--gtest_repeat=8000`): 5/8000
  failed, each at `:3449` and `:3451`. After: 0/8000 twice. `EditorKeypointAgent.*` 12/12;
  `ctest -R` over the file's seven suites 96/96; `check_test_layout --strict` clean.
- Not checked: the hosted `full-cpu` job on this commit. 0/16000 bounds the residual in-process
  failure rate below about 0.02% (95%, rule of three); the old rate was 5/8000 (~0.06%).

## Verification
```bash
cmake --build build/ci --target IntrinsicRuntimeContractTests --parallel 16
build/ci/bin/IntrinsicRuntimeContractTests \
  --gtest_filter=EditorKeypointAgent.EditorJobToolsListWaitAndCancelThroughTheSurface \
  --gtest_repeat=8000 --gtest_brief=1
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='EditorKeypointAgent.*'
ctest --test-dir build/ci -R 'EditorKeypointAgent' --output-on-failure
```

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `411ca6ecc` (filed) and `c2bd44899` (the fix) on the night
integration branch `claude/night-2026-10-24-bug-sweep`. Not yet on `main`.

Reviewer decisions: independent Claude review of `dde5776d6..c2bd44899`, 2026-10-24, relayed
verbatim by the coordinator; the record is not on disk. Verdict approve; all three boxes are
ticked from its decisions quoted above. It confirmed the root cause by reading:
`DrainCompletions` takes the `CompletionQueue` at `Runtime.JobService.cpp:639` and finalizers
run at `:786`; `JobsWait` returns a pending continuation for an incomplete job at
`Runtime.AgentOperations.Editor.cpp:1068-1086`, which is correct protocol, not an engine bug.
Its minor finding (state the varying measured rate) is applied in Context and the bug index.

Limits: the hosted `full-cpu` job has not run on the fix; the rule-of-three bound (about 0.02%
from 0/16000) leaves very rare failures possible. No temporary evidence to delete.
