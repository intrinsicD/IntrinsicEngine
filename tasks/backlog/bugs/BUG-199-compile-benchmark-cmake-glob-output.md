---
id: BUG-199
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive harness repair; fixed diff, regression test and retained failed measurement provide evidence.
contract_schema: 1
contracts: []
contract_review: Existing compile-measurement bookkeeping only; no new subsystem contract, schema or benchmark acceptance rule.
---
# BUG-199 — Recognize CMake glob-check metadata in compile measurements

## Goal
Permit CMakeFiles/cmake.verify_globs bookkeeping outside a Ninja target DAG while still rejecting unexplained compilation outputs.

## Context
Observed during RUNTIME-267's first no-op measurement, after untimed baseline compilation. The helper rejected the absolute CMakeFiles/cmake.verify_globs output; no config probe or accepted result existed. Preserve the failed attempt alongside the corrected population. One writer on codex/editor-compile-locality.

## Acceptance criteria
- [x] Recognize the exact CMakeFiles/cmake.verify_globs path suffix, absolute or relative; retain it in unmapped_meta_outputs and keep full build wall timing.
- [x] Regression-test both spellings and reject a similarly named unrelated output.
- [ ] Complete the corrected matched measurement and Claude review, retaining the rejected attempt.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
```
26 tests pass. The accounting helper is shared with the canonical compile runner; do not fork it into a task-local implementation.
