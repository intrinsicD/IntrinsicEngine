---
id: UI-022
theme: F
depends_on: [GEOM-026]
maturity_target: CPUContracted
---
# UI-022 — Sandbox EditorUI vertex-normal recompute windows

## Goal
- Add a Sandbox EditorUI method window under `Mesh > Processing > Vertices > Normals` that recomputes normals for the selected mesh entity and publishes the result to canonical `v:normal` data.
- Extend the same menu/API pattern to `Graph > Processing > Vertices > Normals` and `PointCloud > Processing > Vertices > Normals` by consuming the retired geometry-owned graph and point-cloud normal modules.

## Non-goals
- No geometry kernel implementation; `GEOM-026` owns mesh, graph, and point-cloud normal recomputation.
- No new methods under edges, halfedges, or faces.
- No topology mutation, mesh repair, smoothing, tangent generation, normal-map baking, or texture baking.
- No GPU/RHI allocation, renderer feature work, shader work, or persistent generated asset.
- No async/streaming execution path unless a later value-gated runtime task accepts it.
- No `Runtime.Engine.cppm` public API expansion unless the existing `SandboxEditorContext` and command-history seams cannot express the workflow.

## Context
- Status: backlog.
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`; runtime may compose ECS `GeometrySources`, geometry algorithms, and editor command/history seams, while geometry owns the algorithms.
- The current editor menu shape has domain menus with a `Processing` submenu and element entries (`Vertices`, `Edges`, `Faces`, `Halfedges`). This task turns each vertex entry into the first method-bearing submenu leaf named `Normals`; the opened window owns the action button label (`Recompute`), not the menu leaf.
- `SandboxEditorGeometryProcessingAlgorithm::NormalEstimation` advertises point-cloud discovery from the existing promoted surface and now advertises mesh vertex discovery for the halfedge-mesh recompute slice. Graph and point-cloud command windows should now consume the retired `GEOM-026` modules rather than remain feature-gated on missing geometry kernels.
- Retired `GEOM-026` exposes domain-owned geometry modules:
  `Geometry.HalfedgeMesh.Vertices.Normals`,
  `Geometry.Graph.Vertex.Normals`, and
  `Geometry.PointCloud.Normals`. Runtime should call those modules by domain
  rather than route through a broad UI-owned or legacy normal-estimation switch.
- Canonical target property is `Extrinsic::ECS::Components::GeometrySources::PropertyNames::kNormal` (`v:normal`) for mesh vertices, graph nodes, and point-cloud points. Mesh and point-cloud targets use `GeometrySources::Vertices`; graph targets use `GeometrySources::Nodes`.
- `src/runtime/Runtime.Engine.cppm` is the composition root and already exposes editor command history. Prefer keeping normal recompute commands in the Sandbox EditorUI command surface unless implementation proves a broader runtime command API is needed.
- GPU synchronization follows the promoted dirty-tag contract: the editor command publishes CPU `v:normal`, stamps `DirtyVertexAttributes`, and render extraction/main-loop residency repacks and uploads the affected geometry on the next extraction opportunity. The UI command must not directly call renderer/RHI upload APIs or launch a GPU update task. If mesh extraction does not currently include `DirtyVertexAttributes` in its mesh-residency dirty set, extend that consumer-side dirty set rather than compensating with a UI-authored `GpuDirty` broad tag.

## UI plan
- `Mesh > Processing > Vertices > Normals` opens a mesh vertex-normal window for the selected mesh entity. Graph and point-cloud windows follow the same leaf name and consume the retired `GEOM-026` graph/point-cloud modules.
- The mesh window exposes the method controls needed by `Geometry.HalfedgeMesh.Vertices.Normals`: averaging mode (`UniformFace`, `AreaWeighted`, `AngleWeighted`, `MaxWeighted`), fallback normal, epsilon/degeneracy tolerance if surfaced by the runtime DTO, and a fixed canonical output target of `v:normal`.
- The primary command is a single `Recompute` button. It is enabled only for a live selected mesh entity with writable mesh `GeometrySources`; unsupported domains show deterministic diagnostics rather than hidden failure.
- The result panel reports the geometry-module counters without requiring a graphics backend: status, written vertex count, valid normal count, processed face count, degenerate/non-finite/invalid-topology face counts, degenerate corner count, fallback count, skipped deleted slots, and fallback-repair state.
- Advanced output-property selection is deferred. This UI writes canonical `v:normal` only so downstream rendering, export, and analysis agree on the same property.

## Required changes
- [x] Extend the processing menu model so mesh `Vertices` can expose method entries, with `Normals` present for mesh vertices and no methods yet under mesh edges or faces.
- [x] Add Sandbox EditorUI state/model data for the mesh normal-recompute window, including selected entity, supported settings, last result, and fail-closed diagnostics.
- [ ] Add per-domain settings: mesh averaging scheme (`UniformFace`, `AreaWeighted`, `AngleWeighted`, `MaxWeighted`), graph connectivity/fallback/orientation settings, and point-cloud KDTree neighborhood settings (`k`, optional radius, minimum neighbors, orientation/fallback).
- [x] Add mesh settings for the face-normal averaging scheme (`UniformFace`, `AreaWeighted`, `AngleWeighted`, `MaxWeighted`) plus fallback normal; graph and point-cloud settings remain pending with their domain modules.
- [x] Add a runtime-owned mesh command DTO/result and `ApplySandboxEditorMeshVertexNormalsCommand(...)` helper that validates selected entity/domain, calls the matching `GEOM-026` mesh module, and publishes count-matched normals as `v:normal`.
- [x] Mark the existing dirty state needed for mesh normal-property re-upload/extraction: stamp `DirtyVertexAttributes` after successful CPU publication, ensure mesh extraction consumes that tag as a deferred reupload trigger, and reserve `GpuDirty` for broad fallback cases rather than stamping it from the UI command.
- [x] Record editor dirty state through `EditorCommandHistory` with a specific normal-recompute command label.
- [x] Surface mesh result diagnostics in the processing window without requiring graphics/Vulkan availability.

## Tests
- [x] Extend `tests/contract/runtime/Test.SandboxEditorUi.cpp` so menu contract coverage proves `Normals` appears under the mesh vertex processing submenu only for this slice.
- [x] Add command tests proving mesh recomputation writes finite count-matched `v:normal` values to the mesh vertex `GeometrySources` property set for all mesh weighting modes.
- [ ] Add graph and point-cloud command tests that consume the retired geometry-owned recompute modules.
- [x] Add tests for selected-entity/domain validation and typed `v:normal` publication conflicts.
- [x] Add tests proving dirty tags and editor dirty state are updated after a successful recompute, and that mesh normal recompute relies on `DirtyVertexAttributes`-driven deferred extraction rather than direct renderer/RHI calls.
- [ ] Keep existing K-Means and processing capability tests passing.

## Docs
- [x] Update `src/runtime/README.md` with the normal-recompute editor workflow, the geometry/runtime ownership split, and the deferred dirty-tag GPU synchronization contract.
- [ ] Update [`tasks/backlog/ui/README.md`](README.md) and this task if scope changes before promotion.
- [ ] Update migration/parity docs only if implementation claims replacement of a legacy normal-estimation workflow.
- [x] Regenerate `docs/api/generated/module_inventory.md` if runtime module surfaces change.

## Acceptance criteria
- [x] The mesh UI path is discoverable as `Mesh > Processing > Vertices > Normals`.
- [x] The mesh command calls geometry-owned normal recomputation and publishes `v:normal` without UI-owned algorithms.
- [x] Mesh settings expose the face-normal averaging scheme and fallback normal.
- [ ] Graph and point-cloud normal windows/commands consume the retired geometry-owned modules.
- [x] Failure cases report deterministic command statuses/diagnostics and do not mutate unrelated properties.
- [x] Focused runtime contract tests and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests
./build/ci/bin/IntrinsicGeometryTests --gtest_filter='HalfedgeMeshVertexNormals.*'
./build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='SandboxEditorUi.*MeshVertexNormals*:SandboxEditorUi.*Processing*:MeshGeometryExtractionDirtyTag.*:MeshGeometryExtraction.MultipleDirtyTagsCoalesceIntoSingleReupload'
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicGeometryTests|IntrinsicRuntimeContractTests' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing normal-estimation algorithms in UI/runtime instead of consuming `GEOM-026`.
- Reaching from geometry into runtime/ECS/UI to satisfy this workflow.
- Adding renderer/RHI dependencies or Vulkan-only behavior to the editor command.

## Maturity
- Target: `CPUContracted`; no `Operational` Vulkan/GPU follow-up is owed.
- The endpoint is a CPU/null-safe editor command and UI contract. No `Operational` Vulkan/GPU follow-up is owed unless a later task requires backend-specific visual proof.
