# UI-004 — Sandbox EditorUI K-Means execution command seam

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: `SandboxEditorUi` now exposes a promoted CPU K-Means command seam
  for mesh vertices, graph nodes, and point-cloud points backed by
  `Geometry.KMeans`; results publish legacy-compatible label/color properties
  into promoted `GeometrySources` and mark vertex attributes dirty.

## Goal
- Promote the first legacy geometry-processing execution path by adding a
  runtime-owned CPU K-Means command seam to `Extrinsic.Runtime.SandboxEditorUi`
  that operates on promoted `ECS::Components::GeometrySources` mesh vertices,
  graph nodes, and point-cloud points.

## Non-goals
- Do not revive legacy `Runtime.PointCloudKMeans`, legacy
  `Runtime.EditorUI`, or legacy ECS data components.
- Do not implement CUDA/asynchronous K-Means scheduling, persistent centroid
  entities, or worker-thread completion polling in this slice.
- Do not port remeshing, simplification, smoothing, subdivision, shortest-path,
  registration, or point-cloud filtering execution widgets here.
- Do not mutate mesh topology or allocate graphics/RHI resources from UI.

## Context
- Owner/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`; runtime may
  compose ECS `GeometrySources`, geometry algorithms, and editor command DTOs.
- `UI-003` promoted geometry-processing discovery and explicitly left execution
  command surfaces as future work. This slice closes the first execution seam
  for K-Means because the promoted `Geometry.KMeans` CPU algorithm already
  exists and can publish labels/colors into `Geometry::PropertySet`s without
  touching graphics/RHI.
- Legacy source behavior to preserve at this maturity: K-Means can target mesh
  vertices, graph vertices/nodes, and point-cloud points; it publishes stable
  label/color properties and records deterministic run diagnostics. CUDA,
  retained centroid render entities, and legacy visualization settings are
  excluded.

## Required changes
- [x] Add promoted K-Means command/result DTOs and pure helper functions to
      `Extrinsic.Runtime.SandboxEditorUi`.
- [x] Execute `Geometry::KMeans::Cluster` synchronously on positions read from
      promoted `GeometrySources`.
- [x] Publish label and color properties into the selected domain's
      `PropertySet` using stable names (`v:kmeans_label`,
      `v:kmeans_label_f`, `v:kmeans_color`, `p:kmeans_label`,
      `p:kmeans_color`).
- [x] Mark vertex attributes dirty after successful publication.
- [x] Wire the existing Processing ImGui window to expose CPU K-Means controls
      and invoke the promoted command seam.

## Tests
- [x] Add `contract;runtime` tests proving mesh, graph, and point-cloud
      K-Means commands publish labels/colors and dirty the selected entity.
- [x] Add fail-closed tests for missing scene, stale entity, unsupported domain,
      invalid parameters, and non-finite/missing positions.
- [x] Focused runtime contract tests pass under the CPU-supported gate.

## Docs
- [x] Update `tasks/backlog/ui/README.md` to record the promoted execution seam.
- [x] Update `src/runtime/README.md` to describe K-Means command ownership and
      the remaining non-goals.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` so the legacy
      geometry-processing gap reflects promoted CPU K-Means execution.
- [x] Regenerate `docs/api/generated/module_inventory.md` because the runtime
      module public surface changes.

## Acceptance criteria
- [x] Promoted Sandbox EditorUI can run CPU K-Means for selected mesh, graph,
      and point-cloud `GeometrySources` entities without importing legacy
      modules.
- [x] K-Means output is visible to existing property/visualization paths via
      deterministic property names.
- [x] Invalid selections and invalid inputs fail closed with explicit command
      status and error codes.
- [x] No legacy module import is added to promoted `src/`.

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

- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  regenerated the public module inventory with 479 modules.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` passed.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 21/21 tests.
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
- Importing any `Runtime.*`, `ECS`, `Graphics.*`, or `RHI.*` legacy module from
  promoted code.
- Adding graphics/RHI ownership, GPU buffers, or asset-service mutation to the
  editor command surface.
- Folding unrelated geometry-processing algorithms into this K-Means slice.

## Maturity
- Target: `CPUContracted`.
- This slice proves the promoted command contract and CPU algorithm execution.
  CUDA/asynchronous scheduling, centroid render entities, and broader algorithm
  execution remain explicit non-goals and require follow-up tasks if needed.
