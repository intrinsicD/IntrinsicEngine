---
id: BUG-240
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive diagnosis; evidence is the CI run logs and the diff
contract_schema: 1
contracts: []
contract_review: Sanitizer test lane failures; no catalog contract covers CI test budgets.
---
# BUG-240 — ASan and UBSan CPU jobs have been red since 2026-09-05

## Goal
- `ci-linux-clang` `sanitizer-tests` (asan, ubsan) pass on `main`, each failure diagnosed rather
  than skipped.

## Context
- Last successful ASan job in the scanned non-push runs: 2026-09-05. Sanitizer jobs run only on
  pull requests and manual dispatch, so the red state stayed unseen (see BUG-238/239).
- Manual dispatch 37836748224 on `acb01096e` (2026-10-08): `full-cpu` success; selection capture
  succeeded in both sanitizer jobs (BUG-239's v2 format); "Run selected CPU tests" failed.
  - ASan, 7 of 4003: timeouts in `PropertySmoothingWorkflows.VariationalFitVariantsAndSolverSwitches`,
    `CoherentPointDriftOperations.LargeDeformingRunsUseTheAutomaticLowRankUnlessExactIsAsked`,
    `SandboxAgentServer.KMeansProgressFollowsTheRunsOwnJob`,
    `SandboxAgentServer.SceneSaveProgressNamesTheSceneJob`, `IntrinsicGeometryTests.Grouped`,
    `IntrinsicGeometryScalarfieldExtremaTests.Grouped`; one assertion failure,
    `SandboxProcessingPanels.ProgressivePoissonManualAndDebouncedRunsShareConfigApply`
    (`Test.SandboxProcessingPanels.cpp:1809` ff.: a rejected auto-run submitted a second job,
    `SubmittedJobs` 2 vs 1, no rejection counted; `RadiusAlpha` 0.41 vs `double(float(.41))`).
  - UBSan, 1 of 4003: `IntrinsicGeometryScalarfieldExtremaTests.Grouped` timeout.
- The assertion failure passes in `full-cpu`; it may be timing-dependent (one scheduler worker,
  debounce) and needs a reproduction before it is classed as a flake.

## Acceptance criteria
- [ ] The ProgressivePoisson panel failure is reproduced (ASan build) and fixed at its root cause.
- [ ] Each timeout is either brought within budget or given a diagnosed, documented sanitizer
      budget/lane; no gate weakened without diagnosis.
- [ ] A manual `ci-linux-clang` dispatch shows both sanitizer jobs and `cpu-test-selection-parity`
      green, or every remaining failure has its own task.

## Verification
```bash
gh workflow run ci-linux-clang.yml --ref main
cmake --preset ci-asan && cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -R 'ProgressivePoissonManualAndDebouncedRunsShareConfigApply'
```
