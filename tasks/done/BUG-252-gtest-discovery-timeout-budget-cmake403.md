---
id: BUG-252
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: test-only change verified by the regression scripts across 10 CMake versions; no results-bearing run
contract_schema: 1
contracts: []
contract_review: Private CMake test instrumentation regression only; the catalog has no CTest discovery contract, and engine behavior stays unchanged.
---
# BUG-252 — GTestDiscoveryTimeout budget test fails on CMake 4.0.3

## Goal
- Explain why `Test.GTestDiscoveryTimeout.py`'s budget case fails on CMake 4.0.3, and either
  make it pass there or record 4.0.3 as unsupported with the reason, without weakening what
  the case checks on the supported versions.

## Context
- Observed by the independent Claude review of BUG-251 (`cca518f55`, 2026-10-24, relayed by
  the coordinator; the record is not on disk):
  `test_declared_budget_passes_below_and_fails_bounded_above` fails with "timed-out lister is
  still running" on CMake 4.0.3. It also fails at the base `4c30f2b82`, before BUG-251; the
  reviewer ran it twice from a `git archive`. So BUG-251 did not cause it.
- 4.0.3 is not in the supported matrix: BUG-251 checked 3.28.3, 3.31.6, 4.3.2 and 4.4.4 (the
  review also 4.2.3 and 4.3.4). Hosted CI uses CMake 3.31.6, so no CI job is red.
- Not checked: other 4.0.x/4.1.x versions, and whether CTest 4.0.3 kills the timed-out
  listing's process tree differently.

## Acceptance criteria
- [x] Reproduce the failure on CMake 4.0.3 and identify whether CTest, the test's own process
      check, or the lister fixture leaves the lister running.
- [x] Either the case passes on 4.0.3 with its bounded-timeout and no-orphan checks intact, or
      the supported CMake range in `tests/README.md` excludes 4.0.x with the recorded reason.
- [x] `Test.GTestDiscoveryTimeout.py` still passes on 3.28.3, 3.31.6, 4.3.2 and 4.4.4.

## Verification
```bash
python3 -m pip install --disable-pip-version-check --target /tmp/bug252-cmake/4.0.3 'cmake==4.0.3'
env PATH=/tmp/bug252-cmake/4.0.3/cmake/data/bin:$PATH cmake --version
env PATH=/tmp/bug252-cmake/4.0.3/cmake/data/bin:$PATH \
  python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
python3 tools/agents/check_task_policy.py --root . --strict
```

## Prediction (written before the first run, 2026-10-24)
- On CMake 4.0.3 the budget case fails at the no-orphan check only: CTest returns bounded at
  the budget with the repository diagnostic, but the timed-out lister survives, as on 3.31
  (whose orphan the case already tolerates and reaps). 4.1.3 is expected to share it if the
  kill arrived later than 4.1; unknown.

## Resolution
- Reproduced: on CMake 4.0.3 the budget case failed only at "timed-out lister is still
  running" (prediction held); 4.1.3 passed. A `ps --forest` probe showed the lister as the
  direct child of `ctest`; after CTest 4.0.3 returned (rc 8, bounded) the lister was
  reparented and still sleeping, on 4.1.3 it was gone. So CTest leaves it, not the test's
  process check or the fixture.
- Cause (upstream, read from source): CMake 3.29 moved `execute_process` to
  `cmUVProcessChain` (commit 5420639a8d), which stopped terminating the child on `TIMEOUT`.
  CMake issue 27378 restored it (`chain.Terminate()` after a timeout; commit 22e84c7e7f,
  in 4.0.5 and 4.1.3, and e2cfc61e5e in 3.31.10); 4.2.0 and
  later have it. 3.28.x still uses `cmsysProcess` and kills. Orphaning range: 3.29.0-3.31.9,
  4.0.0-4.0.4, 4.1.0-4.1.2. The case's old `(3, 31)` major.minor exemption missed 4.0.x.
- Fix: `lister_survives_timeout(version)` encodes that range, and the case now asserts the
  lister state exactly (dead outside the range, still running inside it, then reaped by the
  existing cleanup), so a wrong range fails either way. A boundary unit case pins the range.
  `tests/README.md` states the range instead of single versions.
- Not run: 3.29.x, 3.30.x, 4.0.4/4.0.5, 4.1.0-4.1.2 (range from source and upstream tags only).
- Verification 2026-10-24 (full `Test.GTestDiscoveryTimeout.py`, 24 tests, and
  `Test.ConcurrentCTestDiscovery.py`, 3 tests; `cmake`/`ctest --version` checked per row):

  | CMake | GTestDiscoveryTimeout | orphan reaped | ConcurrentCTestDiscovery |
  |---|---|---|---|
  | 3.28.3 | OK | no | OK |
  | 3.31.6 | OK | yes | OK |
  | 3.31.10 | OK | no | OK |
  | 4.0.3 | OK | yes | OK |
  | 4.1.3 | OK | no | OK |
  | 4.2.0 | OK | no | OK |
  | 4.2.3 | OK | no | OK |
  | 4.3.2 (host) | OK | no | OK |
  | 4.3.4 | OK | no | OK |
  | 4.4.4 | OK | no | OK |

- Mutation checks: marking 4.1.3 as orphaning fails the live case on 4.1.3 ("should leave
  the timed-out lister running") and the boundary case; dropping the 4.0 clause fails the
  live case on 4.0.3 ("timed-out lister is still running") and the boundary case.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `548a653b5` on the night integration branch
`claude/night-2026-10-24-bug-sweep`. Not yet on `main`; hosted CI has not run on it.

Reviewer decisions: independent Claude review of `548a653b5`, 2026-10-24, relayed verbatim by
the coordinator; the record is not on disk. Verdict approve, with criterion 1 "met. The
`ps --forest` probe in the note, the source diff and my 30 s timing on 4.0.3 all point to
CTest."; criterion 2 "met. The no-orphan check now asserts the exact state per version, which
is stricter than before."; criterion 3 "met. I have now run it myself on all four versions in
this review session." (3.28.3, 3.31.6, 4.3.2, 4.4.4; both scripts passed). It confirmed the
version range from upstream `cmExecuteProcessCommand.cxx` at each tag (3.29.0-3.31.9,
4.0.0-4.0.4 and 4.1.0-4.1.2 lack `chain.Terminate()`; 3.31.10, 4.0.5, 4.1.3 and 4.2.0 have it).
Its one required fix, the stale front-matter `evidence_skip_reason`, is applied in the
retirement commit. All three boxes are ticked from these decisions.

Limits:
- 3.29.x, 3.30.x, 4.0.4, 4.0.5 and 4.1.0-4.1.2 were not run; the range comes from source and
  upstream tags only.
- The upstream issue page 27378 was not opened by the reviewer.

Evidence: no files under `tasks/evidence/BUG-252/`; nothing to delete.
