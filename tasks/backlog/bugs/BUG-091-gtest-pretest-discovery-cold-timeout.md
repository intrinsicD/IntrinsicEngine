---
id: BUG-091
theme: G
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive discovery-failure follow-up; command logs and the existing diagnostic plan retain evidence.
contract_schema: 1
contracts: []
contract_review: The catalog has no CTest discovery-timeout contract; this task changes test enumeration policy without changing engine APIs, method contracts or layer ownership.
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
- `CI-011` is related because it owns the measured slow-test cohort, but it is
  likewise not a prerequisite and does not own GoogleTest enumeration. A slow
  discovery path is a harness defect, not a reason to relabel assertions.

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


## Additional observation — 2026-09-17

During UI-037 compilation/reuse work on the Clang 23 `ci-vulkan` tree,
`ctest --test-dir build/ci-vulkan -N -L gpu -L vulkan -R
'PointLBVHGpuSmoke|PointConstructionGpuSmoke'` exited 8 before listing cases.
`GoogleTestAddTests.cmake:112` reported `Process terminated due to timeout`
while enumerating `build/ci-vulkan/bin/IntrinsicPointLBVHGpuTests`, with empty
output. The failure came after reconfiguration and before rebuilding the target
for the current source, and before any requested GPU test assertions ran. The
CPU target build had just completed; the focused CPU run and Vulkan rebuild
started after this failure. This is another enumeration observation, not proof
of a kernel regression or a root-cause finding. No discovery/test budget was
changed. A bounded direct listing after the rebuild and a normal CTest invocation
are the diagnostic follow-up; results are recorded below when available.


After the target rebuild, one bounded direct invocation,
`/usr/bin/time -f 'elapsed_seconds=%e exit_status=%x' timeout 30
build/ci-vulkan/bin/IntrinsicPointLBVHGpuTests --gtest_list_tests`, succeeded in
0.04 s with exit 0 and the expected PointLBVHGpuSmoke/PointConstructionGpuSmoke
inventory. The generated include still specifies `TEST_DISCOVERY_TIMEOUT 5`.
Logs: `/tmp/intrinsic-twelfth-discovery-direct.log` and
`/tmp/intrinsic-twelfth-discovery-direct-time.log`. A subsequent normal selected
CTest execution began running GPU cases successfully; the cold-start root cause
and distribution requirements above remain open.


The subsequent normal CTest invocation completed: all nine selected GPU/Vulkan
publication tests passed, no skips or failures (123.13 s total). Logs are in
`/tmp/intrinsic-twelfth-vulkan-test.log`; the exact selector is in the dated
UI-037 snapshot-comparison checkpoint. This clears the scoped verification run,
not BUG-091's unreproduced cold-start cause. Enrollment in the current micro
workflow moves the original contract-baseline hash unchanged to `consumed`.

## Diagnosis — 2026-10-24

Unattended lane (operator absent until 09:00). No budget value was chosen; the
value question is the operator's.

**Landed** (branch `claude/bug-091-gtest-discovery-timeout`, commits
`d14029314`..`b8fb5b50f` plus the commit carrying this note):
- `tests/CMakeLists.txt` declares `_intrinsic_gtest_discovery_timeout_seconds 5`
  next to the 30 s / 120 s case budgets and passes it as `DISCOVERY_TIMEOUT`.
  The generated includes are byte-identical to before (a reconfigure rewrote
  none of them).
- `tools/ci/gtest_discovery_probe.py`: `direct` reruns the include's exact
  `--gtest_list_tests` command cold (`posix_fadvise` eviction, fincore residency
  0) and warm; `ctest` forces targets stale and records which `_tests.cmake`
  files were relisted or removed. Both record load, PSI, memory, and
  compile/link/ninja/ctest counts. Processes are matched by program name, not by
  `pgrep -f`, which matches its own shell. `decide` holds the frozen rule;
  `report` writes the HTML report.
- `tests/regression/tooling/Test.GTestDiscoveryTimeout.py` (17 hermetic tests,
  plus `--build-dir`): source policy; a slow fake lister passes below the
  declared budget and fails nonzero at it, with path, timeout result and (CMake
  4.x) the budget; the lister is gone afterwards (except CTest 3.31, below) and
  the lock is free; probe, decision-rule and live-reconcile self-checks.
  Wired into `ci-docs.yml` (hermetic) and `ci-linux-clang.yml` (`--build-dir
  build/ci`); `touched_scope.py` maps the probe and `collect_test_timing.py` to
  it. Documented in `tests/README.md` §"GoogleTest discovery and case budgets"
  (CI-008 and CI-011 cross-linked as related, not prerequisites) and
  `tools/ci/README.md`.

