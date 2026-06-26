---
id: UI-026
theme: F
depends_on: [GEOM-040]
maturity_target: CPUContracted
---
# UI-026 — Sandbox EditorUI curvature analysis window and principal-direction field

## Goal
- Add a Sandbox EditorUI window under `Mesh > Processing > Curvature` that computes curvature (mean `H`, Gaussian `K`, principal magnitudes `kappa1`/`kappa2`, and the `GEOM-040` principal directions) on the selected mesh entity and publishes the scalar fields plus principal-direction vector fields as canonical `GeometrySources` properties.
- Extend `src/runtime/Visualization/Runtime.VisualizationAdapters.*` so a scalar color map and principal-direction line glyphs can be built from the published canonical properties, with a visualization toggle to color by a selected scalar and draw the direction glyphs.

## Non-goals
- No geometry kernel implementation; `GEOM-040` owns the curvature algorithm and principal-direction computation.
- No GPU/RHI/compute path; curvature is computed on the CPU only and synchronized through the existing promoted dirty-tag contract.
- No new persistent or generated asset, and no on-disk caching of curvature fields.
- No `Runtime.Engine.cppm` public API expansion unless the existing `SandboxEditorContext`, command-history, and visualization-adapter seams cannot express the workflow.
- No topology mutation, remeshing, smoothing, or repair; the command reads geometry and writes derived per-vertex properties only.

