---
id: LEGACY-038
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-038 — Retire legacy runtime selection modes test

## Goal
- [x] Remove the legacy `tests/contract/runtime/Test.RuntimeSelectionModes.cpp`
      compatibility test from the `LEGACY-006` and `LEGACY-010` external
      consumer sets while preserving retained multi-selection mode coverage in
      the promoted `SelectionController` test.

## Non-goals
- Do not add a promoted `Runtime.Selection` compatibility wrapper.
- Do not promote `Runtime.SelectionModule::GetSelectedEntities`.
- Do not change promoted selection-controller behavior or renderer pick
  readback wiring.

## Context
- Owner/layer: ECS/runtime consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test verified `Runtime::Selection::ApplySelection` add, toggle,
  replace, and background behavior against legacy `ECS::Scene`, plus
  `Runtime::SelectionModule::GetSelectedEntities`.
- Promoted runtime selection authority is `Extrinsic.Runtime.SelectionController`.
  It owns click/add/toggle/background policy, selected/hovered tags, stable-id
  snapshots, pick coalescing, and readback correlation without importing
  graphics or platform.
- The old raw selected-entity-list helper API is not a promoted endpoint;
  promoted consumers use `SelectionController::SelectedStableIds()` and
  selection state queries.

## Required changes
- [x] Delete `tests/contract/runtime/Test.RuntimeSelectionModes.cpp`.
- [x] Remove `Test.RuntimeSelectionModes.cpp` from `tests/CMakeLists.txt`.
- [x] Add promoted `SelectionController` coverage for replace-then-add range
      style selection and toggle add/remove behavior in a multi-selection set.
- [x] Update migration docs/task notes with the reduced ECS and Runtime
      consumer counts.

## Tests
- [x] Build affected runtime contract test target.
- [x] Run focused promoted `SelectionController` tests and confirm the legacy
      `RuntimeSelection`/`RuntimeSelectionModule` cases are no longer
      registered from the retired file.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-006`, `LEGACY-010`, and `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Runtime.Selection` or
      `Runtime.SelectionModule` solely to prove click-mode selection behavior.
- [x] Retained add/toggle/replace/background multi-selection behavior remains
      covered by promoted `SelectionController` tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SelectionController|RuntimeSelectionMode|RuntimeSelectionModule' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'RuntimeSelection/RuntimeSelectionModeParameterized|RuntimeSelection\.RangeSelect_ReplaceThenAdd|RuntimeSelectionModule\.GetSelectedEntities'
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
- Reintroducing legacy `Runtime.Selection` or `Runtime.SelectionModule` under
  promoted runtime names.
- Moving selection mutation into ECS.
- Adding compatibility re-exports from promoted runtime to legacy selection
  modules.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy selection-module
  compatibility coverage and promoted selection-controller contract tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-006` now records 20 remaining ECS test consumers.
- `LEGACY-010` now records 12 remaining Runtime test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'SelectionController|RuntimeSelectionMode|RuntimeSelectionModule' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 29/29 selected promoted entries after the legacy selection-mode
  tests were removed.
- `ctest --test-dir build/ci -N -R 'RuntimeSelection/RuntimeSelectionModeParameterized|RuntimeSelection\.RangeSelect_ReplaceThenAdd|RuntimeSelectionModule\.GetSelectedEntities'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