**Regression first.** Before the change all 4 initial tests failed ("no
declared discovery budget"), and so did `--build-dir`
(`tasks/evidence/BUG-091/regression-before.log`). Mutations M1-M19 each fail
the matching test (`mutations.log`), including a removed helper argument,
`pgrep -f`-style matching, a fixture budget of B-3, and a missing
parent-death binding. Non-default value check: with the variable set to 7 and a
reconfigure, 41/41 includes showed 7 and the live check passed (case budgets
30 x35 / 120 x6). Removing the argument made it fail. Reverted.

**H2 fan-out (confirmed; predictions written first).** On `build/ci` with 3 of
41 discovered executables built, `ctest -N -R '^RuntimeModule\.'` relisted:
all 3 on the first run; none on a second run; exactly ECS after touching its
include or its executable; none after a no-op reconfigure; all 3 after
touching all 3 executables or after a budget change. Unbuilt targets (38
`_NOT_BUILT`) are never listed. A 3-target relist took 0.8 s for the whole
call; it took 0.09 s without one.

**Distributions (pre-flight Full tier, packet digest
`ef55536f956a55d85c148bdd1ffc1c6dc7157592159bdc18b620762d1c4e0544`, frozen rule
`bug-091-budget/v3`).** Both arms collected: 12 cells x 320 listings, >= 311
valid per cell; 2 x 25 forced-stale cold C-real runs. Both post-arm manifest
gates passed.
- Contention arm (03:15-03:34): a lane-owned `nice -n 10 -j5` clean-rebuild
  loop of `build/ci-fast` plus ambient builds. Median load_1m 5.23; a compile
  ran at 97 % of sample starts; PSI cpu <= 0.2 %, io <= 8.1 %. This is light
  contention on 32 CPUs.
- Idle arm (03:35-03:54): every sample started at load_1m < 2 with no compile
  (median load 1.04).
- Listing wall time, cold/warm median (max), seconds:

  | target | contention cold | contention warm | idle cold | idle warm |
  |---|---|---|---|---|
  | IntrinsicECSTests (129 cases) | 0.0076 (0.025) | 0.0043 (0.005) | 0.0083 (0.010) | 0.0052 (0.006) |
  | IntrinsicCoreTests (32) | 0.0046 (0.012) | 0.0028 (0.004) | 0.0052 (0.006) | 0.0036 (0.004) |
  | IntrinsicRuntimeContractTests (1785) | 0.056 (0.073) | 0.033 (0.038) | 0.058 (0.065) | 0.035 (0.038) |

- M_obs = 0.073 s. No sample was capped or failed. Every target had one listing
  hash, matching the technical baseline. All 50 C-real runs exited 0, relisted
  all 3 forced targets and selected 7 tests; they took 0.81-0.90 s.
- Verdicts: **H1 rejected** under the tested conditions (M_obs < 2.5 s, no
  kill condition). This matches the written prediction. **H3 not supported**:
  the contention/idle p99 ratio was 0.97-1.18 cold and 0.88-0.93 warm. The
  written prediction ("supported for cold") was wrong. **H4 not supported**: 0
  outliers. Decision: no value chosen; the explicit 5 s stays.
- Referee recomputation from the raw JSONL agrees
  (`runs/referee-audit.txt`). The report is `runs/report.html`; its layout was
  not inspected in a browser.
- What this does not cover: the two incidents (right after a configure plus
  build; one on `ci-vulkan`) remain unexplained. Sanitized trees, CI runners and
  heavy or equal-priority contention were not measured. The samples are
  serially correlated, so the 0.99^300 tail statement is descriptive only.

**Focused confirmation (predictions written first).** 3 runs of `ctest
--test-dir build/ci --output-on-failure -R '^RuntimeModule\.' -LE
'gpu|vulkan|slow|flaky-quarantine' --timeout 60`, each with all 3 targets forced
stale and evicted: exit 0, 3 targets relisted, "100% tests passed, 0 tests
failed out of 7", 1.33-1.37 s each.

**CMake portability.** The hermetic suite passes with CMake 4.3.2, 3.28.3 and
3.31.6. The ci-docs discovery step and `--build-dir` on a tree configured with
3.31.6 pass as well (`portability.log`). Findings:
- CTest 3.31.6 returns at the discovery budget (rc 8) but leaves the timed-out
  lister running. Reproduced with a 30 s fake; 3.28.3 and 4.3.2 kill it. The
  lister-gone assertion is therefore scoped: on 3.31 the test reaps the lister
  and prints a note, and the gate bounds are still asserted.
- The 3.22, 3.28 and 3.31 modules print the path and the timeout result but not
  the budget; only CMake 4.x prints `Timeout: '<N>'`.
- `Test.ConcurrentCTestDiscovery.py` fails 3/3 with local CMake 4.3.2 ("CMake
  flush implementation changed": the 4.3 module uses REMOVE+APPEND). It passes
  with 3.31.6. This predates the change and is not fixed here.

**Pre-flight reviews** (verbatim under
`tasks/evidence/BUG-091/preflight/distributions/reviews/`): an independent Codex
review (thread `01a16b3f-fc98-7582-8371-323cde8b395a`, read-only sandbox;
recursion protection instruction-only) and an independent Claude review
(session `23f9b8d2-46a2-4a62-ab4f-2943d75327e1`, Read/Grep/Glob only;
enforced). They ran 7 rounds, with REVISE until revision 7. Both returned PASS on
digest `ef55536f…` before either arm started. The plan counter-review's five
blocking items were implemented. Among them: program-name process matching,
M_obs from direct samples only, the H1-gated rule, an unconditional
non-default-value check, and "n < 300 = not collected".

**Chosen defaults** (operator may override): value 5 kept explicit; contention
source = own niced `-j5` build in a separate tree (lane limit), with a load gate
of 4; arms in fixed order (contention, then idle); probe children bound by
`PR_SET_PDEATHSIG`; the CTest 3.31 lister check scoped as above; the touched-scope
broad C++ build was skipped, because the CMake change is byte-identical in
generated output.

**Remaining.** Independent code review (session
`29987edd-d5fe-403b-9503-0c6c39b64871`, Read/Grep/Glob only): round 1 REVISE
(docs-sync, CMake versions), round 2 REVISE (README failure text vs CTest 3.31);
both fixed in the following commits; the final round's verdict is recorded in
`tasks/evidence/BUG-091/code-review/`. Whether the evidence
satisfies acceptance criteria 1-4, especially AC2 (budget in the failure
message) on CMake < 4, is the reviewers' and operator's call. Index and
retirement-log updates belong to the integrator.
