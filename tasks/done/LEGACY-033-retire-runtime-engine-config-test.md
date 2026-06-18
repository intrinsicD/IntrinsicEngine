---
id: LEGACY-033
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-033 — Retire legacy RuntimeEngineConfig test

## Goal
- [x] Remove the legacy `Runtime.Engine` / `Runtime.FrameLoop` engine-config
      validation test from the `LEGACY-010` external consumer set.

## Non-goals
- Do not add promoted runtime config validation fields to match the old legacy
  shape.
- Do not change `Extrinsic.Core.Config.Engine`, `Extrinsic.Runtime.Engine`, or
  runtime device-selection behavior.
- Do not delete `src/legacy/Runtime/`.

## Context
- Owner/layer: runtime/core config test cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/runtime/Test_RuntimeEngineConfig.cpp` covered legacy
  `Runtime::EngineConfig` scalar fields such as width, height, frame context
  count, benchmark flags, frame pacing, and arena size. Those fields do not map
  one-for-one to promoted `Extrinsic::Core::Config::EngineConfig`, which now
  composes `RenderConfig`, `SimulationConfig`, `WindowConfig`,
  `ReferenceSceneConfig`, and `CameraConfig`.
- Retained promoted coverage already exists in `tests/unit/core/Test.Core.Config.cpp`
  for promoted config defaults, plus runtime contract tests for engine/device
  selection and reference-scene configuration.
- The legacy validation/sanitization helper is therefore legacy-only cleanup,
  not a missing promoted runtime API.

## Plan
- Delete the legacy runtime engine-config unit test.
- Remove it from the runtime test object list.
- Update legacy-removal docs and backlog notes with the reduced Runtime test
  consumer count.

## Required changes
- [x] Delete `tests/unit/runtime/Test_RuntimeEngineConfig.cpp`.
- [x] Remove `Test_RuntimeEngineConfig.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Runtime consumer count.

## Tests
- [x] Build the affected runtime/core test targets.
- [x] Run focused promoted config and runtime engine/device-selection coverage.
- [x] Confirm the retired legacy `RuntimeEngineConfig` test cases are no longer
      discovered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the explicit
      legacy config-validation retirement decision.
- [x] Update `LEGACY-010` and `LEGACY-012` blocker notes.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] `tests/unit/runtime/Test_RuntimeEngineConfig.cpp` no longer exists.
- [x] No test imports legacy `Runtime.Engine` / `Runtime.FrameLoop` only for old
      engine-config validation.
- [x] Promoted config and runtime device-selection tests remain wired and
      focused verification passes.
- [x] Remaining `LEGACY-010` test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -R 'CoreConfig|RuntimeDeviceSelection|RuntimeVulkanBreadcrumb|RuntimeEngineConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'RuntimeEngineConfig'
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
- Reintroducing the legacy `Runtime::EngineConfig` scalar validation surface
  under promoted runtime or core modules.
- Changing default engine/render/window configuration behavior.
- Deleting or editing legacy source subtrees.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only config
  validation coverage and existing promoted config/runtime coverage.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-010` now records 17 remaining Runtime test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreConfig|RuntimeDeviceSelection|RuntimeVulkanBreadcrumb|RuntimeEngineConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — initially hit a transient CMake 4.3 GoogleTest JSON discovery parse error
  immediately after regeneration; rerun passed 10/10 selected tests.
- `ctest --test-dir build/ci -N -R 'RuntimeEngineConfig'` — reported
  `Total Tests: 0`.
- `git grep -nE '^\s*(export\s+)?import\s+Runtime\.(Engine|FrameLoop|GraphicsBackend|PointCloudKMeans|RenderExtraction|RenderOrchestrator|ResourceMaintenance|AssetIngestService|SceneManager|SceneSerializer|Selection|SelectionModule|SystemBundles)\b' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 17 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` —
  passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.
