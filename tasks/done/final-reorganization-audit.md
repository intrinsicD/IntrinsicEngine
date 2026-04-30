# RORG-133 — Final Reorganization Audit

## Goal
Confirm the repository now matches the final target configuration and enforcement posture defined by the reorganization backlog.

## Non-goals
- Introducing new engine/runtime/graphics features.
- Performing additional source-tree moves in this audit task.
- Rewriting historical migration records beyond factual completion updates.

## Context
This task is the closing audit gate for the IntrinsicEngine reorganization. It validates that structural migration, policy consolidation, CI split, and strict validation tooling are all in place and synchronized.

## Required changes
- Add this audit task file under `tasks/active/` with explicit completion checklist items.
- Track pass/fail evidence for each final-state requirement.
- Record remaining temporary exceptions (if any) with task IDs and owners.

## Tests
- Run strict task policy validation on `tasks/` metadata/sections.
- Run top-level expected-layout check in strict mode.

## Docs
- Keep this audit task synchronized with `tasks/active/0000-repo-reorganization-tracker.md` final-state status.

## Acceptance criteria
- [x] Root layout matches target.
- [x] `src_new/` removed.
- [x] `src/legacy/` exists and is documented as temporary.
- [x] `src/geometry/` is canonical geometry root.
- [x] `methods/` exists with schema and template.
- [x] `benchmarks/` exists with schema, smoke runner, and docs.
- [x] `tests/` has unit/contract/integration/regression/gpu/benchmark/support.
- [x] `docs/` has index, architecture, adr, methods, benchmarking, agent, migration, api.
- [x] `tasks/` is structured and validated.
- [x] `tools/` is categorized.
- [x] Workflows are split and readable.
- [x] PR template exists.
- [x] AGENTS.md is canonical.
- [x] CLAUDE/Copilot/Codex do not duplicate policy.
- [x] CI strict checks enabled.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_expected_top_level.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/analysis/module_fanout.py --root src --fail-on-regression
cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target IntrinsicTests IntrinsicCoreTests IntrinsicECSTests IntrinsicContractBuildTests IntrinsicBenchmarkTests IntrinsicGeometryTests
```

## Temporary exceptions
- None currently recorded.

## Owner
- Repository maintainers / architecture review rotation.

## Evidence (2026-04-29)
- `python3 tools/agents/check_task_policy.py --root . --strict` passed (0 findings).
- `python3 tools/repo/check_expected_top_level.py --root . --strict` passed after local generated/build artifact directories were excluded from source-layout comparison.
- `python3 tools/repo/check_root_hygiene.py --root . --strict` passed with only allowed root markdown files.
- `python3 tools/docs/check_doc_links.py --root . --strict` passed (103 relative links checked).
- `python3 tools/analysis/module_fanout.py --root src --fail-on-regression` passed.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DINTRINSIC_OFFLINE_DEPS=ON` configured successfully against the populated local dependency cache.
- `cmake --build --preset ci --target IntrinsicTests` passed after moving the remaining shared ImGui test helper into `tests/support/` and replacing a path-sensitive importer test include with the configured graphics include root.
- Categorized non-GPU executable checks passed:
  - `IntrinsicCoreTests`: 265 passed.
  - `IntrinsicECSTests`: 44 passed.
  - `IntrinsicContractBuildTests`: 15 passed.
  - `IntrinsicBenchmarkTests`: 10 passed, 2 skipped SLO checks.
  - `IntrinsicGeometryTests`: 904 passed.
- Full `ctest --test-dir build/ci --output-on-failure` was attempted and interrupted after 300 seconds; it reached GPU/headless Vulkan runtime coverage and exposed pre-existing runtime/GPU failures unrelated to repository layout (`TransferTest.*`/`GraphicsBackendHeadlessTest.*` VMA unmap assertions and two `RuntimeSelection.ResolveGpuSubElementPick_*` expectations). These remain outside the final reorganization structural gate and should be triaged under runtime/GPU follow-up work if full local GPU CTest is required.

## Completion status
- **Status:** done
- **Blocker:** None.
- **Follow-up:** Continue recording any future temporary migration exceptions in `tasks/active/0000-repo-reorganization-tracker.md`.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: Post-reorganization hardening continues in `tasks/active/0001-post-reorganization-hardening-tracker.md`; the resolved GPU/runtime triage record is archived at `tasks/done/HARDEN-053-gpu-runtime-opt-in-failure-triage.md`.

