# HARDEN-051B — Final Post-Reorganization Hardening Audit

## Goal
Execute the final hardening audit with command-backed evidence for every acceptance item in `HARDEN-001`.

## Non-goals
- No new engine/runtime/graphics features.
- No broad refactors to force an audit pass.
- Do not mark `HARDEN-051` done without complete configure/build/test/validator evidence.

## Required changes
- Re-run hardening validators in strict mode and record exact outcomes.
- Re-run CI preset configure gate evidence and record pass/fail.
- Update hardening tracker status and blockers from fresh command outcomes.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/check_stale_src` + `_new_references.py --root . --strict`
- `python3 tools/agents/check_codex_config.py --root . --strict`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict`
- `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`

## Docs
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` with latest HARDEN-051 evidence and blocker status.
- Add follow-up task for unresolved blocker if closure evidence is still incomplete.

## Acceptance criteria
- [x] Final audit artifact exists at `tasks/active/final-post-reorganization-hardening-audit.md`.
- [x] Every `HARDEN-001` final acceptance item has explicit command evidence and pass/fail/blocked status.
- [x] HARDEN-051 remains not-done when closure evidence is incomplete.

## Verification
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Final acceptance evidence (executed 2026-04-30 UTC)

| Acceptance item (from HARDEN-001) | Status | Evidence |
|---|---|---|
| Default local/CI CPU-supported test gate is green. | ❌ blocked | `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` failed during configure because required offline cache path `external/cache/glm-src` is absent. Configure did not complete, so build and CPU gate `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` could not run. |
| GPU/runtime tests are fixed or capability-gated with explicit skip reasons. | ❌ blocked | Runtime/GPU execution evidence depends on successful `build/ci` generation; configure failure above prevents runtime gate execution in this environment. |
| Codex verification builds meaningful targets and runs tests. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed (`findings=0`). |
| Active migration-era source-root alias references are removed or policy-managed. | ✅ pass | strict stale-reference checker passed (`findings=0`) after updating contract runtime layering test file paths to promoted source directories. |
| Test taxonomy is strict and checked. | ✅ pass | `python3 tools/repo/check_test_layout.py --root . --strict` passed (`findings=0`). |
| Layering allowlist quality gate is strict and clean. | ✅ pass | `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict` passed (`Entries: 81`, `No findings`) after installing missing `PyYAML`. |
| CI workflows and docs agree on developer/agent verification commands. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed and `python3 tools/docs/check_doc_links.py --root . --strict` passed (`105` links checked). |
| Docs links pass strict mode. | ✅ pass | `python3 tools/docs/check_doc_links.py --root . --strict` passed with no broken relative links. |
| Task policy passes strict mode. | ✅ pass | `python3 tools/agents/check_task_policy.py --root . --strict` passed (`20` task files validated, `0` findings). |
| Method and benchmark validators pass strict mode. | ✅ pass | `python3 tools/agents/validate_method_manifests.py --root methods --strict` passed (`2` files); `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict` passed (`1` file). |
| Final audit artifact exists and records command evidence. | ✅ pass | This file captures per-item command outcomes and closure blockers. |

## Follow-up task
- `HARDEN-052 — Bootstrap offline dependency cache for CI preset` at `tasks/active/HARDEN-052-offline-dependency-cache-bootstrap.md`.
