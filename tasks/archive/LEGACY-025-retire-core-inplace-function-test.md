---
id: LEGACY-025
theme: F
depends_on: [LEGACY-012, CORE-002]
---
# LEGACY-025 — Retire legacy Core.InplaceFunction test

## Goal
- [x] Remove the legacy-only `Core.InplaceFunction` unit test from the
      `LEGACY-005` external Core consumer set because this utility has no
      promoted `Extrinsic.Core` endpoint.

## Non-goals
- Do not add a promoted `Extrinsic.Core.InplaceFunction` module.
- Do not migrate legacy runtime, graphics, or RHI consumers in this slice.
- Do not change deferred-destruction or render-pass callback behavior.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- The nonlegacy parity matrix records legacy `Core.InplaceFunction` as
  legacy-only cleanup owned by `LEGACY-012` or subtree-deletion gates, not an
  unnamed promoted feature blocker.
- Remaining `Core.InplaceFunction` imports are legacy-subtree consumers plus
  integration coverage for legacy render extraction; those belong to their
  owning subtree cleanup tasks.

## Plan
- Remove `tests/unit/core/Test_InplaceFunction.cpp`.
- Remove the deleted source from the legacy-linked core test object list.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Delete the legacy-only `InplaceFunction` unit test.
- [x] Update `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test target.
- [x] Confirm the deleted unit-test cases are no longer discovered after the
      rebuild.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/core/Test_InplaceFunction.cpp` no longer exists.
- [x] No promoted `core` InplaceFunction replacement is introduced.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests
ctest --test-dir build/ci -N -R '^InplaceFunction\.'
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
- Reintroducing `InplaceFunction` under promoted `core`.
- Editing legacy runtime/graphics/RHI consumers in this unit-test cleanup.
- Mixing this retirement with unrelated Core test migrations.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only
  `Core.InplaceFunction` unit coverage.
- No `Operational` follow-up is owed for the retired standalone core utility.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 31 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests` — passed.
- `ctest --test-dir build/ci -N -R '^InplaceFunction\.'` — reported
  `Total Tests: 0` after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 31 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
