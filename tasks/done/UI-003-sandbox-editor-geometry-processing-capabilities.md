# UI-003 — Sandbox EditorUI geometry processing capabilities

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: `SandboxEditorUi` now exposes promoted, deterministic
  geometry-processing capability discovery for mesh, graph, and point-cloud
  `GeometrySources`; domain menus include read-only `Processing` windows, and
  the legacy `Test_EditorUI.cpp` consumer has been replaced by promoted runtime
  contract coverage.

## Goal
- Promote the legacy EditorUI geometry-processing capability discovery seam onto
  `Extrinsic.Runtime.SandboxEditorUi`, so the default sandbox can present
  mesh/graph/point-cloud processing affordances from promoted `GeometrySources`
  without importing legacy `Runtime.EditorUI`.

## Non-goals
- Do not port legacy ImGui algorithm execution widgets in this slice.
- Do not schedule K-Means, shortest path, remeshing, filtering, registration, or
  any geometry mutation from the promoted UI in this slice.
- Do not delete `src/legacy/EditorUI/` here; that remains the mechanical
  `LEGACY-007` task after consumer gates pass.
- Do not introduce app-owned editor state or graphics-owned ECS mutation.

## Context
- Owner/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`.
- Legacy source: `src/legacy/EditorUI/Runtime.EditorUI.cppm` and
  `Runtime.EditorUI.cpp` expose `GeometryProcessingDomain`,
  `GeometryProcessingAlgorithm`, capability discovery, supported-domain lookup,
  stable algorithm ordering, K-Means source-domain enumeration, and labels.
- Promoted data source: `Extrinsic.ECS.Components.GeometrySources` owns the
  canonical sandbox mesh, graph, and point-cloud CPU domains; promoted EditorUI
  should discover affordances from those components rather than legacy
  `ECS::Mesh::Data`, `ECS::Graph::Data`, or `ECS::PointCloud::Data`.
- This is a prerequisite-style migration for `LEGACY-007`: the old
  `tests/integration/app/Test_EditorUI.cpp` consumer should be replaced by
  promoted runtime contract tests before deleting the legacy EditorUI subtree.

## Required changes
- [x] Add promoted `SandboxEditorGeometryProcessing*` domain, algorithm,
      capability, entry, and model records to `Extrinsic.Runtime.SandboxEditorUi`.
- [x] Implement supported-domain lookup, stable entry resolution, K-Means source
      domain enumeration, and labels against promoted `GeometrySources`.
- [x] Wire processing capability models into the selected-entity inspector and
      Mesh/Graph/PointCloud domain windows.
- [x] Add an ImGui `Processing` submenu/window under each promoted domain slot
      that displays the discovered algorithm affordances; execution remains a
      disabled/future command surface.
- [x] Migrate the legacy EditorUI capability tests into the promoted runtime
      contract test target and remove the legacy `Test_EditorUI.cpp` build
      consumer.

## Tests
- [x] Contract tests cover supported-domain lookup, stable algorithm ordering,
      mesh/graph/point-cloud `GeometrySources` capability discovery, K-Means
      source-domain enumeration, domain-window processing models, and labels.
- [x] No GPU/Vulkan test is required; this is CPU-only model/UI command-surface
      behavior.

## Docs
- [x] Update `tasks/backlog/ui/README.md` and
      `docs/migration/nonlegacy-parity-matrix.md` to record the promoted
      capability-discovery seam.
- [x] Update `src/app/Sandbox/README.md` if the domain menu contents change.
- [x] Regenerate `docs/api/generated/module_inventory.md` because the
      `Extrinsic.Runtime.SandboxEditorUi` public surface changes.

## Acceptance criteria
- [x] Promoted `SandboxEditorUi` exposes deterministic geometry-processing
      capability discovery without importing legacy modules.
- [x] The default sandbox domain menus include a `Processing` window that reads
      the selected entity through runtime-owned models.
- [x] `tests/integration/app/Test_EditorUI.cpp` no longer imports legacy
      `Runtime.EditorUI`; equivalent promoted capability coverage lives in
      `tests/contract/runtime/Test.SandboxEditorUi.cpp`.
- [x] Focused runtime contract tests pass.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

### Completion session record (2026-06-07)

- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  regenerated the public module inventory with 480 modules and no content diff.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` completed
  successfully; the target was already current after dependency scanning.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 19/19 tests.
- Structural checks passed:
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/check_task_state_links.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`, and
  `git diff --check`.

## Forbidden changes
- Mixing this semantic UI/test migration with mechanical deletion of legacy
  `src/legacy/EditorUI/` or `src/legacy/Apps/`.
- Calling legacy geometry-processing execution modules from promoted UI.
- Storing runtime/editor command state in `src/app` or graphics-owned systems.

## Maturity
- Target: `CPUContracted`.
- This slice promotes deterministic capability discovery and UI model wiring;
  algorithm execution command surfaces remain future work and are not owed by
  this task.
