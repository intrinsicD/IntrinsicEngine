---
id: BUG-252
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: observed by the BUG-251 independent review on CMake 4.0.3 and not reproduced in this filing session; the first run under this task is the next evidence
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
- [ ] Reproduce the failure on CMake 4.0.3 and identify whether CTest, the test's own process
      check, or the lister fixture leaves the lister running.
- [ ] Either the case passes on 4.0.3 with its bounded-timeout and no-orphan checks intact, or
      the supported CMake range in `tests/README.md` excludes 4.0.x with the recorded reason.
- [ ] `Test.GTestDiscoveryTimeout.py` still passes on 3.28.3, 3.31.6, 4.3.2 and 4.4.4.

## Verification
```bash
python3 -m pip install --disable-pip-version-check --target /tmp/bug252-cmake/4.0.3 'cmake==4.0.3'
env PATH=/tmp/bug252-cmake/4.0.3/cmake/data/bin:$PATH cmake --version
env PATH=/tmp/bug252-cmake/4.0.3/cmake/data/bin:$PATH \
  python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
python3 tests/regression/tooling/Test.GTestDiscoveryTimeout.py -v
python3 tools/agents/check_task_policy.py --root . --strict
```
