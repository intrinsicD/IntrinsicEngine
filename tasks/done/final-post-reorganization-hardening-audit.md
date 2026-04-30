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
- Update `tasks/done/0001-post-reorganization-hardening-tracker.md` with latest HARDEN-051 evidence and blocker status.
- Add follow-up task for unresolved blocker if closure evidence is still incomplete.

## Acceptance criteria
- [x] Final audit artifact exists at `tasks/done/final-post-reorganization-hardening-audit.md`.
- [x] Every `HARDEN-001` final acceptance item has explicit command evidence and pass/fail/blocked status.
- [x] Unresolved GPU/runtime closure work has an explicit follow-up task.

## Verification
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Final acceptance evidence (executed 2026-04-30 UTC)

| Acceptance item (from HARDEN-001) | Status | Evidence |
|---|---|---|
| Default local/CI CPU-supported test gate is green. | ✅ pass | HARDEN-052 cache bootstrap unblocked the offline CI preset locally with explicit toolchain/scanner overrides: `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps` passed; `cmake --build --preset ci --target IntrinsicTests` passed; `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed with `100% tests passed, 0 tests failed out of 1432` in 96.09 sec, with 2 benchmark/SLO tests skipped by test-internal conditions. |
| GPU/runtime tests are fixed or capability-gated with explicit skip reasons. | ❌ blocked | `ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60` executed on 2026-04-30 and failed: `97% tests passed, 38 tests failed out of 1218` in 249.43 sec. Representative failing groups include graph render data expectations, geometry reuse ASAN lifetime failure, property-set dirty sync, panel registration, render extraction/packet expectations, maintenance lane GPU tests, runtime layering contracts, and null renderer debug dump. Follow-up is tracked by `HARDEN-053`. |
| Codex verification builds meaningful targets and runs tests. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed (`findings=0`). |
| Active migration-era source-root alias references are removed or policy-managed. | ✅ pass | strict stale-reference checker passed (`findings=0`) after updating contract runtime layering test file paths to promoted source directories. |
| Test taxonomy is strict and checked. | ✅ pass | `python3 tools/repo/check_test_layout.py --root . --strict` passed (`findings=0`). |
| Layering allowlist quality gate is strict and clean. | ✅ pass | `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict` passed (`Entries: 81`, `No findings`) after installing missing `PyYAML`. |
| CI workflows and docs agree on developer/agent verification commands. | ✅ pass | `python3 tools/agents/check_codex_config.py --root . --strict` passed and `python3 tools/docs/check_doc_links.py --root . --strict` passed (`105` links checked). |
| Docs links pass strict mode. | ✅ pass | `python3 tools/docs/check_doc_links.py --root . --strict` passed with no broken relative links. |
| Task policy passes strict mode. | ✅ pass | `python3 tools/agents/check_task_policy.py --root . --strict` passed after task archival cleanup (`21` task files validated, `0` findings). |
| Method and benchmark validators pass strict mode. | ✅ pass | `python3 tools/agents/validate_method_manifests.py --root methods --strict` passed (`2` files); `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict` passed (`1` file). |
| Final audit artifact exists and records command evidence. | ✅ pass | This file captures per-item command outcomes and closure blockers. |

## Follow-up task
- `HARDEN-052 — Bootstrap offline dependency cache for CI preset` at `tasks/done/HARDEN-052-offline-dependency-cache-bootstrap.md` is complete for the offline configure/build/CPU-gate blocker.
- `HARDEN-053 — Triage GPU/runtime opt-in gate failures` is archived at `tasks/done/HARDEN-053-gpu-runtime-opt-in-failure-triage.md`; it resolved the remaining GPU/Vulkan/runtime closure blocker.

## Completion metadata
- Completion date: 2026-04-30.
- Commit reference: pending current workspace/PR.
- Follow-up: HARDEN-053 owns the remaining failed GPU/Vulkan/runtime opt-in gate.
