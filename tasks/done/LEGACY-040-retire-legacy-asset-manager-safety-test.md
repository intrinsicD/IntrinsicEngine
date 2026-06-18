---
id: LEGACY-040
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-040 — Retire legacy Asset.Manager safety test

## Goal
- [x] Remove the legacy `tests/unit/assets/Test_CoreAssetSafety.cpp`
      compatibility test from the `LEGACY-004` and `LEGACY-005` external
      consumer sets.

## Non-goals
- Do not add a promoted `Asset.Manager` compatibility wrapper.
- Do not promote the legacy `Core::Assets::AssetLoaderFunc` concept or
  pointer-returning `AssetManager::GetRaw` / `AcquireLease` API shape.
- Do not change promoted asset service, registry, payload-store, or load
  pipeline behavior.

## Context
- Owner/layer: asset/core consumer-test cleanup under the `LEGACY-012`
  migration program.
- The legacy test covered `Asset.Manager` loader-copyability constraints,
  stateful reload captures, invalid-handle errors, failed loader errors, and
  null pointer payload handling through the old `Core::Assets::AssetManager`
  API.
- Promoted asset ownership is split across `Extrinsic.Asset.Service`,
  `Asset.Registry`, `Asset.PayloadStore`, and `Asset.LoadPipeline`.
- Retained behavior is already covered by promoted tests for captured-loader
  reload, reload failure preserving the prior payload, wrong-type reads,
  dead-handle errors, failed-load cleanup, load-state transitions, and event
  ordering. The old null-pointer loader failure and lease-returning pointer API
  shape is not a promoted endpoint.

## Required changes
- [x] Delete `tests/unit/assets/Test_CoreAssetSafety.cpp`.
- [x] Remove `Test_CoreAssetSafety.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Asset and Core
      consumer counts.

## Tests
- [x] Build affected runtime and promoted asset integration test targets.
- [x] Run focused promoted `AssetService` coverage and confirm no legacy
      `AssetLoaderSafety` / `AssetErrorPaths` cases remain registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-004`, `LEGACY-005`, and `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Asset.Manager` solely to prove the old
      loader-safety/error-path compatibility API.
- [x] Retained promoted asset behavior remains covered by `AssetService`,
      `AssetRegistry`, `AssetPayloadStore`, and `AssetLoadPipeline` tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AssetService|AssetLoadPipeline|AssetLoaderSafety|AssetErrorPaths' --timeout 90
ctest --test-dir build/ci -N -R 'AssetLoaderSafety|AssetErrorPaths'
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
- Reintroducing legacy `Asset.Manager` under promoted asset names.
- Moving live asset-service ownership into graphics or ECS.
- Adding compatibility re-exports from promoted asset modules to legacy module
  names.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy
  loader-safety/error-path compatibility coverage and existing promoted asset
  service/load-pipeline tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-004` now records 8 remaining Asset test consumers.
- `LEGACY-005` now records 20 remaining Core test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'AssetService|AssetLoadPipeline|AssetLoaderSafety|AssetErrorPaths' --timeout 90`
  — passed 54/54 selected promoted asset service/load-pipeline tests. The
  `AssetService` / `AssetLoadPipeline` CTest entries inherit the broad
  `integration;runtime;graphics;gpu;vulkan;slow` target labels, so this focused
  verification intentionally ran without the default `-LE` exclusion.
- `ctest --test-dir build/ci -N -R 'AssetLoaderSafety|AssetErrorPaths'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
