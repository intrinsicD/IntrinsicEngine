---
id: CI-008
theme: H
depends_on:
  - CI-003
  - CI-004
---
# CI-008 — Reduce CTest process overhead without oversubscribing workers

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
- Current registration uses
  `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`, so each of roughly
  3,592–3,594 cases runs as a separate process.
- `intrinsic_grouped_test()` exists in `tests/CMakeLists.txt` but has no call
  sites. Retired `CI-001` recorded grouped RuntimeRHI, RenderOrchestrator,
  HeadlessEngine, RenderGraphPacket, and other entries and a suite size near
  1,599 tests; current gates have more than doubled and the grouped
  registrations are absent, indicating performance drift.
- Representative `CI-003` test phases were 221.27s for 3,526 PR-fast cases and
  217.53s for 3,594 full CPU cases. Build is still dominant, but test process
  overhead is material for rapid iteration.
- `Core::Tasks::Scheduler::Initialize(0)` creates
  `hardware_concurrency - 1` workers. Multiple runtime test processes under
  parallel CTest can therefore oversubscribe a hosted runner. `PROCESSORS`,
  explicit test worker configuration, and CTest `-j` must be evaluated
  together.

## Required changes
- [ ] Inventory candidate suites and start with pure core, geometry, and
      contract executables that have no mutable process-global state or
      backend/device lifetime.
- [ ] Restore grouped execution through `intrinsic_grouped_test()` or a
      successor while preserving individual discovered cases for local
      diagnosis and `ctest -R`.
- [ ] Generate a CI execution plan that runs every selected GoogleTest case
      exactly once: grouped suites must replace, not duplicate, their individual
      processes in CI, with mechanical enumeration parity.
- [ ] Define and test reset requirements before grouping any fixture with global
      scheduler, logging, registry, filesystem, environment, windowing, RHI, or
      runtime state.
- [ ] Measure process count and wall time for ungrouped versus grouped plans at
      CTest `-j1`, `-j2`, and `-j4`.
- [ ] Add explicit scheduler worker control for runtime-bearing test processes
      where needed, and set CTest `PROCESSORS` for tests that reserve multiple
      workers.
- [ ] Choose grouped scope, CTest concurrency, and worker budgets from at least
      five comparable samples; retain individual failure names/results in
      uploaded GTest/CTest diagnostics.

## Tests
- [ ] Add parity tooling that compares `--gtest_list_tests` with grouped plus
      ungrouped CI selections and fails on missing or duplicate cases.
- [ ] Add process-global reset regressions for every non-pure suite proposed for
      grouping.
- [ ] Run the default CPU selector through both individual and grouped plans and
      compare pass/skip/fail case counts exactly.
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
python3 tests/regression/tooling/Test.GroupedCTestParity.py --build-dir build/ci
ctest --test-dir build/ci --output-on-failure -L ci-grouped --timeout 60 -j<measured-value>
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j<measured-value>
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Running grouped and corresponding individual entries in the same CI plan.
- Grouping runtime, GPU, filesystem, or global-state suites without reset proof.
- Hiding individual test failures behind one opaque grouped result.
- Changing assertions or coverage to improve process count.
