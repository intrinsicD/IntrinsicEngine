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
- [x] The ProgressivePoisson panel failure is reproduced (ASan build) and fixed at its root cause.
      Met: independent Claude and Codex reviews of the BUG-240 lane (coordinator review record,
      lane `BUG-240+BUG-188`).
- [x] Each timeout is either brought within budget or given a diagnosed, documented sanitizer
      budget/lane; no gate weakened without diagnosis. Independent review of `30d1a148d`:
      "Criterion 2: met."
- [x] A manual `ci-linux-clang` dispatch shows both sanitizer jobs and `cpu-test-selection-parity`
      green, or every remaining failure has its own task. Met by hosted dispatch 40105034304; an
      independent reviewer confirms it in the next step.

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

### Timeouts (criterion 2): diagnosis
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

### Sanitizer case budgets (criterion 2): operator policy applied
- **Decision (2026-10-24, verbatim):** "ok dann Faktor 2 und variations fit als eigenen Produkt
  task." That is the sanitizer-only budget policy with factor 2, plus a product task for the
  noise-level weight search ([GEOM-117](../backlog/geometry/GEOM-117-cheaper-variational-fit-noise-level-search.md)).
- **Which lanes.** A tree is a sanitizer lane when its resolved `INTRINSIC_SANITIZER_IDENTITY`
  (from `INTRINSIC_SANITIZER_MODE`/`INTRINSIC_ENABLE_SANITIZERS`) is not `none`: `ci-asan`,
  `ci-ubsan`, `ci-vulkan` (ASan+UBSan) and the `dev` presets. `ci`, `ci-fast`, `ci-release`,
  `ci-coverage-cpu` and `ci-vulkan-release` are unsanitized and unchanged.
- **Change.** `tests/CMakeLists.txt` declares `_intrinsic_sanitizer_test_timeout_scale 2` and
  multiplies, in sanitizer trees only: the discovered-case `TIMEOUT` (30 -> 60 s, `slow`
  120 -> 240 s), every `add_test` `TIMEOUT` in the directory, including grouped wrappers
  (120 -> 240 s), and every literal `TIMEOUT` of the generated discovery fixups (for example
  Refinement 60 -> 120 s). The 5 s discovery budget is unchanged. Benchmark smokes in
  `benchmarks/CMakeLists.txt` are not scaled. `tests/README.md` documents the policy.
- **Regression.** `Test.GTestDiscoveryTimeout.py` reads the tree's `INTRINSIC_SANITIZER_IDENTITY`
  from `CMakeCache.txt` and requires scaled case budgets in a sanitizer tree and unscaled ones
  otherwise; a missing identity fails closed. Its new hermetic checks failed before the CMake
  change (no declared scale) and pass after it.
- **Verification (this session).** A sanitizer tree configured as
  `cmake --preset ci -B build/ci-asan-bug240 -DINTRINSIC_ENABLE_SANITIZERS=ON -DINTRINSIC_SANITIZER_MODE=address`
  (the `ci-asan` cache values on the already installed `ci` vcpkg tree; configure only):
  before the change its includes carried 35 x 30 s and 6 x 120 s. After it, `--build-dir` reconcile
  reported `timeout_scale=2 case_timeouts={60: 35, 240: 6}`. All 98 `add_test` timeouts of
  `tests/` doubled (`ctest -N --show-only=json-v1`, before/after diff). The 8 benchmark smokes
  were unchanged, and the fixup file's literals doubled. `cmake --preset ci` reconcile still
  reports `timeout_scale=1 case_timeouts={30: 35, 120: 6}`. The full
  `Test.GTestDiscoveryTimeout.py` passes.
- **What this does not settle.** Hang detection in sanitizer lanes is coarser by 2x. The diagnosed
  producers need about 160-210 s on CI against the new 240 s wrapper budget, so their margin is
  small. KMeans/SceneSave (about 20 s locally, 1.6-1.9x on CI) fit the new 60 s case budget.
