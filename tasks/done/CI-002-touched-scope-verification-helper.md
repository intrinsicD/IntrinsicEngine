# CI-002 — Touched-scope verification helper

## Goal
- Add a conservative local/CI helper that maps changed files to affected build targets, CTest label filters, and repository structural checks so small edits can be verified without always running the full CPU gate.

## Non-goals
- Do not replace the canonical full CPU-supported CI gate.
- Do not wire the helper into GitHub Actions in this slice.
- Do not change CTest labels, test source layout, or CMake target structure.

## Context
- Owner: CI/tooling; touches `tools/ci`, tooling regression tests, and documentation.
- `AGENTS.md` still requires the strongest relevant verification subset and the full default CPU gate before PR/merge-level confidence.
- This helper should be conservative: unknown or broad build-system changes should recommend the existing broad CPU gate.

## Required changes
- [x] Add `tools/ci/touched_scope.py` with dry-run and run modes.
- [x] Map common repository path prefixes to build targets, CTest labels, and structural checks.
- [x] Fall back to the broad CPU-supported gate for build-system or unknown source-scope changes.

## Tests
- [x] Add Python regression coverage for representative path-to-command mappings.
- [x] Run the new Python regression tests directly.
- [x] Run relevant repository structural checks.

## Docs
- [x] Document local touched-scope verification usage and limitations.

## Acceptance criteria
- [x] Geometry-only changes produce `IntrinsicGeometryTests`, `-L geometry`, and relevant structural checks.
- [x] Docs/tasks-only changes avoid unnecessary C++ builds while selecting docs/task checks.
- [x] Build-system or unknown source changes recommend the broad CPU gate.
- [x] The helper clearly states it is not a replacement for the full PR gate.

## Verification
```bash
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/ci/touched_scope.py --root . --changed-file src/geometry/Geometry.PointCloud.IO.cpp --print
python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --changed-file tasks/active/CI-002-touched-scope-verification-helper.md --print
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed verification:
- `python3 tests/regression/tooling/Test.TouchedScope.py` — passed, 4/4.
- `python3 -m py_compile tools/ci/touched_scope.py tests/regression/tooling/Test.TouchedScope.py` — passed.
- `python3 tools/ci/touched_scope.py --root . --changed-file src/geometry/Geometry.PointCloud.IO.cpp --build-dir cmake-build-debug --print` — produced the expected geometry target/label/layering plan.
- `python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --changed-file tasks/active/CI-002-touched-scope-verification-helper.md --build-dir cmake-build-debug --print` — produced the expected docs/task-only plan without a C++ build.
- `python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --changed-file tasks/active/CI-002-touched-scope-verification-helper.md --build-dir cmake-build-debug --run` — passed docs/task checks.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed in warning mode with no broken relative links.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed before retirement; re-run after retirement once completion metadata was added.

## Forbidden changes
- Weakening or deleting existing CI/workflow gates.
- Adding new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt`.
- Introducing unrelated source or test refactors.

## Completion
- Completed: 2026-05-13.
- Status: done.
- Implementation commit: this local change (`CI-002: add touched-scope verifier`).
