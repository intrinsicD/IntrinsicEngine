---
id: BUG-249
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: test-harness portability defect found in the BUG-091 review; the reproduction is the unchanged regression command, recorded below
contract_schema: 1
contracts: []
contract_review: Private CMake test instrumentation only; the catalog has no CTest discovery contract, and production discovery locking and engine behavior stay unchanged.
---
# BUG-249 — Test.ConcurrentCTestDiscovery.py fails on CMake 4.x

## Goal
- Make `tests/regression/tooling/Test.ConcurrentCTestDiscovery.py` recognise
  the CMake 4.x `GoogleTestAddTests.cmake` write implementation, or fail at
  once with an actionable unsupported-version message, without weakening what
  its three cases check.

## Context
- Reproduced 2026-10-24 at `3e490d055` with the host's `cmake` 4.3.2 (PyPI
  wheel in `~/.local/bin`): `python3
  tests/regression/tooling/Test.ConcurrentCTestDiscovery.py` ran 3 tests,
  `FAILED (failures=3)`, each `AssertionError: 0 != 2 : CMake flush
  implementation changed` in `project()`. The BUG-091 lane recorded it as
  pre-existing, and its re-reviewer reproduced it at base `599f02ff1`. The
  same command passes with CMake 3.28.3 and 3.31.6 (PyPI wheels).
- Cause: the probe instruments the module's initial `file(WRITE ...)`. It knows
  the 3.28 form (`file(${flush_tests_MODE} ...)`, one site) and the 3.31 form
  (`file(${file_write_mode} "${arg_CTEST_FILE}" ...)`, two sites; BUG-214).
  The 4.3 module has no WRITE mode: it runs `file(REMOVE "${arg_CTEST_FILE}")`
  once, then `file(APPEND ...)` for each chunk over 50000 characters and for
  the rest after `# Write remaining content to the CTest script`.
- CI: `ci-docs.yml` runs this regression on `ubuntu-latest`. The latest `main`
  run (39957196726 on `599f02ff1`, success) used image `ubuntu-24.04`
  `20261012.334`, whose readme lists CMake 3.31.6. CI is green now; a runner
  image with CMake 4.x would turn `ci-docs` red. Only 4.3.2 was checked among
  the 4.x versions.
- Related: BUG-176 (this regression and the production lock), BUG-214 (the 3.31
  form), BUG-091 (found it).
- Open choice (operator): the unsupported-version branch of the first criterion
  keeps the regression a hard failure, but it leaves CMake 4.x without race
  coverage and `ci-docs` red on a 4.x image, and the task could still retire.

## Acceptance criteria
- [ ] On CMake 4.3.2 the three cases (unlocked duplicate-append control,
      guarded serialization with nested CTest, genuine-duplicate
      preservation) run and pass, or the probe fails at once with a message
      naming the CMake version and the unrecognised module form. An unknown
      form stays a hard failure, never a skip or a pass.
- [ ] On each recognised form the unlocked control still shows the duplicated
      registrations, and a mutation that drops the instrumentation fails the
      matching case.
- [ ] The cases still pass on CMake 3.28.3 and 3.31.6. Installed CMake modules
      and `cmake/IntrinsicTestDiscovery.cmake` stay unchanged.

