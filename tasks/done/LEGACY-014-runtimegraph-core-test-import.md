---
id: LEGACY-014
theme: F
depends_on: [LEGACY-013]
---
# LEGACY-014 — Remove unused RuntimeGraph legacy Core test import

## Goal
- [x] Remove the unused bare legacy `Core` import from
      `tests/unit/geometry/Test_RuntimeGraph.cpp`, reducing the remaining
      `LEGACY-012` Core test-consumer set without changing test behavior.

## Non-goals
- Do not migrate broad legacy compatibility test suites.
- Do not delete any `src/legacy/` subtree.
- Do not change graph/runtime geometry behavior.

## Context
- Owner/layer: geometry unit-test cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- After `LEGACY-013`, the remaining `LEGACY-005` external consumers are tests.
  `Test_RuntimeGraph.cpp` imports bare `Core` but does not reference `Core::*`
  symbols.

## Required changes
- [x] Remove `import Core;` from `tests/unit/geometry/Test_RuntimeGraph.cpp`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected geometry test target.
- [x] Run the focused `RuntimeGraph` CTest filter under CPU/null labels.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/geometry/Test_RuntimeGraph.cpp` no longer imports bare
      legacy `Core`.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.
- [x] No compatibility alias or re-export for legacy `Core.*` is introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeGraph' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this one-test import cleanup with broader test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `Test_RuntimeGraph.cpp` no longer imports bare legacy `Core`; the broader
  `LEGACY-005` Core gate now records 133 legacy-internal consumers and 43 test
  consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicGeometryTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeGraph' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 22/22 tests.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — passed.
- `git diff --check` — passed.
