---
id: LEGACY-023
theme: F
depends_on: [LEGACY-012, CORE-002, RUNTIME-102]
---
# LEGACY-023 — Retire legacy Core.Commands test

## Goal
- [x] Remove the legacy-only `Core.Commands` unit test from the `LEGACY-005`
      external Core consumer set after `CORE-002` and `RUNTIME-102` established
      the promoted runtime/editor command-history endpoint.

## Non-goals
- Do not recreate `Core.Commands` under promoted `src/core`.
- Do not change `Extrinsic.Runtime.EditorCommandHistory` behavior.
- Do not migrate unrelated Core tests.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `CORE-002` explicitly retired the legacy global `Core.Commands` core service:
  command history, dirty state, ECS mutation policy, and undo/redo behavior are
  runtime/editor responsibilities.
- `RUNTIME-102` already promoted `Extrinsic.Runtime.EditorCommandHistory` with
  command execute/record/undo/redo, bounded history, dirty-state snapshots,
  transform/selection/visualization/primitive adapters, compound rollback, and
  hierarchy-delete planning coverage.

## Plan
- Remove `tests/unit/core/Test_CoreCommands.cpp`.
- Remove the deleted source from the legacy-linked core test object list.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Delete the legacy-only `Core.Commands` unit test.
- [x] Update `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test target.
- [x] Run the promoted `EditorCommandHistory` CTest filter to confirm the
      runtime-owned endpoint remains covered.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/core/Test_CoreCommands.cpp` no longer exists.
- [x] No promoted `core` replacement for the legacy global command service is
      introduced.
- [x] Promoted runtime command-history coverage remains in place.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|CoreCommands' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Reintroducing legacy command/catalog behavior in promoted `core`.
- Editing runtime command-history code in this cleanup slice.
- Mixing this retirement with broader Core test migrations.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only core command
  coverage and existing promoted runtime command-history contracts.
- No `Operational` follow-up is owed for the retired `core` service.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 34 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|CoreCommands' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 6/6 promoted runtime command-history tests; no `CoreCommands`
  tests remained after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 34 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
