# CORE-001 — Move frame clock to core

## Goal
- Move the promoted frame clock module from `runtime` to `core` with a core-owned module name and docs synchronized.

## Non-goals
- No frame pacing behavior changes.
- No runtime frame-loop semantic refactors beyond import/type updates needed for the move.

## Context
- `FrameClock` currently lives in `src/runtime` but has no runtime dependencies.
- The owning layer after this task is `core`; runtime continues to own frame orchestration and only consumes the core clock value type.
- The change is a mechanical layer ownership correction requested during review.

## Required changes
- [x] Move `src/runtime/Runtime.FrameClock.cppm` to `src/core/Core.FrameClock.cppm`.
- [x] Rename the exported module to `Extrinsic.Core.FrameClock` and put `FrameClock` in `Extrinsic::Core`.
- [x] Update runtime imports and member type references.
- [x] Update CMake module registration for core/runtime.

## Tests
- [x] Build the relevant promoted runtime/test target with the `ci` preset.
- [x] Run focused runtime/core contract tests.
- [x] Run touched-scope structural checks for layering/test layout/docs links where practical.

## Docs
- [x] Update `src/core/README.md`.
- [x] Update `src/runtime/README.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] No source/docs/test references import or list the old runtime frame-clock module.
- [x] `Extrinsic.Core.FrameClock` is listed as a core public module.
- [x] Runtime builds against the core clock without adding new reverse dependencies.
- [x] Verification commands are recorded with current pass/fail status.

## Verification
```bash
cmake --preset ci
cmake --preset ci --fresh -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build --preset ci --target ExtrinsicRuntime
cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'CoreFrameClock' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Status:

- `cmake --preset ci` failed because `clang-20`/`clang++-20` were not installed on `PATH`.
- Reconfigured the `ci` preset with verified Clang 22 compiler paths; configure passed.
- `ExtrinsicRuntime`, `IntrinsicCoreWrapperUnitTests`, and `IntrinsicRuntimeIntegrationTests` built successfully.
- `cmake --build --preset ci --target IntrinsicTests` was attempted and failed in unrelated legacy code because `external/cache/glm-src/glm/detail/type_vec3.inl` was missing from the dependency cache.
- Focused `CoreFrameClock` CTest entries passed: 3/3.
- Focused `RuntimeEngineLayering` CTest entries passed: 6/6.
- Layering, test layout, docs link checks passed.

## Forbidden changes
- Mixing the mechanical move with frame pacing behavior edits.
- Introducing unrelated feature work.
- Moving runtime frame orchestration out of `runtime`.

## Execution log
- 2026-05-11: Moved `FrameClock` to `Extrinsic.Core.FrameClock`, updated runtime imports, refreshed docs, regenerated module inventory, and added focused core unit coverage.
- 2026-05-11: Verified focused core/runtime targets and structural checks. Full `IntrinsicTests` build attempt reached an unrelated missing GLM cache file in legacy runtime compilation.

## Completion metadata
- Completion date: 2026-05-11.
- Commit reference: pending current workspace/PR.
- Follow-up: None.
