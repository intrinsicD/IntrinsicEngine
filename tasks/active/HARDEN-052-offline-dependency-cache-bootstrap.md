# HARDEN-052 — Bootstrap offline dependency cache for CI preset

## Goal
Unblock the CI preset offline configure requirement by populating `external/cache/*-src` in a reproducible way and documenting the workflow.

## Non-goals
- Runtime/graphics feature work.
- Broad build-system redesign.

## Required changes
- Add/verify documented bootstrap steps for dependency cache population.
- Validate offline configure success after cache bootstrap.
- Record build/ctest evidence needed to close HARDEN-051.

## Tests
- `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## Docs
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` with bootstrap and gate results.
- Update `tasks/active/final-post-reorganization-hardening-audit.md` when blocker is resolved.

## Acceptance criteria
- [x] Offline dependency cache bootstrap steps are documented and reproducible.
- [x] `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` succeeds in a clean workspace after bootstrap (validated with `--fresh` and explicit local compiler overrides in this environment).
- [ ] Follow-on build/ctest evidence is recorded for HARDEN-051 closure.

## Verification
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`


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
- Re-configure with `-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/bin/clang-scan-deps-20` unblocked build progression; long-running build was started and is still in-progress at handoff.
- CPU-supported ctest gate remains pending on completion of the full `IntrinsicTests` build in this environment.
