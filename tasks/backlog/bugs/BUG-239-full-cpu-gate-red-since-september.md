---
id: BUG-239
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive diagnosis; evidence is the CI run logs and the diff
contract_schema: 1
contracts: []
contract_review: CI gate selection and test registration; no catalog contract covers CI routing.
---
# BUG-239 — `ci-linux-clang` `full-cpu` has been red on `main` since 2026-09-11

## Goal
- The `full-cpu` job of `ci-linux-clang` passes on `main` again, with each masked failure
  diagnosed rather than skipped.

## Context
- Last successful `full-cpu` job: 2026-09-11 (`12d61d759`); every `main` run from 2026-10-01 to
  2026-10-08 failed (scan of `gh run list --workflow ci-linux-clang.yml`). Failing steps stop the
  job, so each fix exposes the next failure.
- Peeled so far under BUG-238 (2026-10-08): "Reject known compiler hazards" (`JobFailure.hpp`
  default argument) and "Reconcile CPU test routing" (BUG-106 baseline lacked a 2026-10-01 case).
- Current blocker, run 37773451732 on `2f46f1114`, step "Capture CPU test selection", exit 3:
  `BLOCKED: selected CTest test 'ProcessingCompilationLocality.ConfigPropertyTypes' does not map
  to a registered producer` (`tools/ci/cpu_test_selection.py:606`).
- Likely cause: `intrinsic_add_module_boundary_test` (`tests/CMakeLists.txt:20`, from `688ec68ca`
  2026-09-15, tests since `8a35af54a` 2026-09-14) registers Python-command CTest tests labelled
  `contract;runtime;build;headless`. `_producer_for_command` maps tests only to registered test
  binaries, so none of the 96 compilation-locality script tests has a producer.
  `ProcessingCompilationLocality.ScalarRidges` (`15a4b6cb7`) also had no `LABELS`.
- First failure after the last green job: run 34835799762 on `e1cde3fda` (2026-09-14), "Build
  required CPU cohort": `Test.Graphics.DebugViewSystem.cpp` compared `FrameResourceId` with gtest
  `==` without an `operator==`. Fixed since; the build step passes on 2026-10-08.
- Decision (operator, 2026-10-08, after Codex plan review `01a11b90-ec4d-7421-8adb-9933f8c98500`):
  keep the script tests in the CPU run and record them as their own typed category in the
  selection and timing reports, not as a pseudo-producer and not behind a new excluded label
  (`EXCLUDED_LABELS` is replicated policy and would drop them from `full-cpu`).

## Acceptance criteria
- [ ] Root cause of the first `full-cpu` failure after 2026-09-11 identified with its run id.
- [ ] Script tests from an allowlist (`SCRIPT_TESTS`) are captured as `script_tests` in the CPU
      selection (schema v2, digest, summary, variant compare) and timed by script; any other
      producer-less test still blocks; documented in `tools/ci/README.md`; no gate weakened.
- [ ] `full-cpu` passes on `main`, or every remaining failure has its own BUG task.

## Verification
```bash
gh run list --workflow ci-linux-clang.yml --branch main --limit 5
python3 tools/ci/cpu_test_selection.py --help
```
