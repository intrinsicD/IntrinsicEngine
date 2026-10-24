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
