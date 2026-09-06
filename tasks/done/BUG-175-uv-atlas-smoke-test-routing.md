---
id: BUG-175
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "One explicit manual-producer classification entry; actual build-inventory validation is the direct proof, with no engine behavior or broad evidence report needed."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Corrects the existing CPU test routing inventory for a manually registered benchmark; no new reusable contract or test-selection policy."
---
# BUG-175 — UV-atlas remap smoke is missing from manual producer classification

## Goal
- Classify the existing BUG-159 smoke runner consistently with its manual
  CTest registration so strict CPU gate routing accepts the actual registry.

## Context
- METHOD-040's real-build `Test.TestGateRouting.py --build-dir build/ci
  --aggregate IntrinsicCpuTests` failed because `IntrinsicUvAtlasRemapSmoke`
  has no GoogleTest source ownership and was absent from MANUAL_CTEST_TARGETS.
- `benchmarks/CMakeLists.txt` creates the standalone public-method runner,
  registers its benchmark/regression/geometry producer, and adds its manual
  `IntrinsicUvAtlasRemapSmoke.Run` command. The explicit classification entry
  matches that existing declaration; unknown producers still fail closed.

## Acceptance criteria
- [x] Strict routing validates the actual CPU aggregate with the smoke included.
- [x] Grouped/discovered case parity and unknown-producer rejection remain intact.

## Verification
```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.GroupedCTestParity.py --self-test
```

## Status

- Completed 2026-09-06.
- Commit: `81b855283` (implementation).
- Actual CPU routing validates 28 producers and 4,293 GoogleTest cases;
  grouped/discovered registration parity and parity self-tests pass. The two
  manual CTest records bring the selected physical total to 4,295.
