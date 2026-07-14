---
id: LEGACY-037
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-037 — Retire legacy AssetIngestService test

## Goal
- [x] Remove the legacy `tests/unit/assets/Test_AssetIngestService.cpp`
      constructor-shape compatibility test from the `LEGACY-004`,
      `LEGACY-005`, `LEGACY-008`, `LEGACY-009`, and `LEGACY-010` external
      consumer sets.

## Non-goals
- Do not add a promoted `Runtime.AssetIngestService` compatibility wrapper.
- Do not recreate the legacy dependency-heavy constructor surface.
- Do not change promoted asset ingest, runtime import, scene materialization,
  or GPU upload behavior.

## Context
- Owner/layer: assets/runtime consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test verified copy/move/default-construction traits and one
  explicit constructor signature for `Runtime::AssetIngestService`.
- Promoted ingest ownership is the `Extrinsic.Runtime.AssetIngestStateMachine`
  plus promoted asset import bridges, runtime materialization handoffs, and
  runtime `Engine` import/reimport entry points from `RUNTIME-101`,
  `ASSETIO-004`, and related asset/runtime tasks.
- The old `Runtime.AssetIngestService` constructor shape imports legacy
  `Asset.Pipeline`, `Core.IOBackend`, `Graphics`, `RHI`, and
  `Runtime.SceneManager`; that API shape is not a promoted endpoint.

## Required changes
- [x] Delete `tests/unit/assets/Test_AssetIngestService.cpp`.
- [x] Remove `Test_AssetIngestService.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Asset, Core, Graphics,
      RHI, and Runtime consumer counts.

## Tests
- [x] Build affected runtime test targets.
- [x] Run focused promoted asset-ingest state-machine and handoff tests.
- [x] Confirm no legacy `AssetIngestService` test remains registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-004`, `LEGACY-005`, `LEGACY-008`, `LEGACY-009`,
      `LEGACY-010`, and `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Runtime.AssetIngestService` solely to prove the
      old constructor/dependency shape.
- [x] Retained ingest behavior remains covered by promoted
      `RuntimeAssetIngestStateMachine`, asset import format, model-scene
      handoff, texture handoff, and model/texture IO tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -R 'AssetIngestStateMachine|AssetIngestService|AssetImportFormatCoverage|RuntimeAssetModel(TextureIO|TextureHandoff|SceneHandoff)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R '^AssetIngestService\.'
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
- Reintroducing legacy `Runtime.AssetIngestService` as a promoted runtime API.
- Moving graphics or RHI dependencies into `src/assets`.
- Adding compatibility re-exports from promoted runtime to legacy
  `Runtime.AssetIngestService`.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy constructor-shape
  compatibility coverage and existing promoted ingest tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-004` now records 9 remaining Asset test consumers.
- `LEGACY-005` now records 21 remaining Core test consumers.
- `LEGACY-008` now records 37 remaining Graphics test consumers.
- `LEGACY-009` now records 16 remaining RHI test consumers.
- `LEGACY-010` now records 13 remaining Runtime test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'AssetIngestStateMachine|AssetIngestService|AssetImportFormatCoverage|RuntimeAssetModel(TextureIO|TextureHandoff|SceneHandoff)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 37/37 selected promoted entries after the legacy
  `AssetIngestService` tests were removed.
- `ctest --test-dir build/ci -N -R '^AssetIngestService\.'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
