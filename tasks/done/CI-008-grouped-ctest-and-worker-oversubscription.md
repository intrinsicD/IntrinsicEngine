---
id: CI-008
theme: H
depends_on:
  - CI-003
  - CI-004
  - CI-006
  - CI-010
  - CI-011
---
# CI-008 — Reduce CTest process overhead without oversubscribing workers

## Status
- Completed on 2026-07-18 at `Operational`; owner: Codex; branch: `main`.
- Commit: `7d02187f` enables replacement-only grouping in
  required CPU variants, preserves local individual discovery, and reserves
  peak runnable capacity for deliberate multi-worker cases. Evidence-lane
  commit `886495e7` shares one product build across both registration plans.
- Hosted source coverage, 15 matched timing/parity pairs, and the final
  unsanitized/ASan/UBSan correctness and selection-parity workflow all passed.
- Sanitizer concurrency was deliberately not calibrated from the unsanitized
  measurements: ASan and UBSan remain conservative at `--parallel 1`, and no
  sanitizer speedup is claimed.

## Goal
- Execute safe pure suites in grouped GoogleTest processes and assign measured
  CTest/process worker budgets so thousands of process launches and nested
  scheduler pools no longer dominate test feedback.

## Non-goals
- No deletion or merging-away of individual GoogleTest cases.
- No grouped runtime/GPU suite until shared/global state reset is proved.
- No blanket `-j$(nproc)` increase or decrease without comparable A/B evidence.

## Context
- Owner: test CMake helpers, test support, CTest execution planning, and
  scheduler configuration exposed to tests.
- At activation, every GoogleTest case used
  `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` and the existing grouped
  helper had no live call sites. The current default CPU selector has 4,062
  logical records across 28 producers.
- Retired `CI-001` recorded additive grouped RuntimeRHI, RenderOrchestrator,
  HeadlessEngine, RenderGraphPacket, and other entries against a much smaller
  historical population. This task supersedes that design with mechanically
  verified replacement-only grouping.
- Representative `CI-003` test phases were 221.27s for 3,526 PR-fast cases and
  217.53s for 3,594 full CPU cases. Build is still dominant, but test process
  overhead is material for rapid iteration.
- `Core::Tasks::Scheduler::Initialize(0)` creates
  `hardware_concurrency - 1` workers. Multiple runtime test processes under
  parallel CTest can therefore oversubscribe a hosted runner. `PROCESSORS`,
  explicit test worker configuration, and CTest `-j` must be evaluated
  together.
- `CI-006` first fixes the retained unsanitized/ASan/UBSan variant topology;
  `CI-010` supplies source-region parity; and `CI-011` supplies the truthful
  fast/slow cohort. Worker A/B results before those inputs settle would compare
  different workloads.
- Runtime already exposes `SimulationConfig::WorkerThreadCount`, and direct
  scheduler fixtures already choose a worker count. This task should use those
  seams rather than add `IExecutor`, a test-only environment variable, or a
  second scheduler-control surface.

## Slice plan
- Slice A — group only dependency-pure Core/Geometry/contract families. Compare
  a separate CI configure that uses existing `NO_DISCOVER` plus grouped
  registration against any same-tree filtering design; prefer the separate
  configure when it preserves exact case/XML parity with less policy machinery.
  The normal `ci` configuration retains individual discovery for local
  diagnosis.
- Slice B — after the grouped cohort is fixed, measure CTest process count and
  explicit in-process worker budgets for the unsanitized full CPU plan. Retain
  ASan and UBSan at `--parallel 1` unless matched sanitizer evidence supports a
  change, and verify their grouped representation through the final correctness
  gate.

## Required changes
- [x] Inventory candidate suites and start with pure core, geometry, and
      contract executables that have no mutable process-global state or
      backend/device lifetime.
- [x] Restore grouped execution through the existing
      `intrinsic_grouped_test()`/`NO_DISCOVER` helpers unless evidence proves a
      smaller extension is required; do not build a parallel registration
      framework.
- [x] Preserve individual discovered cases in the normal local/`ci`
      configuration for diagnosis and `ctest -R`; use a separately configured
      CI grouped plan if same-tree registration would require fragile label or
      duplicate-suppression machinery.
- [x] Generate a CI execution plan that runs every selected GoogleTest case
      exactly once: grouped suites must replace, not duplicate, their individual
      processes in CI, with mechanical enumeration parity.
- [x] Define and test reset requirements before grouping any fixture with global
      scheduler, logging, registry, filesystem, environment, windowing, RHI, or
      runtime state.
- [x] Measure process count and wall time for ungrouped versus grouped plans at
      CTest `-j1`, `-j2`, and `-j4`.
- [x] Use `SimulationConfig::WorkerThreadCount` or direct scheduler
      initialization for runtime-bearing fixtures, and set CTest `PROCESSORS`
      for tests that reserve multiple workers; add no new worker-control API.
- [x] Choose grouped scope, CTest concurrency, and worker budgets from at least
      five comparable samples; retain individual failure names/results in
      uploaded GTest/CTest diagnostics.

## Tests
- [x] Add parity tooling that compares `--gtest_list_tests` with grouped plus
      ungrouped CI selections and fails on missing or duplicate cases.