## Context
- Status: backlog.
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*` for the command window, and `src/runtime/Visualization/Runtime.VisualizationAdapters.*` for the colormap/glyph builders. Runtime composes the geometry curvature algorithm from `GEOM-040`; geometry owns the algorithm and never depends on runtime.
- This task mirrors `UI-022`'s publish-to-canonical-property pattern: the menu leaf opens a window that owns the action-button label (`Compute`), the command validates a live selected mesh entity with writable mesh `GeometrySources`, calls the geometry-owned algorithm, publishes count-matched per-vertex properties, stamps the existing `DirtyVertexAttributes` tag, and records editor dirty state through `EditorCommandHistory` with a specific curvature-compute label.
- Canonical output property names live next to `Extrinsic::ECS::Components::GeometrySources::PropertyNames` (`kNormal` = `v:normal` is the established precedent). This task adds the curvature names as canonical `v:`-prefixed vertex properties: `v:mean_curvature`, `v:gaussian_curvature`, `v:principal_dir1`, `v:principal_dir2`. Mesh targets use `GeometrySources::Vertices`. If `kappa1`/`kappa2` magnitudes are surfaced they are written as `v:kappa1`/`v:kappa2`; otherwise they are derivable downstream from `H`/`K` and are not required outputs of this slice.
- Visualization already exposes the building blocks in `Runtime.VisualizationAdapters.cppm`: `Graphics::ScalarAttributePacket` (scalar color map input), `Graphics::VectorFieldOverlayPacket` (line-glyph overlay input), `Graphics::Colormap::Type`, and a `PropertyScalarAdapter` that reads a `Geometry::ConstPropertySet`. This task adds a principal-direction glyph builder over the same `ConstPropertySet` seam.
- GPU synchronization follows the promoted dirty-tag contract: the editor command publishes CPU properties, stamps `DirtyVertexAttributes`, and render extraction/main-loop residency repacks and uploads on the next extraction opportunity. The UI command must not call renderer/RHI upload APIs or launch a GPU task.

## Slice plan
- [ ] Slice 1 (CPU scalar curvature): `Mesh > Processing > Curvature` window with curvature-type selection and a `Compute` action that publishes `v:mean_curvature` and `v:gaussian_curvature`, plus the scalar colormap visualization toggle. This slice can land on the scalar-only surface of `GEOM-040`.
- [ ] Slice 2 (principal directions): publish `v:principal_dir1`/`v:principal_dir2` and the principal-direction line-glyph overlay; gate this slice on `GEOM-040` principal-direction availability so the window and adapters degrade deterministically (scalars only) when directions are absent.

## Required changes
- [ ] Add the canonical curvature property names beside `Extrinsic::ECS::Components::GeometrySources::PropertyNames` in `src/ecs/Components/ECS.Component.GeometrySources.cppm`: `kMeanCurvature` (`v:mean_curvature`), `kGaussianCurvature` (`v:gaussian_curvature`), `kPrincipalDir1` (`v:principal_dir1`), `kPrincipalDir2` (`v:principal_dir2`).
- [ ] Extend the processing menu model in `src/runtime/Editor/Runtime.SandboxEditorUi.cppm` so mesh `Processing` exposes a `Curvature` method leaf, and no curvature methods appear under graph or point-cloud domains in this slice.
- [ ] Add Sandbox EditorUI state/model data for the curvature window in `Runtime.SandboxEditorUi.cppm`/`.cpp`: selected entity, curvature-type selection (`Mean`, `Gaussian`, `Principal`, or an explicit "all"), a directions-available flag derived from `GEOM-040`, last result counters, and fail-closed diagnostics.
- [ ] Add a runtime-owned curvature command DTO/result and an `ApplySandboxEditorMeshCurvatureCommand(...)` helper in `Runtime.SandboxEditorUi.cpp` that validates the selected entity/domain and writable mesh `GeometrySources`, calls the `GEOM-040` curvature algorithm, and publishes count-matched `v:mean_curvature`/`v:gaussian_curvature` (always) and `v:principal_dir1`/`v:principal_dir2` (only when `GEOM-040` directions are available).
- [ ] After successful CPU publication, stamp the existing `DirtyVertexAttributes` tag (do not introduce a new tag and do not stamp `GpuDirty` from the UI command) and record the command through `EditorCommandHistory` with a curvature-specific label.
- [ ] In `src/runtime/Visualization/Runtime.VisualizationAdapters.cppm`/`.cpp`, add a curvature scalar adapter path that maps a selected curvature scalar property to a `Graphics::ScalarAttributePacket` via the existing `PropertyScalarAdapter`/`Graphics::Colormap::Type` seam.
- [ ] In `Runtime.VisualizationAdapters.*`, add a principal-direction glyph builder that reads `v:principal_dir1`/`v:principal_dir2` from a `Geometry::ConstPropertySet` and emits `Graphics::VectorFieldOverlayPacket` line glyphs; when either direction property is absent or count-mismatched, the builder produces an empty/scalar-only overlay and a deterministic diagnostic without entering an invalid state.
- [ ] Add a visualization toggle (color-by-selected-scalar and draw-principal-direction-glyphs) wired so the glyph toggle is inert when directions are not published.
- [ ] Surface curvature result diagnostics (status, written vertex count, finite/valid scalar count, degenerate/non-finite vertex count, directions-published flag) in the window without requiring graphics/Vulkan availability.

## Tests
- [ ] Extend `tests/contract/runtime/Test.SandboxEditorUi.cpp` so menu contract coverage proves `Curvature` appears under the mesh `Processing` submenu only for this slice.
- [ ] Add an editor contract test that `Compute` discovers an eligible mesh entity, runs the `GEOM-040` curvature algorithm, and writes finite count-matched `v:mean_curvature`/`v:gaussian_curvature` to the mesh vertex `GeometrySources` property set.
- [ ] Add an editor contract test that, when `GEOM-040` directions are available, `Compute` also publishes count-matched `v:principal_dir1`/`v:principal_dir2`, and that when directions are unavailable the command still succeeds with scalars only and a deterministic "directions not published" diagnostic.
- [ ] Add tests for selected-entity/domain validation (no mesh selected, non-mesh domain, non-writable sources) producing deterministic statuses without mutating unrelated properties.
- [ ] Add tests proving `DirtyVertexAttributes` and editor dirty state are updated after a successful compute, with no direct renderer/RHI calls.
- [ ] Add a visualization-adapter contract test that the glyph/colormap builder consumes the published curvature properties, and that the principal-direction glyph builder does not enter an invalid state (emits empty/scalar-only overlay plus diagnostic) when `v:principal_dir1`/`v:principal_dir2` are absent or count-mismatched.

## Docs
- [ ] Update `src/runtime/README.md` with the curvature-analysis editor workflow, the geometry/runtime ownership split (runtime composes `GEOM-040`), the canonical curvature property names, the directions-gated visualization behavior, and the deferred dirty-tag GPU synchronization contract.
- [ ] Update `src/ecs/Components/README.md` to list the new canonical curvature property names under `PropertyNames`.
- [ ] Update [`tasks/backlog/ui/README.md`](README.md) and this task if scope changes before promotion.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if runtime/ECS module surfaces change.

## Acceptance criteria
- [ ] The curvature UI path is discoverable as `Mesh > Processing > Curvature` with a curvature-type selection and a single `Compute` action.
- [ ] `Compute` calls the `GEOM-040`-owned curvature algorithm and publishes canonical `v:mean_curvature`/`v:gaussian_curvature` (and `v:principal_dir1`/`v:principal_dir2` when directions are available) without any UI/runtime-owned curvature math.
- [ ] When `GEOM-040` directions are unavailable, the command succeeds with scalars only and reports a deterministic diagnostic; the direction-glyph toggle is inert.
- [ ] The visualization adapters build a scalar colormap from a selected published curvature scalar and principal-direction line glyphs from the published direction properties, never entering an invalid state when directions are absent or count-mismatched.
- [ ] Failure cases (no selection, non-mesh domain, non-writable sources) report deterministic command statuses and do not mutate unrelated properties.
- [ ] After a successful compute, `DirtyVertexAttributes` and editor dirty state are updated with no direct renderer/RHI calls.
- [ ] Focused runtime contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|VisualizationAdapters' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing the curvature kernel or principal-direction computation in UI/runtime/visualization instead of consuming `GEOM-040`; geometry owns the algorithm.
- Reaching from geometry into runtime/ECS/UI/visualization to satisfy this workflow, or introducing any `geometry -> runtime` (or geometry -> assets/graphics/rhi/ecs/app) dependency.
- Adding renderer/RHI dependencies, GPU compute, or Vulkan-only behavior to the editor command or the property-publication path.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted`; the endpoint is a CPU/null-safe editor command plus the visualization colormap/glyph builder contract.
- No `Operational` Vulkan/GPU follow-up is owed beyond this contract unless a later task adds an interactive smoke or backend-specific visual proof.

- Closure: no `Operational` follow-up is owed for this task.
