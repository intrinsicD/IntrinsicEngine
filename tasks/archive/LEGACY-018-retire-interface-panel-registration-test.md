---
id: LEGACY-018
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-018 — Retire legacy Interface panel-registration test

## Goal
- [x] Retire the legacy-only `Interface::GUI` panel-registration contract test
      so the `LEGACY-001` external test-consumer count drops to zero.

## Non-goals
- Do not delete `src/legacy/Interface/`.
- Do not migrate or edit legacy Graphics/Runtime Interface consumers.
- Do not introduce a compatibility re-export for the legacy `Interface` module.

## Context
- Owner/layer: `tests/contract/ui` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/contract/ui/Test_PanelRegistration.cpp` imports legacy `Interface` and
  validates the legacy `Interface::GUI` registration API.
- Promoted current UI behavior is owned by `Extrinsic.Runtime.SandboxEditorUi`
  and covered by runtime/UI contract tests from `UI-008`; the legacy
  panel-registration API is not a promoted endpoint.

## Plan
- Delete the legacy-only test instead of inventing a promoted compatibility API.
- Remove it from `tests/CMakeLists.txt`.
- Update `LEGACY-001`, `LEGACY-012`, and migration docs with the zero external
  Interface test-consumer count.
- Verify the affected runtime test target still builds and that the
  `PanelRegistration` CTest filter no longer selects tests.

## Required changes
- [x] Delete `tests/contract/ui/Test_PanelRegistration.cpp`.
- [x] Remove `Test_PanelRegistration.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Interface external
      test-consumer count.
- [x] Record why the legacy-only coverage is retired rather than migrated.

## Tests
- [x] Build the affected runtime test target.
- [x] Confirm the `PanelRegistration` CTest filter has no remaining tests.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/legacy-retirement.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update `LEGACY-001` and `LEGACY-012` blocker notes.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] No test imports legacy `Interface`.
- [x] `LEGACY-001` records zero external test consumers and remains blocked
      only by legacy-internal Graphics/Runtime consumers.
- [x] No surviving touched test needs a naming-convention rename.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci -N -R 'PanelRegistration'
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this one-test retirement with legacy subtree deletion.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this retires legacy-only test coverage
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `tests/contract/ui/Test_PanelRegistration.cpp` is deleted instead of
  migrated because `Interface::GUI` panel registration is not a promoted
  endpoint. `LEGACY-001` now records zero external test consumers and remains
  blocked by six legacy-internal Graphics/Runtime files.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeTests` — passed.
- `ctest --test-dir build/ci -N -R 'PanelRegistration'` — passed, 0 tests
  selected.
- `git grep -nE 'import\s+Interface\b|Interface::GUI|#include\s+"Interface' -- 'tests/**'`
  — passed, no matches.
- Consumer grep outside `src/legacy/Interface/**` still reports the expected
  six legacy-internal Graphics/Runtime files owned by `LEGACY-008` and
  `LEGACY-010`.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
