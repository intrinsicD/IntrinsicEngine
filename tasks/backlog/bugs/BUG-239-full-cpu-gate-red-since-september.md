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
  binaries, so none of the eight `ProcessingCompilationLocality.*` tests has a producer. Not yet
  verified which commit first broke `full-cpu` between 2026-09-11 and 2026-09-14.
- Decision to make: whether these script tests belong to the CPU aggregate (register a producer
  for them) or to a separate build-graph lane (label them out of the CPU selection), per the
  CPU selection contract in `docs/agent/contract.md` §Testing and verification protocol.

## Acceptance criteria
- [ ] Root cause of the first `full-cpu` failure after 2026-09-11 identified with its run id.
- [ ] `ProcessingCompilationLocality.*` are either selected through a registered producer or
      excluded by an explicit, documented lane; no gate weakened.
- [ ] `full-cpu` passes on `main`, or every remaining failure has its own BUG task.

## Verification
```bash
gh run list --workflow ci-linux-clang.yml --branch main --limit 5
python3 tools/ci/cpu_test_selection.py --help
```
