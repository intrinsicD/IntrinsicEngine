# UI-005 — Sandbox EditorUI visualization property presets

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: `SandboxEditorUi` now enumerates visualization-eligible promoted
  mesh, graph, and point-cloud `GeometrySources` properties and routes scalar,
  isoline, and `glm::vec4` color-buffer preset buttons through the
  runtime-owned `VisualizationConfig` command seam.

## Goal
- Reimplement the legacy property-enumeration-to-visualization UI seam for promoted `GeometrySources`: selected mesh, graph, and point-cloud windows list visualization-eligible properties and can apply scalar, isoline, and color-buffer visualization presets through runtime-owned editor commands.

## Non-goals
- No generic GPU residency/upload for arbitrary `Geometry::PropertySet` arrays; current promoted packers only prove geometry positions/topology residency.
- No persistent visualization-adapter registration from UI-owned borrowed `ConstPropertySet` views; adapter lifetime refresh/hot-reload is a later runtime task.
- No legacy `Graphics.PropertyEnumerator`, `Graphics.ColorMapper`, `Graphics.VectorFieldManager`, or `Graphics.IsolineExtractor` imports.
- No CUDA/asynchronous K-Means scheduling, centroid entities, or topology mutation.

## Context
- Owner/layer: `runtime` editor surface (`Extrinsic.Runtime.SandboxEditorUi`) consuming promoted ECS `GeometrySources` and producing runtime/editor commands.
- Legacy `Graphics.PropertyEnumerator` filtered internal/connectivity properties and surfaced scalar/color/vector candidates for editor visualization controls.
- `UI-004` now publishes K-Means labels/colors into promoted property sets, but users still need a promoted editor seam to discover and select those properties.
- `VisualizationConfig` is the currently promoted material/visualization handoff for scalar and RGBA color-buffer selection. Arbitrary property buffer GPU upload remains outside this slice.

## Required changes
- [x] Add promoted property metadata DTOs to `Extrinsic.Runtime.SandboxEditorUi`, including property domain, value kind, element count, and preset eligibility.
- [x] Enumerate visualization-eligible properties from selected mesh vertices/edges/faces, graph nodes/edges, and point-cloud points while filtering promoted internal/connectivity properties.
- [x] Add a fail-closed `SandboxEditorVisualizationPropertyCommand` that validates selected entity/domain/property/type and applies the correct `VisualizationConfig` for scalar, isoline, or RGBA color-buffer presets.
- [x] Extend the Mesh/Graph/PointCloud ImGui visualization windows to render discovered property rows and issue the new preset command instead of only hard-coded `Scalar: scalars` buttons.

## Tests
- [x] Add `contract;runtime` coverage for property enumeration over mesh, graph, and point-cloud `GeometrySources`, including K-Means output properties and internal-property filtering.
- [x] Add `contract;runtime` coverage proving scalar, isoline, and color-buffer preset commands mutate `VisualizationConfig` correctly and fail closed for stale entities, wrong domains, missing properties, and unsupported types.

## Docs
- [x] Update `src/runtime/README.md` to record the promoted EditorUI property preset seam and its GPU-residency non-goal.
- [x] Update `tasks/backlog/ui/README.md` and `tasks/backlog/README.md` with the UI-005 status.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` to show property enumeration / visualization preset wiring is promoted while arbitrary property GPU residency remains open.
- [x] Regenerate `docs/api/generated/module_inventory.md` because the public module surface changes.

## Acceptance criteria
- [x] Selected domain windows expose deterministic visualization property metadata for current `GeometrySources` without importing legacy graphics modules.
- [x] Applying a scalar preset writes `VisualizationConfig::ColorSource::ScalarField` with the selected property and mapped domain.
- [x] Applying an isoline preset writes the same scalar field plus an explicit isoline count.
- [x] Applying a color preset writes the appropriate per-domain color-buffer source for `glm::vec4` properties such as `v:kmeans_color` / `p:kmeans_color`.
- [x] Invalid requests return a specific fail-closed status without mutating the scene.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

### Completion session record (2026-06-07)

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` passed
  after the `SandboxEditorUi` public surface and contract tests were updated.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 23/23 tests.
- Structural checks passed:
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`,
  `python3 tools/agents/validate_tasks.py --root tasks --strict`,
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/check_task_state_links.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`,
  and `git diff --check`.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding `src/graphics/*` imports of live ECS/runtime/editor state.
- Adding new renderer/RHI buffer-residency behavior under this UI task.

## Maturity
- Target: `CPUContracted`.
- This slice closes property enumeration and editor command contracts only. `Operational` visual proof for arbitrary property buffers is deferred to a later runtime/renderer property-buffer residency task; no `Operational` claim is made here.
