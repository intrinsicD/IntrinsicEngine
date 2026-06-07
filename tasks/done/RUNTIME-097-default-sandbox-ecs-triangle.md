# RUNTIME-097 — Default sandbox ECS-authored white triangle

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: The default reference/sandbox triangle is now an ordinary
  ECS-authored mesh-domain `GeometrySources` entity with durable stable
  identity, selectable state, render-surface hints, and white visualization
  intent. Runtime extraction observes it through the mesh residency path; the
  procedural geometry path remains available only for explicit procedural
  fixtures and callers.

## Goal
- Make the default `ExtrinsicSandbox` triangle a normal ECS entity authored
  through promoted `GeometrySources` mesh data and runtime/editor-visible
  components, so it is selected, inspected, extracted, and rendered like
  file-loaded mesh objects.

## Non-goals
- No new app-layer rendering shortcuts in `src/app/Sandbox`.
- No deletion of the procedural-geometry modules or their standalone tests;
  only the default sandbox/reference triangle stops depending on that path.
- No broad asset-import, scene-serialization, or material-system redesign.
- No new graph or point-cloud demo entities in this task.

## Context
- Owner/layer: `runtime` reference-scene/bootstrap composition. `Sandbox::App`
  must remain a runtime-only consumer that attaches `SandboxEditorUi`; it must
  not import ECS, graphics, RHI, assets, or platform implementation details.
- The current default reference triangle is authored by
  `Extrinsic.Runtime.ReferenceScene::TriangleProvider` in
  `src/runtime/Runtime.ReferenceScene.cpp`. It creates `ReferenceTriangle` with
  `RenderSurface` plus `ProceduralGeometryRef{Triangle}`.
- The requested stop-state is a visible white triangle that resides in the ECS
  with the same component family as loaded mesh entities: metadata, transform,
  hierarchy/world transform, stable identity, selectable state, render hints,
  visualization/material color intent, and mesh `GeometrySources` data.
- This task should preserve the existing runtime-owned
  `ReferenceSceneRegistry`/`TriangleProvider` seam unless implementation
  inspection proves a smaller provider name split is required.

## Required changes
- [x] Update the default triangle provider to author a mesh-domain
      `GeometrySources` entity with `Vertices`, `Edges`, `Halfedges`, `Faces`,
      and `HasMeshTopology` for one finite triangle.
- [x] Stamp the triangle with the runtime/editor-facing components needed to
      behave like loaded geometry: `MetaData`, `Transform::Component`,
      `Transform::WorldMatrix`, `Hierarchy::Component`, durable `StableId`,
      `Selection::SelectableTag`, `Graphics::Components::RenderSurface`, and a
      white appearance contract (`VisualizationConfig` uniform white or the
      existing material/tint component if that is the renderer-owned canonical
      surface at implementation time).
- [x] Remove `ProceduralGeometryRef` from the default sandbox/reference
      triangle entity while leaving procedural extraction support available for
      explicit procedural tests and future callers.
- [x] Ensure runtime extraction routes the default triangle through the mesh
      `GeometrySources` residency lane (`RUNTIME-085`) instead of the
      procedural-geometry lane.
- [x] Ensure `SandboxEditorUi` panel models list and inspect the default
      triangle as a mesh-domain selectable entity without any special-case UI
      path.

## Tests
- [x] Update `tests/contract/runtime/Test.RuntimeReferenceScene.cpp` so
      `TriangleProviderContract` asserts the new ECS component set, mesh-domain
      `GeometrySources` counts, durable stable id, selectable tag, and white
      appearance component.
- [x] Update reference-scene extraction coverage so the default triangle reports
      one renderable candidate, one allocated instance, one mesh geometry upload,
      and zero procedural geometry uploads/reuse hits.
- [x] Add or update `SandboxEditorUi`/runtime acceptance coverage showing the
      default reference triangle appears in the hierarchy, has `Domain::Mesh`,
      and can be selected through `SelectionController`.
- [x] Preserve existing standalone procedural-geometry tests by moving any
      default-triangle-specific procedural assertions to explicit procedural
      fixtures.

## Docs
- [x] Update `src/runtime/README.md` to describe the default triangle as
      ECS-authored mesh `GeometrySources`, not `ProceduralGeometryRef`.
- [x] Update `src/app/Sandbox/README.md` with the default sandbox scene behavior
      and the fact that `Sandbox::App` still only attaches runtime-owned
      `SandboxEditorUi`.
- [x] Update `tasks/backlog/runtime/README.md` and
      `tasks/backlog/README.md` status links when the task is promoted or
      retired.
- [x] Refresh `docs/api/generated/module_inventory.md` only if public module
      surfaces change.

## Acceptance criteria
- [x] Launching the default reference/sandbox configuration creates exactly one
      white triangle entity in ECS with the required mesh-domain component set.
- [x] The entity is selectable through `SelectionController` and appears in
      `SandboxEditorUi` hierarchy/inspector models as ordinary mesh geometry.
- [x] Runtime extraction uses the mesh `GeometrySources` residency path for the
      default triangle and does not use `ProceduralGeometryRef` for that entity.
- [x] `Sandbox::App` remains policy-light and imports runtime only.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R 'ReferenceScene|SandboxEditorUi|RuntimeSandboxAcceptance|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

### Completion session record (2026-06-07)

- `cmake --preset ci` configured successfully.
- `cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox` built
  successfully.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests ExtrinsicSandbox`
  built successfully after the warning fix in `Runtime.ReferenceScene.cpp`.
- `ctest --test-dir build/ci --output-on-failure -R 'ReferenceScene|SandboxEditorUi|RuntimeSandboxAcceptance|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 51/51 tests.
- Final combined verification for the retired `RUNTIME-097` + `UI-002` batch:
  `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests ExtrinsicSandbox`
  built successfully, `cmake --build --preset ci --target IntrinsicTests`
  built successfully,
  `ctest --test-dir build/ci --output-on-failure -R 'ReferenceScene|SandboxEditorUi|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 56/56 tests,
  `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 376/376 tests, and
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 2819/2819 tests.
- Final structural checks passed:
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/check_task_state_links.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`, and
  `git diff --check`.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  regenerated 479 modules with no content diff.

## Forbidden changes
- Importing lower engine layers directly into `src/app/Sandbox`.
- Reintroducing legacy graphics/editor paths to make the triangle visible.
- Moving mesh residency ownership from runtime extraction into ECS or graphics.
- Deleting procedural-geometry infrastructure outside the explicitly scoped
  default-triangle source swap.

## Maturity
- Target: `CPUContracted` in the default CPU/null gate, with existing
  RUNTIME-095/`ci-vulkan` sandbox smoke remaining the operational proof for
  Vulkan-capable hosts. No `Operational` follow-up is owed unless the
  implementation invalidates that smoke, in which case updating the smoke is
  part of this task.
