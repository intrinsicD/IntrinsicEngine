# BUG-003 — FetchContent cache corruption breaks dependency checkouts during CI retries

## Goal
- Make repository dependency population resilient to interrupted or repeated CI configure/build attempts so partial checkouts do not poison later builds.

## Non-goals
- No change to dependency versions unless required for a targeted FetchContent fix.
- No replacement of FetchContent with a different package manager.
- No unrelated build-performance tuning.

## Context
- Status: done 2026-05-09.
- Owner/agent: Copilot.
- Observed: 2026-05-09 while retrying CI commands against `build/ci` and `external/cache`.
- Symptom: after a failed/incomplete population, subsequent configure/build steps fail from corrupted dependency trees:
  - `external/cache/glm-src` is missing headers such as `glm/glm.hpp`, `detail/type_vec3.hpp`, and `detail/setup.hpp`.
  - `external/cache/json-src` clone retries fail with Git lock/config/ref errors including `could not lock config file`, `destination path 'json-src' already exists and is not an empty directory`, and `BUG: refs/files-backend.c:3055`.
  - `external/cache/volk-src` generation fails because `volk.h` is missing.
- Expected behavior: dependency population should be atomic or fail with a clear cleanup instruction, and later CI retries should not reuse incomplete source trees as valid dependencies.
- Impact: `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicTests`, sanitizer configure, and benchmark build retries can fail before engine code is compiled.

## Required changes
- [x] Audit `cmake/Dependencies.cmake` FetchContent population into `external/cache/` for lock/atomicity and completeness checks.
- [x] Add dependency source-tree validation for header-only/content dependencies before `FetchContent_MakeAvailable` treats cached directories as valid.
- [x] Ensure failed clone/populate attempts either clean their partial `<name>-src` directory or fail with a deterministic recovery message.
- [x] Decide whether local/manual CI retry scripts should use isolated build/cache directories and document that policy if required.

## Tests
- [x] Add a focused dependency-cache regression test or script-level check that simulates an incomplete dependency directory and verifies configure fails deterministically or repairs it.
- [x] Re-run the default configure/build/test gate after clearing the corrupted dependency directories.

## Docs
- [x] Update `docs/build-troubleshooting.md` with safe recovery steps for partial `external/cache/*-src` checkouts.
- [x] Update dependency tooling docs if `INTRINSIC_OFFLINE_DEPS` or cache-validation semantics change.

## Acceptance criteria
- [x] A missing required dependency file such as `external/cache/glm-src/glm/glm.hpp` or `external/cache/volk-src/volk.h` is detected before a long compile/generate phase.
- [x] Re-running configure after a failed dependency clone does not leave Git lock/ref corruption as the primary error.
- [x] Recovery instructions are clear for both online and `INTRINSIC_OFFLINE_DEPS=ON` workflows.
- [x] The fix preserves centralized dependency declarations in `cmake/Dependencies.cmake` and does not introduce layer violations.

## Verification
```bash
rm -rf build/ci
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not commit generated or downloaded dependency contents from `external/cache/`.
- Do not mask dependency-population failures by continuing with missing headers.
- Do not make online network access mandatory for `INTRINSIC_OFFLINE_DEPS=ON` when the cache is already complete.

## Captured evidence
- `build/ci-full-logs/build_intrinsic_tests.log` and `build_intrinsic_tests_rerun.log` show missing GLM headers and JSON clone corruption.
- `build/ci-full-logs/configure_asan.log` shows GLM CMake generation failing because `external/cache/glm-src/CMakeLists.txt` is missing.
- `build/ci-full-logs/build_benchmark_smoke.log` shows Volk generation failing because `volk.h` is missing.

## Completion
- Completed: 2026-05-09.
- Commit reference: pending.
- Notes: dependency population now validates required source markers under `external/cache/<name>-src`; online configures remove incomplete source trees before repopulation, while offline configures fail with deterministic recovery guidance.

