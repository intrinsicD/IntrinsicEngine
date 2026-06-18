---
id: LEGACY-039
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-039 — Retire legacy element-selection test

## Goal
- [x] Remove the legacy `tests/integration/runtime/Test_ElementSelection.cpp`
      compatibility test from the `LEGACY-006` and `LEGACY-010` external
      consumer sets.

## Non-goals
- Do not add a promoted `Runtime.Selection::SubElementSelection`
  compatibility type.
- Do not promote `Runtime.SelectionModule` element-mode configuration or
  mutable per-entity vertex/edge/face selection sets.
- Do not change promoted primitive-refinement, selection-controller, or editor
  selection model behavior.

## Context
- Owner/layer: ECS/runtime consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test verified `Runtime::Selection::SubElementSelection`, the
  legacy `ElementMode` enum, and `Runtime.SelectionModule` clear/reset behavior
  for mutable vertex/edge/face sets.
- Promoted sub-primitive behavior is represented by
  `Extrinsic.Runtime.PrimitiveSelectionRefinement`, engine-owned
  `GetLastRefinedPrimitiveSelection()` caching, and `SandboxEditorUi` selection
  model presentation. Whole-entity selection state remains owned by
  `Extrinsic.Runtime.SelectionController`.
- The old persistent mutable `SubElementSelection` set API is not a promoted
  endpoint for current workflows.

## Required changes
- [x] Delete `tests/integration/runtime/Test_ElementSelection.cpp`.
- [x] Remove `Test_ElementSelection.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced ECS and Runtime
      consumer counts.

## Tests
- [x] Build affected runtime and promoted runtime contract test targets.
- [x] Run focused promoted primitive-selection refinement tests and confirm no
      legacy `ElementSelection` cases remain registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-006`, `LEGACY-010`, and `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Runtime.Selection` or `Runtime.SelectionModule`
      solely to prove the old persistent `SubElementSelection` set API.
- [x] Retained primitive refinement/editor behavior remains covered by
      promoted `PrimitiveSelectionRefinement` and `SandboxEditorUi` tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'PrimitiveSelectionRefinement|ElementSelection' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R '^ElementSelection\.'
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
- Reintroducing legacy `SubElementSelection` under promoted runtime names.
- Moving primitive-selection state into ECS.
- Adding compatibility re-exports from promoted runtime to legacy selection
  modules.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy persistent
  sub-element selection compatibility coverage and existing promoted primitive
  refinement/editor tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-006` now records 19 remaining ECS test consumers.
- `LEGACY-010` now records 11 remaining Runtime test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeContractTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'PrimitiveSelectionRefinement|ElementSelection' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 42/42 selected promoted primitive-refinement entries after the
  legacy `ElementSelection` tests were removed.
- `ctest --test-dir build/ci -N -R '^ElementSelection\.'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
