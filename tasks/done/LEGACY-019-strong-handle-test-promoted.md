---
id: LEGACY-019
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-019 — Migrate StrongHandle test to promoted Core

## Goal
- [x] Move the legacy `Test_CoreHandle.cpp` strong-handle coverage to
      `Extrinsic.Core.StrongHandle`, use the new test filename convention, and
      reduce the `LEGACY-005` Core test-consumer set by one file.

## Non-goals
- Do not change `Extrinsic.Core.StrongHandle` behavior.
- Do not delete `src/legacy/Core/`.
- Do not migrate unrelated Core tests in this slice.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_CoreHandle.cpp` imports bare legacy `Core` only to test
  `Core::StrongHandle`.
- The promoted replacement is `Extrinsic.Core.StrongHandle`. Its exported
  hasher is `StrongHandleHash<Tag>` rather than a `std::hash` specialization
  visible across module boundaries.
- `tests/unit/core/Test.Core.StrongHandleLegacy.cpp` already carries smaller
  promoted wrapper coverage; this slice replaces it with the fuller migrated
  test and removes the legacy-suffixed filename.

## Plan
- Rename `Test_CoreHandle.cpp` to `Test.CoreStrongHandle.cpp`.
- Import `Extrinsic.Core.StrongHandle` and use the `Extrinsic::Core` namespace.
- Replace unordered container `std::hash` assumptions with
  `StrongHandleHash<Tag>`.
- Remove the smaller duplicate promoted wrapper test.
- Update `tests/CMakeLists.txt`, migration docs, and task notes with the
  reduced Core test-consumer count.

## Required changes
- [x] Rename and migrate the full strong-handle unit test to promoted Core.
- [x] Remove `Test.Core.StrongHandleLegacy.cpp`.
- [x] Update `tests/CMakeLists.txt` source lists.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.
- [x] Update historical test-taxonomy notes for the promoted filename.

## Tests
- [x] Build the affected core test targets.
- [x] Run the focused `StrongHandle` CTest filter.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Update the HARDEN-041 taxonomy move note for the renamed promoted test.

## Acceptance criteria
- [x] No `tests/unit/core/Test_CoreHandle.cpp` legacy consumer remains.
- [x] `Test.CoreStrongHandle.cpp` imports promoted
      `Extrinsic.Core.StrongHandle`, not legacy `Core`.
- [x] The touched independent test uses the `Test.<Name>.cpp` naming
      convention.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'StrongHandle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this StrongHandle cleanup with broader Core test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `tests/unit/core/Test.CoreStrongHandle.cpp` now carries the full
  strong-handle coverage against `Extrinsic.Core.StrongHandle` and the exported
  `StrongHandleHash` hasher. The smaller legacy-suffixed promoted wrapper test
  is deleted as duplicate coverage. The broader `LEGACY-005` Core gate now
  records 133 legacy-internal consumers and 39 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'StrongHandle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 16/16 tests.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 39 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
