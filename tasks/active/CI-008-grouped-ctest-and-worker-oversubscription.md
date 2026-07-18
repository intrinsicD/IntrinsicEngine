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
- In progress on 2026-07-18; owner: Codex; branch: `main`.
- Promotion commit `7d02187f` enables replacement-only grouping in required CPU
  variants, preserves local individual discovery, and reserves peak runnable
  capacity for deliberate multi-worker cases.
- Hosted source-coverage run `29620815336` passed exact grouped/individual
  parity. Timing run `29622055604` passed registration and seven pairs, then
  exposed `BUG-113`: an unchanged runtime contract assumed one-frame async
  asset completion under `--parallel 2`. Commit `02945683` now drives the
  existing completion seam, and `886495e7` removes the evidence lane's
  duplicate 27-minute product build.
- Replacement timing/parity run `29625346673` passed all 15 fresh reports at
  `886495e7`; `--parallel 4` is the fastest grouped-plan budget by absolute
  median and p95. Final required CPU/ASan/UBSan run `29627099771` is in
  progress at the same SHA.

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
  explicit in-process worker budgets across the retained unsanitized, ASan, and
  UBSan variants, then encode only the winning supported-runner settings.

## Required changes
- [ ] Inventory candidate suites and start with pure core, geometry, and
      contract executables that have no mutable process-global state or
      backend/device lifetime.
- [ ] Restore grouped execution through the existing
      `intrinsic_grouped_test()`/`NO_DISCOVER` helpers unless evidence proves a
      smaller extension is required; do not build a parallel registration
      framework.
- [ ] Preserve individual discovered cases in the normal local/`ci`
      configuration for diagnosis and `ctest -R`; use a separately configured
      CI grouped plan if same-tree registration would require fragile label or
      duplicate-suppression machinery.
- [ ] Generate a CI execution plan that runs every selected GoogleTest case
      exactly once: grouped suites must replace, not duplicate, their individual
      processes in CI, with mechanical enumeration parity.
- [ ] Define and test reset requirements before grouping any fixture with global
      scheduler, logging, registry, filesystem, environment, windowing, RHI, or
      runtime state.
- [ ] Measure process count and wall time for ungrouped versus grouped plans at
      CTest `-j1`, `-j2`, and `-j4`.
- [ ] Use `SimulationConfig::WorkerThreadCount` or direct scheduler
      initialization for runtime-bearing fixtures, and set CTest `PROCESSORS`
      for tests that reserve multiple workers; add no new worker-control API.
- [ ] Choose grouped scope, CTest concurrency, and worker budgets from at least
      five comparable samples; retain individual failure names/results in
      uploaded GTest/CTest diagnostics.

## Tests
- [ ] Add parity tooling that compares `--gtest_list_tests` with grouped plus
      ungrouped CI selections and fails on missing or duplicate cases.
- [ ] Compare exact GoogleTest case identities and GTest XML pass/skip/fail
      records; a lower CTest process count is expected and is not case loss.
- [ ] Add process-global reset regressions for every non-pure suite proposed for
      grouping.
- [ ] Run the default CPU selector through both individual and grouped plans and
      compare pass/skip/fail case counts exactly.
- [ ] Use `CI-010` to prove unchanged covered production line/branch/region sets
      for the test-registration-only grouping change.
- [ ] Record process count, selected case count, and median/p95 test duration
      against `CI-003`.

## Docs
- [ ] Document grouped-suite eligibility, reset obligations, CI/local command
      differences, and worker-budget policy in `tests/README.md`.
- [ ] Update `CI-001`'s historical assumptions only through a linked current
      note; do not rewrite the retired task.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] CI executes each selected assertion-bearing GoogleTest case exactly once
      while local individual CTest entries remain available.
- [ ] Initial grouping is limited to suites with proven process isolation/reset.
- [ ] CTest and scheduler worker counts are explicit and measured, not inferred
      independently from host core count.
- [ ] Test wall-time and process-count deltas are reported against the named
      baseline with unchanged pass/skip/fail totals.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCpuTests
cmake --preset ci -B build/ci-grouped -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build build/ci-grouped --target IntrinsicCpuTests
python3 tests/regression/tooling/Test.GroupedCTestParity.py --individual-build-dir build/ci --grouped-build-dir build/ci-grouped
ctest --test-dir build/ci-grouped --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j<measured-value>
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j<measured-value>
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
