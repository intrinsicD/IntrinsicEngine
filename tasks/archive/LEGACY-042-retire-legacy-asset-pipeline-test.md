---
id: LEGACY-042
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-042 — Retire legacy Asset.Pipeline test

## Goal
- [x] Remove the legacy `tests/unit/assets/Test_AssetPipeline.cpp`
      compatibility test and grouped CTest entry from the `LEGACY-004`,
      `LEGACY-005`, and `LEGACY-009` external consumer sets.

## Non-goals
- Do not add a promoted `Runtime::AssetPipeline` compatibility wrapper.
- Do not promote the legacy main-thread queue, loaded-material list,
  `RHI::TransferToken` polling, or direct `AssetManager` finalization API.
- Do not change promoted asset service, load-pipeline, GPU asset cache, RHI, or
  runtime handoff behavior.

## Context
- Owner/layer: asset/core/RHI consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test covered `Runtime::AssetPipeline` copy/move traits,
  construction from legacy `RHI::TransferManager`, main-thread queue draining,
  loaded-material tracking, Vulkan transfer-token polling, completion
  callbacks, upload finalization, and no-op empty queue handling.
- Promoted asset streaming is split across `Extrinsic.Asset.LoadPipeline`,
  `AssetService`, `Graphics.GpuAssetCache`, and runtime model/texture handoffs.
- Retained behavior is already covered by promoted tests for staged load-state
  transitions, GPU-fence completion, typed payload/event handling,
  GpuAssetCache residency requests, and runtime texture/model handoff. The old
  `Runtime::AssetPipeline` queue/material-list/finalization surface is not a
  promoted endpoint.

## Required changes
- [x] Delete `tests/unit/assets/Test_AssetPipeline.cpp`.
- [x] Remove `Test_AssetPipeline.cpp` from `tests/CMakeLists.txt`.
- [x] Remove the now-empty `IntrinsicRuntimeTests.AssetPipelineHeadlessGrouped`
      CTest registration and `tests/README.md` mention.
- [x] Update migration docs/task notes with the reduced Asset, Core, and RHI
      consumer counts.

## Tests
- [x] Build affected runtime, runtime-contract, asset-unit, and graphics-asset
      unit test targets.
- [x] Run focused promoted asset load-pipeline/service/GpuAssetCache/runtime
      handoff coverage and confirm no legacy `AssetPipeline` cases remain
      registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-004`, `LEGACY-005`, `LEGACY-009`, and `LEGACY-012`
      blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Asset.Pipeline` solely to prove the old
      `Runtime::AssetPipeline` transfer-token compatibility API.
- [x] Retained promoted asset streaming/upload behavior remains covered by
      `AssetLoadPipeline`, `AssetService`, `GpuAssetCache`, and runtime handoff
      tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicGraphicsAssetsUnitTests
ctest --test-dir build/ci --output-on-failure -R 'AssetLoadPipeline|AssetService|GpuAssetCache|AssetModelTextureHandoff|AssetModelSceneHandoff|AssetPipeline' --timeout 90
ctest --test-dir build/ci -N -R 'AssetPipelineHeadlessTest|^AssetPipeline\.'
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
- Reintroducing legacy `Asset.Pipeline` under promoted asset or runtime names.
- Moving live asset-service ownership into graphics or ECS.
- Adding compatibility re-exports from promoted modules to legacy module names.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy
  transfer-token/queue/material-list compatibility coverage and existing
  promoted asset load-pipeline/service/GpuAssetCache/runtime-handoff tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-004` now records 6 remaining Asset test consumers.
- `LEGACY-005` now records 18 remaining Core test consumers.
- `LEGACY-009` now records 14 remaining RHI test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicGraphicsAssetsUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'AssetLoadPipeline|AssetService|GpuAssetCache|AssetModelTextureHandoff|AssetModelSceneHandoff|AssetPipeline' --timeout 90`
  — passed 88/88 selected promoted asset load/service/cache/handoff tests plus
  the surviving legacy `HeadlessEngineTest.AssetPipelineMainThreadQueueInFrameLoop`
  integration consumer. The `AssetService` / `AssetLoadPipeline` CTest entries
  inherit the broad `integration;runtime;graphics;gpu;vulkan;slow` target
  labels, so this focused verification intentionally ran without the default
  `-LE` exclusion.
- `ctest --test-dir build/ci -N -R 'AssetPipelineHeadlessTest|^AssetPipeline\.'`
  — reported `Total Tests: 0` for the deleted standalone `AssetPipeline` suites.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
