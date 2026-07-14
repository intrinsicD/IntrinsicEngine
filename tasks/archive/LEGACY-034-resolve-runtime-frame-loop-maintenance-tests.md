---
id: LEGACY-034
theme: F
depends_on: []
---
# LEGACY-034 — Resolve legacy runtime frame-loop and maintenance tests

## Goal
- [x] Migrate or explicitly retire `tests/unit/runtime/Test_RuntimeFrameLoop.cpp`
      and `tests/unit/runtime/Test_MaintenanceLane.cpp` so they no longer import
      bare legacy `Core`, `Runtime.FrameLoop`, `Runtime.RenderExtraction`,
      `Runtime.ResourceMaintenance`, or `Graphics.Camera`, and so retained
      runtime lifecycle/maintenance contracts remain covered by promoted tests.

## Non-goals
- Do not delete `src/legacy/Runtime/`, `src/legacy/Core/`, or
  `src/legacy/Graphics/` in this task.
- Do not revive legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, or
  `Runtime.ResourceMaintenance` as promoted modules.
- Do not promote the legacy `FrameLoopFeatureCatalog` / global feature-toggle
  rollback surface.
- Do not change runtime frame-loop, renderer, asset, or Vulkan behavior while
  resolving the tests.

## Context
- Owner/layer: runtime lifecycle and maintenance test cleanup under the
  `LEGACY-012` legacy consumer-test migration program.
- The remaining direct legacy `Runtime.FrameLoop` /
  `Runtime.ResourceMaintenance` test consumers were
  `tests/unit/runtime/Test_RuntimeFrameLoop.cpp` and
  `tests/unit/runtime/Test_MaintenanceLane.cpp`.
- Promoted lifecycle ownership already exists:
  `Extrinsic.Core.FrameLoop` owns backend-neutral platform/render/maintenance/
  operational-transition/shutdown contracts, while `Extrinsic.Runtime.Engine`
  owns composition hooks and frame-context wiring. `RUNTIME-099` records that
  legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, and
  `Runtime.ResourceMaintenance` are not imported by the promoted runtime path.
- Existing promoted coverage includes
  `tests/contract/runtime/Test.RuntimeFrameLoopContract.cpp`,
  `tests/contract/runtime/Test.RuntimeEngineLayering.cpp`, runtime device
  selection/config tests, and renderer/graphics lifecycle tests.
- `Test_MaintenanceLane.cpp` also contained opt-in Vulkan/deferred-destruction
  checks. Those checks are backend-facing RHI behavior, not runtime
  maintenance-lane behavior, so `LEGACY-035` now owns the retained-vs-retired
  decision before that coverage can become promoted RHI/graphics evidence.

## Required changes
- [x] Inventory all `RuntimeFrameLoop` and `MaintenanceLane` test cases and
      classify each as promoted coverage, missing retained coverage, opt-in
      GPU/Vulkan coverage, or legacy-only rollback/catalog behavior.
- [x] Compare retained CPU/null cases against
      `Test.RuntimeFrameLoopContract.cpp`, `Test.RuntimeEngineLayering.cpp`,
      and runtime engine/device-selection tests.
- [x] Compare retained GPU/Vulkan deferred-destruction cases against existing
      promoted graphics/RHI/runtime lifecycle coverage; if still important,
      either keep them as an opt-in promoted test or create a dedicated follow-up
      before deleting the legacy test.
- [x] Add or extend promoted tests for retained CPU/null behavior that lacks
      current coverage.
- [x] Delete or migrate the two legacy tests and update `tests/CMakeLists.txt`.
- [x] Update `LEGACY-005`, `LEGACY-010`, and `LEGACY-012` blocker notes with
      current consumer counts.

## Classification

