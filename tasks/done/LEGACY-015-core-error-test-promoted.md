---
id: LEGACY-015
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-015 — Migrate CoreError test to promoted Core

## Goal
- [x] Migrate the CoreError unit test from legacy `Core.Error` to promoted
      `Extrinsic.Core.Error`, reducing the `LEGACY-005` Core test-consumer set
      without changing tested behavior.

## Non-goals
- Do not delete any `src/legacy/` subtree.
- Do not migrate unrelated Core test files.
- Do not add compatibility re-exports from promoted Core modules to legacy
  names.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `LEGACY-005` cannot retire while tests import bare legacy `Core.*` modules.
- `Test_CoreError.cpp` maps directly to retained promoted behavior in
  `src/core/Core.Error.cppm`, so it can migrate independently.

## Plan
- Rename the touched independent test to the new `Test.<Name>.cpp` convention.
- Replace `import Core.Error;` with `import Extrinsic.Core.Error;`.
- Update legacy API spellings to the promoted `Extrinsic::Core` surface.
- Update `tests/CMakeLists.txt` for the renamed source and promoted module
  provider.
- Refresh migration/task counts and retire this slice after focused
  verification.

## Required changes
- [x] Rename `tests/unit/core/Test_CoreError.cpp` to
      `tests/unit/core/Test.CoreError.cpp`.
- [x] Migrate the test to `Extrinsic.Core.Error`.
- [x] Update `tests/CMakeLists.txt` for the renamed file and promoted Core
      dependency.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test target.
- [x] Run the focused `CoreError` CTest filter under CPU/null labels.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `Test.CoreError.cpp` imports promoted `Extrinsic.Core.Error`, not legacy
      `Core.Error`.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.
- [x] No compatibility alias or re-export for legacy `Core.*` is introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests
ctest --test-dir build/ci --output-on-failure -R 'CoreError' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this one-test migration with broader Core test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `Test.CoreError.cpp` imports promoted `Extrinsic.Core.Error`; the broader
  `LEGACY-005` Core gate now records 133 legacy-internal consumers and 42 test
  consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreError' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 14/14 tests.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — passed.
- `git diff --check` — passed.
