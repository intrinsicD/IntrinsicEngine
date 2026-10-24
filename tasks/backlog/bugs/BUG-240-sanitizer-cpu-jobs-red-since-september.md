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
- 2026-10-18: the CPD case above is split into `...UseTheAutomaticLowRank` and
  `...KeepTheFullKernelWhenExactIsAsked` (one exact iteration) after it also timed out in
  `full-cpu`; locally 6.5 s became 0.7 s + 3.0 s. Its ASan timing is not re-measured.

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

## Resolution — 2026-10-24
Covers criterion 1 and the diagnosis for criterion 2. Raw logs are in
`tasks/evidence/BUG-240/` (git-ignored).

### ProgressivePoisson panel failure (criterion 1)
- **Root cause.** The panel's auto-run debounce compares `ImGui::GetTime() - LastEditTime`
  with `DebounceSeconds` (0.25 s). ImGui time is the sum of real frame deltas, clamped to 0.25 s
  per frame. The Grid-width edit is committed at step 6. The test assumes that the debounce
  has not yet expired at step 10, where it arms `reject` and jumps the clock by 1 s. Under ASan
  with one worker, frames slower than about 83 ms make three frames exceed 0.25 s. The auto-run
  then submits job 2 before step 10, and every later expectation cascades from it.
- **RadiusAlpha 0.41 vs `double(float(.41))`.** This is not a precision bug. It is a stale read
  that follows from the early job. Phase 3's condition (`Succeeded && SubmittedJobs >
  jobsBefore`) is already true one frame early. The fixture hook runs before the panel draw
  (RuntimeTestModule: "Fixtures tick before ordinary production UiBuild hooks"). So the check
  reads the applied .41 before the manual Run has widened it through the float widget.
  Phase 4 then counts 4 jobs where it expects 3.
- **Change (test only).** At the top of `OnFrame`, the line
  `ImGui::GetCurrentContext()->Time += 1. / 60. - ImGui::GetIO().DeltaTime;` gives the test
  a fixed 1/60 s UI clock. The explicit `Time += 1.` jumps stay. No product change: debouncing
  on real UI time is correct product behaviour. Chosen default: a fixed-step UI clock rather
  than a larger `DebounceSeconds`, which would depend on the 0.25 s clamp and on frame counts.
- **Regression and verification** (ci-asan, `IntrinsicSandboxEditorIntegrationTests`; the
  command is the CI ctest flags plus `-R 'ProgressivePoissonManualAndDebouncedRunsShareConfigApply'`):
  - Before the fix, under CPU contention (`taskset -c 7` plus 3 bounded busy loops on the same
    CPU): 3 runs out of 3 failed with the CI signature, i.e. the same six expectations with the
    same values (`SubmittedJobs` 2 vs 1 at the step-10 check; RadiusAlpha
    0.40999999999999998 vs 0.40999999642372131; 4 vs 3), in 13.4-15.0 s. Without contention it
    passes in 3.27 s.
  - Host-independent probe (temporary, not committed): `sleep_for(100ms)` per phase-2 frame.
    Without the fix it failed with the same six expectations. With the fix it passed (5.26 s).
  - With the fix: the plain run passed (3.38 s); 3 of 3 contention runs passed (13.9-14.1 s);
    `-R '^SandboxProcessingPanels\.'` passed 85 of 85 (228.7 s, serial).

### Timeouts (criterion 2): diagnosis, no budget changed
- **The workflow's `--timeout 60` has no effect.** CTest's `--timeout` applies only to tests
  without a `TIMEOUT` property. Every case discovered via `intrinsic_test` carries 30 s
  (`tests/CMakeLists.txt` `_intrinsic_default_test_timeout_seconds`), and every grouped wrapper
  carries 120 s. The CI kills happen at 30.0x s and 120.0x s. The sanitizer lanes therefore run
  with the unsanitized budgets.
- **Local timings, exact CI ctest command, serial,** host load average about 6 of 32 cores:

  | Case | Local ASan | CI ASan (2026-10-08) |
  |---|---|---|
  | `PropertySmoothingWorkflows.VariationalFitVariantsAndSolverSwitches` | 22.96 s | killed at 30.07 s |
  | CPD LowRank / CPD Exact (halves of the 2026-10-18 split) | 2.39 s / 9.84 s | only the unsplit case ran: killed at 30.02 s |
  | `SandboxAgentServer.KMeansProgressFollowsTheRunsOwnJob` | 20.45 s | killed at 30.08 s |
  | `SandboxAgentServer.SceneSaveProgressNamesTheSceneJob` | 15.93 s | killed at 30.09 s |
  | `IntrinsicGeometryTests.Grouped` | 98.59 s | killed at 120.07 s (UBSan 73.57 s) |
  | `IntrinsicGeometryScalarfieldExtremaTests.Grouped` | 112.94 s | killed at 120.03 s (UBSan killed at 120.04 s) |

  Where both timings exist (Refinement 34.7 vs 56.3 s; UvAtlasQuality.SmoothCurved 19.1 vs
  32.6 s), the CI runner is 1.6-1.7x slower than this host. Every case listed passes locally
  within the unchanged budget.
- **VariationalFit split: evaluated, not applied.** One per-step timing (a temporary probe, not
  committed) shows that a single product call dominates. "noise-level fidelity" takes 14.90 s
  of the 22.7 s: the bisection on log(weight) in `Geometry.HarmonicField.Fit.cpp` runs about
  2327 operator applications of the reweighted L1 fit, against 11 for one fixed-weight fit.
  After that come uniform bound 2.81 s and per-row bound 2.82 s. All other steps take 0.02-0.5 s.
  - Splitting after "reweighted after TGV", as planned, gives about 2 s plus about 20.6 s. The
    second half misses the gate of at most 15 s locally (at most 30 s divided by the CI factor).
  - The smallest possible split isolates the noise-level step at about 15.0 s, which is on the
    gate without margin.
  - So a test split alone cannot make this case reliable. The operator options are below.
  - The same bisection also dominates the grouped `VariationalFit.NoiseLevelChoosesTheWeightWhoseResidualMatches`
    (22.1 s locally inside `IntrinsicGeometryTests.Grouped`).
- **KMeans / SceneSave.** The cost is the product job on a 300 000-point cloud (`AddCloud(300000)`;
  building the fixture is a cheap loop), which the test uses so that the job spans several
  frames. A deterministic, gate-held job would need a production seam. That goes to the
  operator, below.
- **UBSan `IntrinsicGeometryScalarfieldExtremaTests.Grouped`** (fresh `ci-ubsan` tree,
  `-DINTRINSIC_GROUP_PURE_CTEST=ON`, CI ctest flags): passed in 112.40 s locally at load
  average about 20. Comparing per case with CI UBSan, the runner is 1.87x slower (Orientation
  29.59 vs 15.85 s; Gaussian 14.82 vs 7.94 s; Retriangulation 14.81 vs 7.91 s). The producer
  therefore needs about 210 s on CI UBSan and about 180 s on CI ASan; `IntrinsicGeometryTests.Grouped`
  needs about 160-170 s on CI ASan. A 120 s wrapper cannot hold these producers in their current
  form. Refinement (33.6 s locally) is the largest single case.
- **Cases near the limit on CI ASan** (passed, but likely to tip over):
  `ReusedExecutionPanelsRejectInvalidRequestsBeforePublishing` 29.61 s,
  `EveryEntityInputFollowsSelectionWithSelectionDetailsClosed` 28.24 s,
  `CoherentPointDriftOperations.SamplingMethodsAreSelectableForSubsamplesAndLandmarks` 27.13 s,
  `NamedMeshOutputsUseAppearanceWithoutRecomputing` 24.89 s,
  `ShowButtonsApplyAppearancePropertiesOnMeshGraphAndCloud` 24.11 s.

### Remaining
- Criterion 2 needs an operator decision. No budget, label or grouping was changed. If no
  decision is taken, everything stays as it is and the listed cases stay red on CI. Options:
  - Policy: either split case by case (the CPD precedent), or a sanitizer-only budget, i.e. a
    `ci-asan`/`ci-ubsan` cache variable that scales the 30 s/120 s `TIMEOUT` properties. The
    budget covers all items below at once. Hang detection would get coarser by the same factor.
  - VariationalFit: three-way split (about 2 s / 15 s / 5.7 s locally; the noise-level part has
    no margin on CI), or a cheaper noise-level weight search in the product (warm start or
    tolerance; this changes numerics and needs its own task and review).
  - KMeans/SceneSave: a test seam for a job held by a gate instead of the 300 000-point cloud
    (production seam, own task), or a smaller cloud (a timing-derived parameter, results-bearing).
  - Grouped producers: split them following the `tests/README.md` precedent, together with
    parity and inventory updates, or a longer wrapper timeout for the sanitizer lanes only.
  - The `slow` label is not recommended: it removes the cases from every required CPU lane.
- Criterion 3 needs the coordinator's integrated `ci-linux-clang` dispatch. BUG-240 stays open
  until that dispatch is green, or until every remaining failure has its own task.
