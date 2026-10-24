---
id: BUG-251
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: test-harness portability follow-up split from BUG-249; its 4.4.4 failure log is the reproduction, and the first run on this task's own change is the next evidence
contract_schema: 1
contracts: []
contract_review: Private CMake test instrumentation and a CI diagnostic only; the catalog has no CTest discovery contract, and engine behavior stays unchanged.
---
# BUG-251 — Support the CMake 4.4 GoogleTest discovery include layout

## Goal
- The discovery tooling that hard-codes CMake's `<target>[1]_include.cmake` name finds the
  generated include on CMake 4.4 as well. Each tool keeps what it checks today and keeps an
  unknown layout a hard failure.

## Context
- Split from [BUG-249](../../done/BUG-249-concurrent-ctest-discovery-cmake4.md) (retired
  2026-10-24), which made
  `tests/regression/tooling/Test.ConcurrentCTestDiscovery.py` support the CMake 4.2+
  REMOVE+APPEND write form and left 4.4 out of its scope. BUG-249's independent Claude review
  asked for this follow-up to be filed before retirement.
- Observed by the BUG-249 lane (`impl-after-4.4.4.log`, git-ignored in the `bug-149` lane
  worktree under `tasks/evidence/BUG-249/`): with the CMake 4.4.4 PyPI wheel on `PATH`,
  `Test.ConcurrentCTestDiscovery.py` gives `FAILED (failures=3)`. Every case fails with
  `CMake 4.4.4: generated discovery include layout changed, no Fake[1]_include.cmake; extend
  this probe`.
- Cause, read from the 4.4.4 module source (`share/cmake-4.4/Modules/`):
  - `GoogleTest.cmake:662-674` names the PRE_TEST files from
    `MAKE_C_IDENTIFIER("<target>_<sha8>")`, where `<sha8>` is the first 8 hex digits of the
    SHA-256 of the call's arguments. That gives `<target>_<sha8>_include.cmake`, which includes
    a new `<target>_<sha8>_discovery.cmake`, which writes `<target>_<sha8>_tests.cmake`.
    Multi-config generators add `_<CONFIG>`.
  - `GoogleTestAddTests.cmake:5` includes the new `GoogleTest/ParseTestList.cmake`, which holds
    part of the former module.
  - The module include and the `gtest_discover_tests_impl(` call move into `_discovery.cmake`
    (`GoogleTest.cmake:703-725`); `_include.cmake` only includes `_discovery.cmake` and
    `_tests.cmake` (`:727-758`). So finding the renamed file is not enough. Inferred from source,
    not run: `parse_include` requires the impl call in the file it reads
    (`Test.GTestDiscoveryTimeout.py:71-76`, `tools/ci/gtest_discovery_probe.py:41-47`); the
    redirect in `Test.ConcurrentCTestDiscovery.py:140-145` needs exactly one module include in
    that file; `reconcile()` (`Test.GTestDiscoveryTimeout.py:609-616`, the `ci-linux-clang`
    `--build-dir` path) globs `*_include.cmake` and takes the target as `name.split('[')[0]`,
    which on 4.4 is the whole file name. `tests_files()` (`gtest_discovery_probe.py:273`) keys
    `_tests.cmake` mtimes the same way.
  - Lane report, no kept log: the BUG-249 lane's prototype redirect on 4.4 still failed on the
    relative include of `GoogleTest/ParseTestList.cmake` (`GoogleTestAddTests.cmake:5` resolves
    it from `CMAKE_CURRENT_LIST_DIR`). A private module copy therefore also needs the
    `GoogleTest/` directory beside it (the lane's Option B).
- Hard-coded `[1]_include.cmake` sites (the line numbers are at the BUG-249 merge). Only the first
  was run on 4.4; the failure of the others there is inferred from source.
  - `tests/regression/tooling/Test.ConcurrentCTestDiscovery.py:137` (the observed failure).
  - `tests/regression/tooling/Test.GTestDiscoveryTimeout.py:144` (`include()`), together with
    the `[1]_tests.cmake` name at `:147` (`stale()`). `:401` and `:583` write synthetic trees.
  - `tools/ci/gtest_discovery_probe.py:230` (`cmd_direct`) and `:311` (`ctest_once`). Neither
    line is in the BUG-091 `decide`/`report` shortcut.
- Production discovery is likely unaffected, but this is inferred from source and was not run:
  `cmake/IntrinsicTestDiscovery.cmake` wraps whatever `TEST_INCLUDE_FILES` holds, and 4.4.4
  `GoogleTest.cmake:771-773` still appends to that property. Its failed-listing watch depends
  on the module's `result` variable, which `GoogleTestAddTests.cmake:222` still sets.
- CI: `ci-docs.yml` runs `Test.ConcurrentCTestDiscovery.py` and `Test.GTestDiscoveryTimeout.py`,
  and `ci-linux-clang.yml` runs the latter with `--build-dir build/ci`. The hosted image listed
  CMake 3.31.6 at BUG-249's filing. A runner image with CMake 4.4 would turn these jobs red.
- Open choice (operator), carried over from BUG-249: until this lands, CMake 4.4 is a hard
  failure without race coverage. Whether to support 4.4 now, or to pin CMake in CI first,
  is the operator's decision.

## Acceptance criteria
- [ ] With CMake 4.4.4, `Test.ConcurrentCTestDiscovery.py` runs its three cases and they pass.
      The unlocked control still shows duplicated registrations, and dropping any
      instrumentation hook fails the marker check. An unrecognised layout or write form still
      fails at once with a message naming the CMake version.
- [ ] With CMake 4.4.4, `Test.GTestDiscoveryTimeout.py` passes and still checks what it checks
      on 4.3.2, or it fails at once with a message naming the version and the layout. A live
      `--build-dir` run on a 4.4 configure is recorded, or recorded as not run. Either way,
      `reconcile()` reads a 4.4-layout tree (a synthetic one is enough) with the right target
      names and the discovery call's fields, and still rejects an unknown layout.
- [ ] `tools/ci/gtest_discovery_probe.py` finds a target's generated discovery call on both
      layouts (in `_include.cmake` before 4.4, in the `_discovery.cmake` it includes on 4.4) and
      parses it in `cmd_direct` and `ctest_once`, without guessing between several matching
      files. An unknown layout still raises.
- [ ] The three tools still pass on CMake 3.28.3, 3.31.6 and the host's 4.3.2. Installed CMake
      modules and `cmake/IntrinsicTestDiscovery.cmake` stay unchanged, unless a 4.4 run shows
      that the production lock or watch is affected; any such change is recorded with that run.

## Verification
```bash
cmake --version
python3 -m pip install --disable-pip-version-check --target /tmp/bug251-cmake/4.4.4 'cmake==4.4.4'
python3 -m pip install --disable-pip-version-check --target /tmp/bug251-cmake/3.28.3 'cmake==3.28.3'
python3 -m pip install --disable-pip-version-check --target /tmp/bug251-cmake/3.31.6 'cmake==3.31.6'
for v in 4.4.4 3.28.3 3.31.6; do
  env PATH=/tmp/bug251-cmake/$v/cmake/data/bin:$PATH python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
  env PATH=/tmp/bug251-cmake/$v/cmake/data/bin:$PATH python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
done
python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
python3 tools/agents/check_task_policy.py --root . --strict
```