## Verification
```bash
cmake --version
python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
python3 -m pip install --disable-pip-version-check --target /tmp/bug249-cmake/3.28.3 'cmake==3.28.3'
python3 -m pip install --disable-pip-version-check --target /tmp/bug249-cmake/3.31.6 'cmake==3.31.6'
env PATH=/tmp/bug249-cmake/3.28.3/cmake/data/bin:$PATH python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
env PATH=/tmp/bug249-cmake/3.31.6/cmake/data/bin:$PATH python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py
python3 tests/regression/tooling/Test.TestBuildAggregates.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Resolution — 2026-10-24
- Root cause (module source, ten PyPI wheels plus the host 4.3.2): the probe
  knew two CTest-file write forms and fell through to the 3.31 count for
  anything else (`0 != 2`). The forms per version are:

  | CMake | `GoogleTestAddTests.cmake` write form |
  |---|---|
  | 3.28.3 | `file(${flush_tests_MODE} ...)` in a macro, 1 site |
  | 3.31.6, 3.31.10, 4.0.3, 4.1.3 | `file(${file_write_mode} ...)`, 2 sites |
  | 4.2.0, 4.2.3, 4.3.2, 4.3.4, 4.4.4 | `file(REMOVE "${arg_CTEST_FILE}")` once, then `file(APPEND ...)` at 2 sites (chunk flush in macro `write_test_to_file`, final write) |

  The defect therefore affects CMake 4.2 and later, not 4.0/4.1.
- Gap found on the way: removing the barrier hooks or the done hook still
  passed on every form, because the unlocked control only asserts "same
  names, some duplicated". Criterion 2 was not enforced before this change.
- Change (only `tests/regression/tooling/Test.ConcurrentCTestDiscovery.py`):
  - The locate probe also reads `${CMAKE_VERSION}`. The form is chosen
    explicitly (3.28 / 3.31-4.1 / 4.2+), and anything else fails with
    `CMake <version>: unrecognised CTest-file write form in <module>; ...
    Extend its instrumentation.`. Every exact-count assertion (write sites,
    final flush, 4.2+ REMOVE, function entry) carries that message. The 3.28
    and 3.31 instrumentation is unchanged.
  - 4.2+: `before-write` runs before the REMOVE, and `after-write` runs once
    after the first APPEND (a flag set after the REMOVE; macros share the
    function scope). The done hook follows the final APPEND. The I/O
    statements themselves are untouched.
  - `concurrent()` asserts that every expected hook marker exists
    (`a-{before-write,after-write,done}`, plus `b-*` in race mode) and names
    the missing ones.
  - A missing `Fake[1]_include.cmake` fails with `CMake <version>: generated
    discovery include layout changed ...; extend this probe` instead of a
    bare `FileNotFoundError`.
  - Defaults chosen: the marker check counts as part of criterion 2 (it makes
    3.28/3.31 stricter, never weaker). CMake 4.4 support is not added here
    (see below).
- Verification (logs in `tasks/evidence/BUG-249/impl-*.log`):
  - Before, unchanged test at `b353492b8`, host 4.3.2: `FAILED (failures=3)`,
    `0 != 2 : CMake flush implementation changed` (`impl-before-4.3.2.log`).
  - After, `python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v`:
    `OK` (3 tests) on 4.3.2 (host), 3.28.3, 3.31.6, 3.31.10, 4.0.3, 4.1.3,
    4.2.0, 4.2.3 and 4.3.4 (`env PATH=/tmp/bug249-cmake/<v>/cmake/data/bin:$PATH`).
  - 4.4.4: `FAILED (failures=3)`, all three with the layout message naming
    4.4.4. A disposable 4.3.4 copy with the REMOVE replaced (`unknown-form`):
    `FAILED (failures=3)`, all three with the unrecognised-form message naming
    4.3.4 and the module path. Both are hard failures, not skips.
  - Control multiplicity (diagnostic copy that prints it) on 4.3.2, 3.28.3,
    3.31.6, 4.0.3 and 4.2.0: 138 names once, 463 twice. That is the same
    duplicate-append signature on all three forms.
  - Removal checks on `/tmp` copies of the edited test (`--helper` pointing to
    the worktree's `cmake/IntrinsicTestDiscovery.cmake`). Each mutation was
    run with the marker check (C) and without it:
    - m1, drop the 4.2+ before/after hooks (4.3.2): C `FAILED (failures=3)`;
      without the check `OK`.
    - m2, drop the 3.28/3.31 before/after hooks (3.28.3, 3.31.6): C
      `FAILED (failures=3)`; without the check `OK`.
    - m3, drop the done hook (4.3.2, 3.28.3, 3.31.6): C `FAILED (failures=3)`;
      without the check `OK`.
    - Each failure reads `instrumented discovery hooks did not run: [...]`.
  - `Test.GTestDiscoveryTimeout.py` OK (19), `Test.TestBuildAggregates.py`
    OK (8), `Test_CiPrerequisiteGuards.py` OK (7), all on the host 4.3.2.
  - The host module sha256 is still `6d10cb916be1...`, and
    `git diff --stat b353492b8 -- cmake/` is empty.
  - jscpd on `tests/regression/tooling`: 106 clones, none in this file.
- Remains:
  - CMake 4.4 support is not covered by this task. 4.4.4 `GoogleTest.cmake`
    names the PRE_TEST include `<target>_<sha8>_include.cmake` (which
    includes `_discovery.cmake`, which writes `_tests.cmake`).
    `[1]_include.cmake` is also hard-coded in
    `tools/ci/gtest_discovery_probe.py:230,311` and
    `tests/regression/tooling/Test.GTestDiscoveryTimeout.py:144`. Those were
    not run on 4.4; this is inferred from source.
  - The production lock (`cmake/IntrinsicTestDiscovery.cmake`) wraps whatever
    `TEST_INCLUDE_FILES` holds and does not depend on the include name. This
    is also inferred from source.
  - A backlog follow-up for 4.4 layout support is proposed to the operator,
    not filed (bug-ID allocation and the bug index belong to the integrator).
  - The `ci-docs` remote run (ubuntu-24.04, CMake 3.31.6) was not repeated;
    the local 3.31.6 run covers that version.
