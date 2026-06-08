# BUG-022 — Sandbox non-manifold OBJ imports are renderable

## Status
- Status: done.
- Maturity: `CPUContracted`.
- Completion date: 2026-06-08.
- PR/commit: pending local commit.

## Goal
Make dropped/imported OBJ triangle-soup meshes such as `suzanne.obj` materialize as renderable sandbox mesh entities even when strict shared-topology halfedge conversion rejects non-manifold or inconsistently wound edges.

## Non-goals
- Do not weaken `Geometry.Mesh.Conversion::ToHalfedgeMesh` strict validation for geometry algorithms.
- Do not claim imported fallback meshes have valid shared adjacency for topology-sensitive methods.
- Do not bypass promoted Vulkan fail-closed behavior or suppress validation-layer diagnostics.

## Context
The drag/drop trace reaches `Runtime::Engine::DecodeGeometryImport`, where OBJ data is decoded into `MeshIOResult` and then converted to `HalfedgeMesh` before ECS materialization. The imported mesh entity uses the same render-critical component set as `ReferenceTriangle` once it exists: `CreateDefault` components, `SelectableTag`, `RenderSurface{Vertex}`, `VisualizationConfig`, and mesh-domain `GeometrySources`. The observed Suzanne OBJ contains non-manifold edge topology, so the strict conversion can fail with `InvalidFormat` before any ECS entity is created. Runtime import owns a renderability fallback; geometry conversion remains strict.

The Vulkan validation log is a separate renderer gate: current frame-recipe tests show the default recipe no longer emits `SceneDepth` color-attachment barriers in this checkout, but the sandbox cannot display any mesh while the promoted Vulkan device stays fail-closed.

## Required changes
- [x] Add a runtime-only disconnected render fallback after strict mesh conversion fails for renderable topology diagnostics.
- [x] Preserve strict failure for invalid indices, missing data, degenerate faces, and unsupported non-topology diagnostics.
- [x] Add a regression test that imports a minimal non-manifold OBJ and compares its render-critical components to the default triangle lane.
- [x] Keep the default-recipe texture-usage compatibility guard that catches depth textures receiving color barriers.

## Tests
- [x] Build `IntrinsicTests` with the `ci` preset.
- [x] Run focused runtime import/drop tests.
- [x] Run focused default frame-recipe barrier tests.

## Docs
- [x] Update runtime notes to document the disconnected render-only fallback.

## Acceptance criteria
- [x] Non-manifold OBJ import returns a successful mesh import result.
- [x] The imported mesh entity carries `MetaData`, `Hierarchy`, local/world transform, `SelectableTag`, `RenderSurface`, `VisualizationConfig`, and mesh `GeometrySources`.
- [x] Runtime render extraction sees one mesh candidate and uploads/binds one mesh geometry without pack failures.
- [x] Default frame recipe barriers remain compatible with texture usage capabilities.

## Verification
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R 'EngineImportFacadeMaterializesNonManifoldObjAsRenderableMesh|EngineImportFacadeMaterializesStandaloneGeometryDomains|DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'DefaultRecipeBarriersRespectTextureUsageCapabilities|DefaultRecipeDoesNotDepthTransitionColorResources|DefaultRecipeCompiledGraphHasNoValidationFindings' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## Forbidden changes
- Do not revert or rewrite the existing UI-007/BUG-021 drag/drop changes.
- Do not use destructive git commands.
- Do not route live asset-service or runtime knowledge into lower graphics/geometry layers.

## Maturity
- Target: `CPUContracted` for runtime import/render-extraction behavior. `Operational` visibility still depends on the promoted Vulkan device passing its backend validation gate.