- [x] Compare exact GoogleTest case identities and GTest XML pass/skip/fail
      records; a lower CTest process count is expected and is not case loss.
- [x] Keep every non-pure/global-state suite individually registered; no such
      suite was proposed for grouping, so no speculative reset seam was added.
- [x] Run the default CPU selector through both individual and grouped plans and
      compare pass/skip/fail case counts exactly.
- [x] Use `CI-010` to prove unchanged covered production line/branch/region sets
      for the test-registration-only grouping change.
- [x] Record process count, selected case count, and median/p95 test duration
      against `CI-003`.

## Docs
- [x] Document grouped-suite eligibility, reset obligations, CI/local command
      differences, and worker-budget policy in `tests/README.md`.
- [x] Update `CI-001`'s historical assumptions only through a linked current
      note; do not rewrite the retired task.
- [x] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] CI executes each selected assertion-bearing GoogleTest case exactly once
      while local individual CTest entries remain available.
- [x] Initial grouping is limited to suites with proven process isolation/reset.
- [x] CTest and scheduler worker counts are explicit; full CPU settings are
      measured, and sanitizer settings remain conservatively serial rather than
      inferred independently from host core count.
- [x] Test wall-time and process-count deltas are reported against the named
      baseline with unchanged pass/skip/fail totals.

## Evidence
- Matched source-coverage run
  [`29620815336`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29620815336)
  at `4ab70245`, artifact `8422537827`
  (`sha256:cad43ca733d69d8747f521eea1e95bcd91f05a50ca41fcc8368e7082530cc145`),
  reported zero gained or lost covered production lines, regions, and branch
  arms between the individual and grouped plans.
- Matched timing run
  [`29625346673`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29625346673)
  at `886495e7`, artifact `8424178333`
  (`sha256:344f0f8d8960791407e16c2bb6b3f0d704e255d482e6853dac3e459c64b60dc2`),
  built the product once and inode-verified all 28 hardlinked producer binaries.
  Registration retained 4,061 logical GoogleTest cases plus one manual CTest
  across 28 producers. Five grouped producers represented 1,351 cases, reducing
  physical records from 4,062 to 2,716.
- All five matched pairs at each of `--parallel 1`, `2`, and `4` had exact
  logical status parity: 4,055 passed, six disabled/not-run, and no failures or
  errors. Individual versus grouped median/p95 seconds were
  `72.278999/73.071192` versus `62.085994/62.533167` at j1,
  `39.256532/39.445179` versus `37.201451/37.313669` at j2, and
  `27.715948/27.847886` versus `29.489894/29.551698` at j4. The required
  workflow retains j4 because it is the fastest absolute grouped plan; grouping
  itself regressed the j4 A/B median/p95 by `6.400%/6.118%`.
- The 41 deliberate multi-worker cases carry exact reservations:
  22 use `PROCESSORS=3` and 19 use `PROCESSORS=4`. Required ASan and UBSan
  execution remains serial at `--parallel 1`.
- Final required workflow
  [`29627099771`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29627099771)
  at `886495e7` passed unsanitized full CPU, ASan, UBSan, and cross-variant
  selection parity. Each variant selected the same 28 producers and 4,062
  logical records at digest
  `e8dc6d95d59c30c2317ea3a27b0395011583fd11a6e3e5118d870f47d064e540`;
  each JUnit retained 2,716 physical records, seven capability skips, zero
  failures, and five per-producer XML reports totaling 1,351 grouped cases.
  Result artifacts were `8424472546`
  (`sha256:fa4a8c074137229631750255c6770e84120e8f2015e6c40fbe1a5d83a73b0398`),
  `8424402127`
  (`sha256:822f524658b21ea232a523200d679bf59fc0eeab6bacaa6226f3021865f50d8e`),
  and `8424480112`
  (`sha256:44c2dcb8a47ccc98e0cfe123d2e145e5e9f6950659ff7dab3173ae858001d4f6`).
  Parity artifact `8424481768`
  (`sha256:9ac35fa725a6ed4cc9bbf17a6e34ad7627cfd9c325d0f5cc3236de09e9db9cf5`)
  reproduced the uploaded verdict byte-for-byte.
- Earlier timing run `29622055604` exposed and diagnosed `BUG-113`; because the
  harness correction changed executed test code, none of that run's partial
  timings enter the retained measurements. `CI-009` owns lifecycle/runner
  routing, and `BUG-091` remains the independent PRE_TEST discovery owner.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCpuTests
cmake --preset ci -B build/ci-grouped -DINTRINSIC_GROUP_PURE_CTEST=ON
mkdir -p build/ci-grouped/bin
cp -al build/ci/bin/. build/ci-grouped/bin/
python3 tests/regression/tooling/Test.GroupedCTestParity.py registration --individual-build-dir build/ci --grouped-build-dir build/ci-grouped
ctest --test-dir build/ci-grouped --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --parallel 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --parallel 4
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Running grouped and corresponding individual entries in the same CI plan.
- Grouping runtime, GPU, filesystem, or global-state suites without reset proof.
- Hiding individual test failures behind one opaque grouped result.
- Changing assertions or coverage to improve process count.
- Adding a new executor/scheduler interface or environment-only worker override
  when the existing config/direct initialization seam suffices.
