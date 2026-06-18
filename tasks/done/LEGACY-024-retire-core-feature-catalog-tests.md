---
id: LEGACY-024
theme: F
depends_on: [LEGACY-012, CORE-002]
---
# LEGACY-024 — Retire legacy Core feature-catalog tests

## Goal
- [x] Remove the legacy-only `Core.FeatureRegistry` and
      `Core.SystemFeatureCatalog` unit tests from the `LEGACY-005` external Core
      consumer set after `CORE-002` established that promoted `core` will not
      recreate the global feature catalog.

## Non-goals
- Do not add a promoted global feature registry or system feature catalog.
- Do not migrate runtime, graphics, or app legacy feature-registry consumers.
- Do not change promoted callback/config/frame-loop APIs.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `CORE-002` explicitly retired legacy `Core.FeatureRegistry` and
  `Core.SystemFeatureCatalog` as promoted `core` services. Render-pass,
  ECS-system, editor-panel, geometry-operator, shader-reload, and GPU-memory
  policy descriptors stay with their owning runtime, graphics, ECS, UI, config,
  or legacy-subtree cleanup tasks.
- The remaining runtime/graphics/app tests that still import the legacy feature
  registry are consumer cleanup for their owning subtree tasks, not evidence
  that promoted `core` should regain the global catalog.

## Plan
- Remove `tests/unit/core/Test_FeatureRegistry.cpp`.
- Remove `tests/unit/core/Test_SystemFeatureCatalog.cpp`.
- Remove both deleted sources from the legacy-linked core test object list.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Delete the legacy-only feature-registry and system-catalog unit tests.
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
- [x] `tests/unit/core/Test_FeatureRegistry.cpp` no longer exists.
- [x] `tests/unit/core/Test_SystemFeatureCatalog.cpp` no longer exists.
- [x] No promoted `core` global feature catalog is introduced.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests
ctest --test-dir build/ci -N -R '^(FeatureRegistry|SystemFeatureCatalog)\.'
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
- Reintroducing legacy feature/catalog behavior in promoted `core`.
- Editing runtime, graphics, or app feature-registry consumers in this slice.
- Mixing this retirement with unrelated Core test migrations.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only core catalog
  coverage under the already-retired `CORE-002` decision.
- No `Operational` follow-up is owed for the retired global catalog service.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 32 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests` — passed.
- `ctest --test-dir build/ci -N -R '^(FeatureRegistry|SystemFeatureCatalog)\.'`
  — reported `Total Tests: 0` after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 32 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
