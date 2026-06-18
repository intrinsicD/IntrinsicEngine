---
id: LEGACY-041
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-041 — Retire legacy Asset.Manager core test

## Goal
- [x] Remove the legacy `tests/unit/assets/Test_CoreAssets.cpp`
      compatibility test from the `LEGACY-004`, `LEGACY-005`, `LEGACY-008`, and
      `LEGACY-009` external consumer sets.

## Non-goals
- Do not add a promoted `Asset.Manager` compatibility wrapper.
- Do not promote the legacy `AssetLease`, `TryGetFast`, read-phase, or
  `AssetManager::Clear()` API shapes.
- Do not change promoted asset service, registry, payload-store, load-pipeline,
  event-bus, graphics, RHI, or runtime handoff behavior.

## Context
- Owner/layer: asset/core/graphics/RHI consumer-test cleanup under the
  `LEGACY-012` migration program.
- The legacy test covered `Core::Assets::AssetManager` async loading, path
  caching, pointer/lease access, processing/finalization gates, main-thread
  notifications, read-phase `TryGetFast`, concurrent lease copying, clear while
  leases are held, and a compile-only `Graphics::Material` ownership check.
- Promoted asset ownership is split across `Extrinsic.Asset.Service`,
  `Asset.Registry`, `Asset.PayloadStore`, `Asset.LoadPipeline`, and
  `Asset.EventBus`, with runtime-owned texture/model handoffs to graphics.
- Retained behavior is already covered by promoted tests for path interning,
  typed payload storage/reads, wrong-type errors, reload/destroy ordering,
  failed-load cleanup, load-state transitions, event fanout, and runtime
  asset-to-graphics handoff. The old pointer-returning manager/lease API is not
  a promoted endpoint.

## Required changes
- [x] Delete `tests/unit/assets/Test_CoreAssets.cpp`.
- [x] Remove `Test_CoreAssets.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Asset, Core, Graphics,
      and RHI consumer counts.

## Tests
- [x] Build affected runtime and promoted asset integration test targets.
- [x] Run focused promoted asset service/load-pipeline/payload-store/registry
      coverage and confirm no legacy `AssetSystem` / `CoreAssets` cases remain
      registered.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement
      decision.
- [x] Update `LEGACY-004`, `LEGACY-005`, `LEGACY-008`, `LEGACY-009`, and
      `LEGACY-012` blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Asset.Manager` solely to prove the old
      async/cache/lease/clear/TryGetFast compatibility API.
- [x] Retained promoted asset behavior remains covered by `AssetService`,
      `AssetRegistry`, `AssetPayloadStore`, `AssetLoadPipeline`, `AssetEventBus`,
      and runtime handoff tests.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests IntrinsicAssetUnitTests
ctest --test-dir build/ci --output-on-failure -R 'AssetService|AssetLoadPipeline|AssetPayloadStore|AssetRegistry|AssetSystem|CoreAssets' --timeout 90
ctest --test-dir build/ci -N -R 'AssetSystem|CoreAssets'
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
  async/cache/lease/clear/TryGetFast compatibility coverage and existing
  promoted asset service/load-pipeline/payload-store/registry/event-bus tests.
- No `Operational` follow-up is owed for this test-consumer retirement.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-004` now records 7 remaining Asset test consumers.
- `LEGACY-005` now records 19 remaining Core test consumers.
- `LEGACY-008` now records 36 remaining Graphics test consumers.
- `LEGACY-009` now records 15 remaining RHI test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicRuntimeIntegrationTests IntrinsicAssetUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'AssetService|AssetLoadPipeline|AssetPayloadStore|AssetRegistry|AssetSystem|CoreAssets' --timeout 90`
  — passed 84/84 selected promoted asset service/load-pipeline/payload-store/
  registry tests. The `AssetService` / `AssetLoadPipeline` CTest entries
  inherit the broad `integration;runtime;graphics;gpu;vulkan;slow` target
  labels, so this focused verification intentionally ran without the default
  `-LE` exclusion.
- `ctest --test-dir build/ci -N -R 'AssetSystem|CoreAssets'`
  — reported `Total Tests: 0`.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
