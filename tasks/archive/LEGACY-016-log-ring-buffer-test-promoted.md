---
id: LEGACY-016
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-016 — Migrate LogRingBuffer test to promoted Core

## Goal
- [x] Migrate the LogRingBuffer unit test from legacy `Core.Logging` to
      promoted `Extrinsic.Core.Logging`, reducing the `LEGACY-005` Core
      test-consumer set without changing tested behavior.

## Non-goals
- Do not delete any `src/legacy/` subtree.
- Do not migrate unrelated Core test files.
- Do not change logging behavior.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `LEGACY-005` remains blocked by tests importing bare legacy `Core.*` modules.
- `Test_LogRingBuffer.cpp` only exercises the retained promoted logging ring
  buffer contract and can migrate independently after `LEGACY-015` added the
  promoted Core dependency to `CoreTestObjs`.

## Plan
- Rename the touched independent test to the new `Test.<Name>.cpp` convention.
- Replace `import Core.Logging;` with `import Extrinsic.Core.Logging;`.
- Keep the existing `Core::Log` namespace spelling through a namespace alias to
  minimize test churn.
- Update `tests/CMakeLists.txt` for the renamed source.
- Refresh migration/task counts and retire this slice after focused
  verification.

## Required changes
- [x] Rename `tests/unit/core/Test_LogRingBuffer.cpp` to
      `tests/unit/core/Test.LogRingBuffer.cpp`.
- [x] Migrate the test to `Extrinsic.Core.Logging`.
- [x] Update `tests/CMakeLists.txt` for the renamed file.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test target.
- [x] Run the focused `LogRingBuffer` CTest filter under CPU/null labels.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `Test.LogRingBuffer.cpp` imports promoted `Extrinsic.Core.Logging`, not
      legacy `Core.Logging`.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.
- [x] No compatibility alias or re-export for legacy `Core.*` is introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests
ctest --test-dir build/ci --output-on-failure -R 'LogRingBuffer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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
- `Test.LogRingBuffer.cpp` imports promoted `Extrinsic.Core.Logging`; the
  broader `LEGACY-005` Core gate now records 133 legacy-internal consumers and
  41 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'LogRingBuffer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 9/9 tests.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — passed.
- `git diff --check` — passed.