- **2026-10-24 follow-up: routing checker.** The scale change broke the sanitizer jobs' routing
  step (`ci-linux-clang` dispatch 40099074759, asan and ubsan): `Test.TestGateRouting.py`
  pinned grouped wrappers to 120 s, but sanitizer trees configure with
  `INTRINSIC_GROUP_PURE_CTEST=ON` and now carry 240 s. The checker now expects slow budget x
  tree scale, reusing `case_budgets()`/`tree_scale()` from `Test.GTestDiscoveryTimeout.py`; it
  still requires the exact value in both tree kinds. Local grouped ASan tree
  (`cmake --preset ci -B build/ci-asan-routing -DINTRINSIC_ENABLE_SANITIZERS=ON
  -DINTRINSIC_SANITIZER_MODE=address -DINTRINSIC_GROUP_PURE_CTEST=ON`, `IntrinsicCpuTests`
  built): before, the same CI error; after, `ok: targets=28 cases=5837`. `build/ci` and
  `--self-test` pass. `tools/ci/source_coverage.py` keeps its 120 s pin because coverage and
  sanitizers are mutually exclusive.

### Hosted CI (criterion 3)
- `ci-linux-clang` `workflow_dispatch` run 40105034304 on `main` `dde5776d6`: conclusion
  success; jobs `full-cpu`, `sanitizer-tests (asan, ci-asan)`, `sanitizer-tests (ubsan, ci-ubsan)`
  and `cpu-test-selection-parity` all success. Push run 40105035007 on the same commit: `full-cpu`
  success.
- CTest totals: ASan 4110/4110 passed, 7 skipped (display/locale), 2232.93 s; UBSan 4110/4110
  passed, the same 7 skipped, 957.87 s.
- Previously failing cases, CI duration against the new budget (cases 60 s, grouped wrappers
  240 s):

  | Case | ASan | UBSan |
  |---|---|---|
  | `PropertySmoothingWorkflows.VariationalFitVariantsAndSolverSwitches` | 43.84 s / 60 | 14.20 s / 60 |
  | CPD `...UseTheAutomaticLowRank` / `...KeepTheFullKernelWhenExactIsAsked` | 3.67 / 14.65 s | 2.39 / 8.83 s |
  | `SandboxAgentServer.KMeansProgressFollowsTheRunsOwnJob` | 31.43 s / 60 | 14.17 s / 60 |
  | `SandboxAgentServer.SceneSaveProgressNamesTheSceneJob` | 31.19 s / 60 | 7.71 s / 60 |
  | `IntrinsicGeometryTests.Grouped` | 180.07 s / 240 | 73.59 s / 240 |
  | `IntrinsicGeometryScalarfieldExtremaTests.Grouped` | 183.91 s / 240 | 208.59 s / 240 |
  | `SandboxProcessingPanels.ProgressivePoissonManualAndDebouncedRunsShareConfigApply` | 7.02 s | 1.15 s |

- Thinnest margin: UBSan `IntrinsicGeometryScalarfieldExtremaTests.Grouped` at 208.59 s of 240 s
  (87%), matching the predicted about 210 s. ASan `VariationalFit` (73% of 60 s) is owned by
  GEOM-117.

## Completion — 2026-10-24
Completed 2026-10-24. Commits: `dd65adee6` (Poisson panel fixed-step UI clock), `30d1a148d`
(sanitizer case budgets x2), `dde5776d6` (routing checker follows the tree scale); follow-up
`66748220a` files [GEOM-117](../backlog/geometry/GEOM-117-cheaper-variational-fit-noise-level-search.md)
for the noise-level weight search. All on `main`.
- Operator decision on the budget policy (verbatim): "ok dann Faktor 2 und variations fit als
  eigenen Produkt task."
- Two CI-only regressions surfaced on the way and were fixed: the routing baseline missed a new
  BUG-193 case (`b233910ad`), and `Test.TestGateRouting.py` pinned grouped wrappers to 120 s in
  scaled sanitizer trees (`dde5776d6`, after dispatch 40099074759).
- Margins: see the table above; the UBSan ScalarfieldExtrema producer (208.59 s of 240 s) is the
  thinnest and will tip over first if those producers grow.
- Limits: hang detection is 2x coarser in every sanitizer lane, `ci-vulkan` (ASan+UBSan) included.
  Criterion 3 rests on one hosted dispatch; its independent confirmation is the next step.
- Raw evidence stays git-ignored in the `asan` lane worktree (`tasks/evidence/BUG-240/`); nothing
  tracked to delete.
