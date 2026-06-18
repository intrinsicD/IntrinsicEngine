---
id: LEGACY-034
theme: F
depends_on: []
---
# LEGACY-034 — Resolve legacy runtime frame-loop and maintenance tests

## Goal
- [ ] Migrate or explicitly retire `tests/unit/runtime/Test_RuntimeFrameLoop.cpp`
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
  `Runtime.ResourceMaintenance` test consumers are:
  `tests/unit/runtime/Test_RuntimeFrameLoop.cpp` and
  `tests/unit/runtime/Test_MaintenanceLane.cpp`.
- Promoted lifecycle ownership already exists:
  `Extrinsic.Core.FrameLoop` owns backend-neutral platform/render/maintenance/
  operational-transition/shutdown contracts, while `Extrinsic.Runtime.Engine`
  owns composition hooks and frame-context wiring. `RUNTIME-099` records that
  legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, and
  `Runtime.ResourceMaintenance` are not imported by the promoted runtime path.
- Existing promoted coverage includes
  `tests/contract/runtime/Test.RuntimeFrameLoopContract.cpp` and
  `tests/contract/runtime/Test.RuntimeEngineLayering.cpp`; additional retained
  behavior should extend those promoted `Test.<Name>.cpp` files or add a new
  promoted-named contract test, not rewrite the legacy files in place.
- `Test_MaintenanceLane.cpp` also contains opt-in Vulkan/deferred-destruction
  checks. Slice A must separate CPU/null maintenance ordering from GPU/Vulkan
  proof before any deletion.

## Required changes
- [ ] Inventory all `RuntimeFrameLoop` and `MaintenanceLane` test cases and
      classify each as promoted coverage, missing retained coverage, opt-in
      GPU/Vulkan coverage, or legacy-only rollback/catalog behavior.
- [ ] Compare retained CPU/null cases against
      `Test.RuntimeFrameLoopContract.cpp`, `Test.RuntimeEngineLayering.cpp`,
      and runtime engine/device-selection tests.
- [ ] Compare retained GPU/Vulkan deferred-destruction cases against existing
      promoted graphics/RHI/runtime lifecycle coverage; if still important,
      either keep them as an opt-in promoted test or create a dedicated follow-up
      before deleting the legacy test.
- [ ] Add or extend promoted tests for retained CPU/null behavior that lacks
      current coverage.
- [ ] Delete or migrate the two legacy tests and update `tests/CMakeLists.txt`.
- [ ] Update `LEGACY-005`, `LEGACY-010`, and `LEGACY-012` blocker notes with
      current consumer counts.

## Tests
- [ ] Build affected core/runtime/graphics test targets.
- [ ] Run focused CTest filters for promoted frame-loop/maintenance contracts.
- [ ] Run opt-in GPU/Vulkan deferred-destruction coverage only if this task keeps
      or migrates that behavior; otherwise record the retirement/follow-up
      decision.
- [ ] Confirm `ctest --test-dir build/ci -N -R 'RuntimeFrameLoop|MaintenanceLane'`
      reports zero legacy-named tests after retirement or only promoted renamed
      tests if migrated.
- [ ] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [ ] Update `docs/migration/legacy-removal-audit.md` with reduced consumer
      counts.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` if legacy rollback,
      feature-toggle, or maintenance-lane behavior is formally retired.
- [ ] Update relevant backlog deletion tasks with current blocker notes.
- [ ] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] No test imports legacy `Runtime.FrameLoop` or
      `Runtime.ResourceMaintenance`.
- [ ] Any retained CPU/null frame-loop or maintenance-ordering contract has
      promoted coverage under `core`/`runtime` ownership.
- [ ] Any retained GPU/Vulkan deferred-destruction behavior is either covered by
      promoted opt-in tests or split into a named follow-up before legacy test
      deletion.
- [ ] Remaining legacy deletion-task consumer counts are current and
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
- No `Operational` follow-up is owed for CPU/null lifecycle ordering if Slice A
  maps it fully to promoted contracts.
- If Slice A finds retained backend-facing deferred-destruction behavior that
  needs opt-in Vulkan proof and is not already covered, split that behavior into
  a named follow-up before retiring this task.
