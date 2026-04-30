# HARDEN-051B — Final Post-Reorganization Hardening Audit

## Goal
Execute the final hardening audit with command-backed evidence for every acceptance item in `HARDEN-001`.

## Non-goals
- Do not introduce new engine/runtime/graphics features.
- Do not perform broad refactors to force audit pass status.
- Do not mark HARDEN-051 done without complete build/test/validator evidence.

## Context
`HARDEN-051` required an execution artifact at `tasks/active/final-post-reorganization-hardening-audit.md`. This file is that executed audit, with exact command outcomes and closure status.

## Required changes
- Create this audit artifact under `tasks/active/`.
- Run/record evidence for each final acceptance item in `HARDEN-001`.
- Record closure blockers explicitly when evidence is incomplete.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/check_stale_src_new_references.py --root . --strict`
- `python3 tools/agents/check_codex_config.py --root . --strict`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict`
- `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`

## Docs
- Synchronize status/evidence references in `tasks/active/0001-post-reorganization-hardening-tracker.md`.

## Acceptance criteria
- [x] Final audit artifact exists at `tasks/active/final-post-reorganization-hardening-audit.md`.
- [x] Every `HARDEN-001` final acceptance item has explicit command evidence and pass/fail/blocked status.
- [x] HARDEN-051 remains not-done when closure evidence is incomplete.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Evidence execution (2026-04-29 UTC)

| Acceptance item (from HARDEN-001) | Status | Evidence |
|---|---|---|
| Default local/CI CPU-supported test gate is green. | ❌ blocked | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` failed due missing offline dependency cache path `external/cache/glm-src`, so configure/build/test gate could not be executed in this environment. |
| GPU/runtime tests are fixed or capability-gated with explicit skip reasons. | ❌ not verified | Full/runtime-targeted test execution was not possible because `build/ci` configure did not complete; no fresh execution evidence can be produced in this run. |
| Codex verification builds meaningful targets and runs tests. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed (`findings=0`). |
| Active `src_new` naming is gone or allowlisted as historical/migration-only. | ❌ fail | `python3 tools/repo/check_stale_src_new_references.py --root . --strict` reported 14 findings in `tests/contract/runtime/Test_RuntimeEngineLayering.cpp` (lines 28, 48, 91, 101-111). |
| Test taxonomy is strict and checked. | ✅ pass | `python3 tools/repo/check_test_layout.py --root . --strict` passed (`findings=0`). |
| Layering allowlist is no longer migration-wide except where explicitly justified. | ✅ pass | `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict` passed (`Entries: 81`, `No findings`). |
| CI workflows and docs agree on developer and agent verification commands. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed (`findings=0`) and `python3 tools/docs/check_doc_links.py --root . --strict` passed (`105` links checked). |
| Docs links pass strict mode. | ✅ pass | `python3 tools/docs/check_doc_links.py --root . --strict` passed. |
| Task policy passes strict mode. | ✅ pass | `python3 tools/agents/check_task_policy.py --root . --strict` passed (`18` task files validated, `0` findings) before adding this audit file, then strict-green again after synchronization. |
| Method and benchmark validators pass strict mode. | ✅ pass | `python3 tools/agents/validate_method_manifests.py --root methods --strict` passed (`2` files) and `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict` passed (`1` file). |
| `tasks/active/final-post-reorganization-hardening-audit.md` records final evidence for HARDEN-051. | ✅ pass | This artifact records per-item command evidence and outcomes. |

## Completion status
- **HARDEN-051:** in-progress (not done).
- **Blocking evidence gaps:**
  1. `check_stale_src_new_references` strict failures must be remediated or policy-allowlisted.
  2. `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` must succeed in a cache-populated environment, followed by build/test gate evidence.
