# HARDEN-052 — Bootstrap offline dependency cache for CI preset

## Goal
Unblock the CI preset offline configure requirement by populating `external/cache/*-src` in a reproducible way and documenting the workflow.

## Non-goals
- Runtime/graphics feature work.
- Broad build-system redesign.

## Context
HARDEN-051 closure was blocked because the CI preset requires `INTRINSIC_OFFLINE_DEPS=ON` configure success against a populated `external/cache/*-src` dependency snapshot. This task supplies the reproducible bootstrap procedure and CPU-supported gate evidence so the canonical hardening tracker can record fresh pass/fail status for the offline configure/build/test pipeline.

## Required changes
- [x] Add/verify documented bootstrap steps for dependency cache population.
- [x] Validate offline configure success after cache bootstrap.
- [x] Record build/ctest evidence needed to close HARDEN-051.

## Tests
- [x] `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`
- [x] `cmake --build --preset ci --target IntrinsicTests`
- [x] `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## Docs
- [x] Update `tasks/done/0001-post-reorganization-hardening-tracker.md` with bootstrap and gate results.
- [x] Update `tasks/done/final-post-reorganization-hardening-audit.md` when blocker is resolved.

## Acceptance criteria
- [x] Offline dependency cache bootstrap steps are documented and reproducible.
- [x] `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` succeeds in a clean workspace after bootstrap (validated with `--fresh` and explicit local compiler overrides in this environment).
- [x] Follow-on build/ctest evidence is recorded for HARDEN-051 closure.

## Verification
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Forbidden changes
- No changes to runtime/graphics behavior.
- No restructuring of build presets beyond the documented offline bootstrap path.
- No new feature dependencies introduced under `external/cache/`.

## Bootstrap procedure (reproducible)
1. Prime `external/cache/*-src` once with network enabled:
   - `cmake --preset ci -DCMAKE_C_COMPILER=/root/.swiftly/bin/clang -DCMAKE_CXX_COMPILER=/root/.swiftly/bin/clang++`
2. Re-run configure in offline mode from a clean preset state:
   - `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/root/.swiftly/bin/clang -DCMAKE_CXX_COMPILER=/root/.swiftly/bin/clang++`
3. Build and run the CPU-supported gate once toolchain/environment issues are cleared:
   - `cmake --build --preset ci --target IntrinsicTests`
   - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## 2026-04-30 execution evidence
- Cache bootstrap configure (online prime): passed; `external/cache/*-src` was created/populated.
- Offline configure with `INTRINSIC_OFFLINE_DEPS=ON` + `--fresh`: passed in this environment after bootstrap.
- Initial build attempt failed before recompilation because `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` was unset for local Clang 17 wrapper (`CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND`).
- Re-configure with explicit local toolchain paths passed in this environment:
  - `cmake --preset ci --fresh -DINTRINSIC_OFFLINE_DEPS=ON -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps`
- `cmake --build --preset ci --target IntrinsicTests`: passed after declaring direct test object-library dependencies needed by C++ module scanning in `tests/CMakeLists.txt`.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`: passed; `100% tests passed, 0 tests failed out of 1432`; total real time `96.09 sec`; 2 benchmark/SLO tests were reported as skipped by test-internal conditions.

## Completion metadata
- Completion date: 2026-04-30.
- Commit reference: pending current workspace/PR.
- Follow-up: HARDEN-051/final hardening audit still owns GPU/Vulkan/runtime opt-in closure evidence.

