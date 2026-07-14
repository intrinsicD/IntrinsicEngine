---
id: LEGACY-017
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-017 — Retire duplicate legacy CoreHash test

## Goal
- [x] Remove the duplicate legacy `Test_CoreHash.cpp` consumer while preserving
      promoted `Extrinsic.Core.Hash` coverage, reducing the `LEGACY-005` Core
      test-consumer set without changing tested behavior.

## Non-goals
- Do not delete any `src/legacy/` subtree.
- Do not change hash behavior or assertions.
- Do not migrate unrelated Core test files.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_CoreHash.cpp` imports bare legacy `Core`.
- `tests/unit/core/Test.Core.HashLegacy.cpp` already carries the same CoreHash
  assertions against promoted `Extrinsic.Core.Hash`; the only source
  differences are the import and namespace spellings.

## Plan
- Rename the existing promoted hash test to the new non-legacy filename
  `Test.CoreHash.cpp`.
- Remove the duplicate legacy `Test_CoreHash.cpp` from the old Core test source
  list and filesystem.
- Update `tests/CMakeLists.txt` and historical test-taxonomy notes for the
  renamed promoted source.
- Refresh migration/task counts and retire this slice after focused
  verification.

## Required changes
- [x] Rename `tests/unit/core/Test.Core.HashLegacy.cpp` to
      `tests/unit/core/Test.CoreHash.cpp`.
- [x] Delete duplicate legacy `tests/unit/core/Test_CoreHash.cpp`.
- [x] Update `tests/CMakeLists.txt` for the removed and renamed sources.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test targets.
- [x] Run the focused `CoreHash` CTest filter under CPU/null labels.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Update the HARDEN-041 taxonomy move note for the renamed promoted test.

## Acceptance criteria
- [x] No `tests/unit/core/Test_CoreHash.cpp` legacy consumer remains.
- [x] `Test.CoreHash.cpp` imports promoted `Extrinsic.Core.Hash`, not legacy
      `Core`.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'CoreHash' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this duplicate-test retirement with broader Core test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `Test.CoreHash.cpp` preserves the promoted `Extrinsic.Core.Hash` coverage;
  the duplicate legacy `Test_CoreHash.cpp` consumer is deleted. The broader
  `LEGACY-005` Core gate now records 133 legacy-internal consumers and 40 test
  consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreHash' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 23/23 tests.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — passed.
- `git diff --check` — passed.