| Legacy test coverage | Decision |
|---|---|
| Frame timing helpers, fixed-step accumulation, platform begin-frame/minimized/close paths, resize skip/sync, render-lane ordering, maintenance ordering, operational transition, shutdown, and engine layering | Retired from legacy files because retained behavior is covered by `Extrinsic.Core.FrameLoop` and `Extrinsic.Runtime.Engine` contract/integration tests. |
| `FrameLoopFeatureCatalog`, legacy rollback mode, and global feature-toggle behavior | Retired as legacy-only behavior; the promoted frame-loop contract does not reintroduce the global catalog/toggle surface. |
| Activity-tracker/idling helper behavior | Retired with the legacy frame-loop helper; promoted runtime uses explicit platform/frame contracts and Null-window `Engine::Run()` coverage rather than the old helper API. |
| `MaintenanceLaneGpuTest` Vulkan `SafeDestroy*` deferred-destruction behavior | Split to `LEGACY-035` because it is RHI/Vulkan-owned opt-in coverage, not a runtime maintenance-lane contract. |

## Tests
- [x] Build affected core/runtime/graphics test targets.
- [x] Run focused CTest filters for promoted frame-loop/maintenance contracts.
- [x] Run opt-in GPU/Vulkan deferred-destruction coverage only if this task keeps
      or migrates that behavior; otherwise record the retirement/follow-up
      decision.
- [x] Confirm `ctest --test-dir build/ci -N -R 'RuntimeFrameLoop|MaintenanceLane'`
      reports zero legacy-named tests after retirement or only promoted renamed
      tests if migrated.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md` with reduced consumer
      counts.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` because legacy
      rollback/catalog and maintenance-lane behavior is formally retired or
      split to `LEGACY-035`.
- [x] Update relevant backlog deletion tasks with current blocker notes.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No test imports legacy `Runtime.FrameLoop` or
      `Runtime.ResourceMaintenance`.
- [x] Any retained CPU/null frame-loop or maintenance-ordering contract has
      promoted coverage under `core`/`runtime` ownership.
- [x] Any retained GPU/Vulkan deferred-destruction behavior is either covered by
      promoted opt-in tests or split into a named follow-up before legacy test
      deletion.
- [x] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
git grep -nE '^\s*(export\s+)?import\s+Runtime\.(FrameLoop|ResourceMaintenance)\b' -- 'tests/**'
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeFrameLoopContract|RuntimeEngineLayering|RuntimeFrameLoop|MaintenanceLane' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'RuntimeFrameLoop|MaintenanceLane'
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
- Reintroducing legacy frame-loop or maintenance modules under promoted runtime
  names.
- Recreating the old feature-catalog rollback toggle as promoted API.
- Running broad GPU/Vulkan tests as default CPU-gate proof.
- Deleting the tests before retained-vs-retired behavior is documented.

## Maturity
- Target: `CPUContracted` for consumer-test migration/retirement.
- Closed at `CPUContracted`: retained CPU/null lifecycle ordering maps to
  promoted contracts; backend-facing deferred-destruction behavior is split to
  `LEGACY-035`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-005` now records 22 remaining Core test consumers.
- `LEGACY-008` now records 38 remaining Graphics test consumers.
- `LEGACY-009` now records 17 remaining RHI test consumers and `LEGACY-035`
  owns the deferred-destruction coverage decision.
- `LEGACY-010` now records 15 remaining Runtime test consumers.

## Verification results
- `git grep -nE '^\s*(export\s+)?import\s+Runtime\.(FrameLoop|ResourceMaintenance)\b' -- 'tests/**'`
  — no matches.
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests IntrinsicRuntimeTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeFrameLoopContract|RuntimeEngineLayering|RuntimeFrameLoop|MaintenanceLane' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — reported no selected tests because the promoted entries are currently
  registered under broader runtime graphics labels.
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeFrameLoopContract|RuntimeEngineLayering' --timeout 90`
  — passed 45/45 selected promoted entries.
- `ctest --test-dir build/ci -N -R '^RuntimeFrameLoop\.|^MaintenanceLane'`
  — reported `Total Tests: 0`.
