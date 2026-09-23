---
id: BUG-214
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive regression-fixture repair with reproduced CI failure and two-version execution evidence
contract_schema: 1
contracts: []
contract_review: Private CMake test instrumentation only; production discovery locking and engine behavior remain unchanged.
---
# BUG-214 — Support the CI CMake discovery implementation in the race probe

## Goal

Repair the pre-existing CMake-version assumption exposed by
[PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044). The existing
concurrent discovery regression recognizes the 3.28 flush macro; the hosted
3.31.6 module inlines two write sites instead. Reuse the same barriers and
three end-to-end assertions for both implementations without changing the
production lock or editing installed CMake modules.

## Acceptance criteria

- [x] Reproduce all three failures with the hosted runner's CMake 3.31.6.
- [x] Instrument the initial WRITE and final completion for both known module
  forms; unexpected write counts remain hard failures.
- [x] Pass the unlocked corruption control, guarded registry/nested CTest and
  genuine-duplicate preservation cases on 3.28.3 and 3.31.6.
- [x] Run the remaining test-build aggregate regressions and retain evidence.

## Verification

```bash
python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py
env PATH=<isolated-cmake-3.31.6>/bin:$PATH python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py
python3 tests/regression/tooling/Test.TestBuildAggregates.py
python3 tests/regression/tooling/Test_CiPrerequisiteGuards.py
```

The isolated diagnostic executable comes from the official Kitware release
archive, verified against its SHA-256 file. This tests a second CMake version
without replacing the host toolchain; it runs no engine compiler.

## Completion

Retired 2026-09-23 at the test-tooling maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing CMake discovery-probe repair commit.

The isolated 3.31.6 binary reproduced all three original failures. The final
probe passes all three cases on 3.28.3 and 3.31.6, including intentional unlocked
corruption, serialized discovery with nested CTest, and genuine duplicates.
Aggregate (8) and prerequisite (7) regressions pass. Actual Opus 5.5 approved the
[final routing delta](../evidence/BUG-214/claude-final-review.md). Both the private
write-site instrumentation and the generated fixture include now bind the
executed probe; neither installed CMake nor production discovery locking changes.
The [versioned archive binding](../evidence/BUG-214/cmake-source.json) and the
before/intermediate/after logs preserve the diagnosis. Remote CI reruns after push.
