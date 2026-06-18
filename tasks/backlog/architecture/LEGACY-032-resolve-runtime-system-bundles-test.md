---
id: LEGACY-032
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-032 — Resolve legacy `Runtime.SystemBundles` test migration

## Goal
- [ ] Migrate or explicitly retire
      `tests/unit/runtime/Test_RuntimeSystemBundles.cpp` so it no longer imports
      bare legacy `Core`, `ECS`, `Core.SystemFeatureCatalog`, or
      `Runtime.SystemBundles`, and so the retained runtime/graphics system-bundle
      contracts are covered by promoted tests.

## Non-goals
- Do not delete `src/legacy/Runtime/`, `src/legacy/Core/`, `src/legacy/ECS/`,
  or `src/legacy/Graphics/` in this task.
- Do not promote the global legacy `Core.SystemFeatureCatalog` shape.
- Do not reintroduce feature toggles as a cross-layer global registry.
- Do not mix this task with unrelated runtime frame-loop, asset-manager, or
  renderer hot-reload consumer migrations.

## Context
- Owner/layer: runtime composition and legacy consumer-test cleanup under
  `LEGACY-012`.
- `tests/unit/runtime/Test_RuntimeSystemBundles.cpp` currently keeps three
  deletion gates red: `LEGACY-005` (`Core`/`Core.SystemFeatureCatalog`),
  `LEGACY-006` (`ECS`), and `LEGACY-010` (`Runtime.SystemBundles`).
- Promoted runtime already has `Extrinsic.Runtime.EcsSystemBundle` for baseline
  transform hierarchy, bounds propagation, and render-sync activation. Coverage
  lives in `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp`.
- The old test also names legacy graphics lifecycle passes
  (`PropertySetDirtySync`, `PrimitiveBVHBuild`, `GraphLifecycle`,
  `MeshRendererLifecycle`, `PointCloudLifecycle`, `MeshViewLifecycle`,
  `GPUSceneSync`). Those expectations are not a pure ECS/runtime import swap:
  the parity matrix says remaining legacy system bundles and graphics lifecycle
  behavior require value-gated promoted owners/tests or an explicit retirement
  decision.

## Plan
- **Slice A — Inventory and classify.** Map every assertion in
  `Test_RuntimeSystemBundles.cpp` to one of:
  promoted runtime bundle coverage, promoted graphics/runtime lifecycle coverage,
  or legacy-only feature-catalog behavior to retire.
- **Slice B — Fill retained coverage.** If any retained ordering or lifecycle
  contract is not already covered, add promoted contract tests under the owning
  layer (`runtime` for composition/activation, `graphics` for renderer-side
  lifecycle systems) without importing legacy modules.
- **Slice C — Retire the legacy test.** Delete or rename the old test only after
  Slice A/B produce an explicit promoted coverage map; update `tests/CMakeLists.txt`
  and migration docs with the reduced `Core`/`ECS`/`Runtime` consumer counts.

## Required changes
- [ ] Inventory the six `RuntimeSystemBundles` test cases and classify their
      retained/retired status.
- [ ] Compare the retained cases against
      `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` and existing
      promoted graphics/runtime lifecycle tests.
- [ ] Add or extend promoted tests for any retained behavior that lacks current
      coverage.
- [ ] Delete or migrate `tests/unit/runtime/Test_RuntimeSystemBundles.cpp`.
- [ ] Update `tests/CMakeLists.txt` and the relevant `LEGACY-005`,
      `LEGACY-006`, `LEGACY-010`, and `LEGACY-012` blocker notes.

## Tests
- [ ] Build affected runtime/graphics/ECS test targets.
- [ ] Run focused CTest filters for promoted runtime bundle and any promoted
      graphics lifecycle coverage added by this task.
- [ ] Confirm `ctest --test-dir build/ci -N -R 'RuntimeSystemBundles'` reports
      zero tests after retirement, or only promoted renamed tests if the file is
      migrated.
- [ ] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [ ] Update `docs/migration/legacy-removal-audit.md` with the reduced
      consumer counts.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` if any legacy
      `Runtime.SystemBundles` behavior is formally retired rather than
      migrated.
- [ ] Update the relevant backlog deletion tasks with current blocker notes.
- [ ] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] No test imports `Runtime.SystemBundles`.
- [ ] No test imports legacy `Core.SystemFeatureCatalog` for runtime bundle
      ordering.
- [ ] Any retained runtime/graphics lifecycle ordering contract has promoted
      coverage under the owning layer.
- [ ] Remaining legacy deletion-task consumer counts are current and
      reproducible by grep.

## Verification
```bash
git grep -nE '^\s*(export\s+)?import\s+(Core|ECS|Runtime\.SystemBundles)\b' -- tests/unit/runtime/Test_RuntimeSystemBundles.cpp
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests IntrinsicECSTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEcsSystemBundle|RuntimeSystemBundles|PropertySetDirtySync|PrimitiveBVH|GraphLifecycle|MeshRendererLifecycle|PointCloudLifecycle|MeshViewLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R 'RuntimeSystemBundles'
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
- Adding compatibility re-exports from promoted modules to legacy
  `Runtime.SystemBundles` or `Core.SystemFeatureCatalog`.
- Promoting legacy graphics lifecycle systems into `src/ecs/`.
- Changing runtime/graphics ownership boundaries to make the old test compile.
- Deleting the test before retained-vs-retired behavior is documented.

## Maturity
- Target: `CPUContracted` for consumer-test migration/retirement.
- No `Operational` follow-up is owed for the consumer-test migration itself.
- If Slice A finds retained backend-facing graphics lifecycle behavior that
  needs opt-in Vulkan proof, split that behavior into a named follow-up before
  retiring this task.
