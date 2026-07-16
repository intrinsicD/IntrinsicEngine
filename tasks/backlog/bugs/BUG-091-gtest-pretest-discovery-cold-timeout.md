---
id: BUG-091
theme: G
depends_on: []
---
# BUG-091 — GoogleTest PRE_TEST discovery times out on a cold start

## Goal
- Keep GoogleTest discovery bounded and fail-closed while preventing a cold or
  contended launch of an unrelated test executable from aborting a focused
  CTest run before its selected tests can start.

## Non-goals
- No retry-until-green behavior or warning-only discovery failure.
- No blanket timeout increase based on one transient observation.
- No grouping, relabeling, or deletion of individual GoogleTest cases.
- No production ECS behavior change unless evidence identifies an ECS startup
  defect.

## Context
- Owner: test registration and CTest harness policy in `tests/CMakeLists.txt`.
- Symptom: on 2026-07-16, immediately after the focused `RUNTIME-178` build,
  CTest aborted before running the selected runtime tests because
  `GoogleTestAddTests.cmake` timed out while enumerating the unrelated
  `build/ci/bin/IntrinsicECSTests`. A direct
  `timeout 30 build/ci/bin/IntrinsicECSTests --gtest_list_tests` immediately
  afterward succeeded in 1.32 s, and the same CTest command then passed.
- Bounded follow-up evidence: three direct warm
  `IntrinsicECSTests --gtest_list_tests` invocations completed in 0.02 s each
  and listed 126 lines. The one-shot failure is therefore not yet a reliable
  reproduction and must not be treated as proof of an ECS code defect.
- `intrinsic_test_executable()` passes `TIMEOUT 30` through `PROPERTIES`; that
  controls each discovered test case. It does not set GoogleTest's separate
  enumeration budget. The generated
  `build/ci/tests/IntrinsicECSTests[1]_include.cmake` records
  `TEST_DISCOVERY_TIMEOUT 5`, CMake 3.28's default.
- `DISCOVERY_MODE PRE_TEST` loads stale discovery files while CTest constructs
  the complete test inventory, before its name/label selector can run the
  requested subset. A newly configured or rebuilt tree can therefore expose
  the focused command to discovery startup from unrelated executables.
- Ranked, falsifiable hypotheses:
  1. The implicit five-second enumeration budget is below the cold/contended
     tail. If so, forced cold discovery will sometimes cross five seconds while
     remaining well below a separately measured bounded discovery budget.
  2. PRE_TEST invalidation fans out across stale executable inventories after a
     configure/build. If so, touching a generated include or rebuilding one
     unrelated target will make a filtered `ctest -N`/run enumerate that target;
     a fresh inventory will not.
  3. Transient CPU, filesystem, sanitizer, or concurrent build/test contention
     delayed process startup. If so, controlled pressure and process telemetry
     will raise discovery p95/p99 without changing the listed test set.
  4. `IntrinsicECSTests` has an intermittent expensive static/sanitizer startup
     path. If so, its cold-listing distribution or startup trace will be an
     outlier against similarly sized test executables even on an otherwise idle
     host.
- `CI-008` is related because it owns CTest process overhead and retains
  individual PRE_TEST registrations, but it is not a prerequisite: grouping
  selected test execution does not remove enumeration of those executables,
  and this fail-closed discovery-budget defect can be diagnosed independently.

## Required changes
- [ ] Add a deterministic harness that forces PRE_TEST rediscovery and records
      enumeration wall time, exit status, executable timestamp, discovery-file
      timestamp, host load, and concurrent build/CTest processes.
- [ ] Collect idle warm, idle cold, and controlled-contention distributions for
      `IntrinsicECSTests` plus representative core/runtime executables; use
      enough repetitions to report median, p95, p99, and maximum rather than a
      single retry.
- [ ] Confirm whether filtered CTest inventory construction discovers unrelated
      stale executables and quantify how many enumerations one focused command
      can trigger after configure and focused/full builds.
- [ ] Choose the smallest evidence-backed correction: set a distinct explicit
      `DISCOVERY_TIMEOUT` in `intrinsic_test_executable()`, eliminate a proven
      pathological startup path, or reduce unnecessary PRE_TEST invalidation/
      fan-out without losing individual test registration.
- [ ] Keep discovery and test-execution budgets separate and make both visible
      in generated-registration policy coverage.

## Tests
- [ ] Add a tooling regression with a controlled slow fake GoogleTest lister
      that proves discovery succeeds below the declared budget and fails
      nonzero above it.
- [ ] Add registration-policy coverage proving the helper emits the selected
      `TEST_DISCOVERY_TIMEOUT` independently of each test's `TIMEOUT` property.
- [ ] Re-run forced rediscovery repeatedly on the configured `ci` tree under
      idle and controlled-contention conditions; preserve all enumeration
      failures and timings.
- [ ] Run a focused selector whose requested tests are unrelated to ECS and
      confirm stale `IntrinsicECSTests` discovery can no longer cause an
      ordinary-cold-start false failure under the supported budget.

## Docs
- [ ] Document the distinction between GoogleTest enumeration timeout and CTest
      case timeout, including the evidence-backed budget and failure behavior,
      in `tests/README.md`.
- [ ] Cross-link `CI-008` as related process-overhead work without making either
      task an artificial prerequisite; update the bug index and retirement log
      when the correction is verified.

## Acceptance criteria
- [ ] A deterministic forced-rediscovery loop reproduces the old five-second
      failure or provides sufficient cold/contention evidence to reject that
      hypothesis before any fix is selected.
- [ ] GoogleTest enumeration remains bounded and a genuinely hung lister fails
      the gate with an actionable executable name and budget.
- [ ] Repeated cold and contended focused runs stay inside the declared
      discovery budget without retries, skips, or label exclusions.
- [ ] Individual `gtest_discover_tests` cases and focused `ctest -R` filtering
      remain available with unchanged selected-case counts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests
cmake -E touch 'build/ci/tests/IntrinsicECSTests[1]_include.cmake'
ctest --test-dir build/ci -N -R '^RuntimeModule\.'
timeout 30 build/ci/bin/IntrinsicECSTests --gtest_list_tests
python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py --build-dir build/ci
ctest --test-dir build/ci --output-on-failure -R '^RuntimeModule\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Masking discovery failure with retries, warning-only handling, or exclusions.
- Reusing the per-test `TIMEOUT` value as a discovery budget without separate
  evidence and policy naming.
- Treating one passing rerun as proof that the cold-start defect is fixed.
- Folding the work into `CI-008` unless evidence shows grouping/process-budget
  changes are necessary to solve discovery rather than only test execution.
