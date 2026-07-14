---
id: RUNTIME-106
theme: F
depends_on: [RUNTIME-085, RUNTIME-086, RUNTIME-087, RUNTIME-088, UI-013, BUG-028]
maturity_target: CPUContracted
---
# RUNTIME-106 — Render component domain composition

## Status
- State: `done` — retiring to `tasks/done/` on 2026-06-12 at maturity
  `CPUContracted`.
- Owner: agent. Branch: `main`.
- PR/commit: this retirement commit.
- Resolution: user-facing render composition is now ECS component based across
  promoted mesh, graph, and point-cloud domains. Mesh surface rendering is driven
  by `RenderSurface`; mesh edge and vertex lanes are driven by `RenderEdges` /
  `RenderPoints` and reuse the existing runtime primitive-view sidecars without
  requiring `RenderSurface`. Graph and point-cloud domains keep their existing
  runtime-owned residency implementations; graph lane-mask changes repack the
  shared graph handle, and point-cloud `RenderSurface` / `RenderEdges` requests
  now fail closed with diagnostics and release stale point residency.
- Compatibility: `MeshPrimitiveViewSettings` remains as a legacy DTO for older
  command/test seams, but the engine/UI compatibility paths translate to
  `RenderEdges` / `RenderPoints`; extraction no longer consumes it as authority.
- Verification: see the commands listed below.

## Goal
- Make geometric-domain entities render through real ECS component composition: `GeometrySources::BuildConstView(...)` selects the entity domain (`Mesh`, `Graph`, or `PointCloud`), and the presence of user-facing render components selects lanes (`RenderSurface`, `RenderEdges`, `RenderPoints`) when the domain provides the required data.

## Non-goals
- No point-cloud-to-graph or point-cloud-to-mesh promotion; point clouds can only render point lanes.
- No graph surface rendering unless a future task defines a graph-to-surface representation.
- No graphics-layer ECS reads or topology traversal; runtime remains the only live ECS owner.
- No asset/procedural residency redesign.
- No legacy `MeshEdgeView` / `MeshVertexView` component resurrection.
- No user-facing mesh primitive-view side channel. Meshes, graphs, and point clouds must use the same render component vocabulary.

## Context
- Owner/layer: `runtime` owns domain classification, lane routing, residency sidecars, and ECS-to-render-snapshot composition; `graphics` consumes immutable `GpuWorld` handles/config and render flags; UI edits value components only.
- Target user-facing API:
  - add `RenderPoints` to render vertices/nodes/points for meshes, graphs, and point clouds.
  - add `RenderEdges` to render mesh edges/wireframe or graph edges.
  - add `RenderSurface` to render mesh surfaces.
- Existing repo precedent:
  - `docs/architecture/rendering-target-architecture.md` described lifecycle systems where graphs write `Line::Component`/`Point::Component`, point clouds write `Point::Component`, and mesh views write `Line::Component`/`Point::Component` over shared mesh buffers.
  - `docs/architecture/patterns.md` records the BDA shared-buffer target: mesh wireframe and vertex visualization reuse the mesh vertex buffer instead of duplicating geometry.
  - Legacy components used presence-based render controls: `Graphics.Components.Line` says component presence enables mesh/graph edge rendering, and `Graphics.Components.Point` says component presence enables mesh vertex, graph node, or cloud point rendering.
  - `RUNTIME-086` and `RUNTIME-087` already promoted the graph and point-cloud halves of this model; `RUNTIME-088`/`BUG-028` special-cased meshes via `MeshPrimitiveViewSettings`, which this task corrects.
- Current promoted render components live in `src/graphics/renderer/Components/Graphics.Component.RenderGeometry.cppm`: `RenderSurface`, `RenderEdges`, and `RenderPoints`. This task migrated the user-facing edge component from the old `RenderLines` spelling to `RenderEdges` while keeping backend `GpuRender_Line` / `ForwardLinePass` naming as renderer implementation detail.
- Pre-change sandbox UI was domain-gated in `RenderHintCommandMatchesDomain(...)`: mesh commands could only edit `RenderSurface`; graph commands could edit `RenderEdges`/`RenderPoints`; point clouds could edit `RenderPoints`.
- Pre-change runtime extraction was not true lane composition:
  - Mesh domain binds one surface upload via `BindMeshGeometry(...)`; mesh edge/vertex rendering uses cache-owned `MeshPrimitiveViewSettings` sidecars instead of `RenderEdges` / `RenderPoints`.
  - Graph domain uses the current `RenderEdges` / `RenderPoints`, but `BindGraphGeometry(...)` packs both requested lanes into one `GraphGeometry` handle with a lane-mask reupload rule.
  - Point-cloud domain requires `RenderPoints` and routes through `BindPointCloudGeometry(...)`.
- Desired domain × component matrix:
  - Mesh + `RenderSurface` -> filled surface triangles from halfedge/face topology.
  - Mesh + `RenderEdges` -> mesh edge/wireframe lines from explicit edges or derived surface topology.
  - Mesh + `RenderPoints` -> mesh vertex points using `RenderPoints::Type` and `SizeSource`.
  - Graph + `RenderEdges` -> graph edge lines from node positions plus edge endpoints.
  - Graph + `RenderPoints` -> graph node points using `RenderPoints::Type` and `SizeSource`.
  - PointCloud + `RenderPoints` -> point-cloud points using `RenderPoints::Type` and `SizeSource`.
  - Unsupported pairs fail closed with diagnostics and no stale renderable.

## Required changes
- [x] Classify each renderable candidate from one `GeometrySources::ConstSourceView` plus component presence, computing requested/supported lanes for `Surface`, `Edges`, and `Points` without graphics importing ECS.
- [x] Migrate the user-facing ECS edge-render component from `Graphics::Components::RenderLines` to `Graphics::Components::RenderEdges` while leaving backend `GpuRender_Line` / line-pass names intact.
- [x] Refactor `RenderExtractionCache` so each render component routes through the appropriate domain lane reconciler:
  - `RenderSurface` lane: validates mesh surface requirements and owns surface geometry residency.
  - `RenderEdges` lane: validates mesh or graph edge requirements and owns edge/line geometry residency.
  - `RenderPoints` lane: validates mesh, graph, or point-cloud point requirements and owns point geometry residency/config.
- [x] Make mesh `RenderEdges` and mesh `RenderPoints` use the existing mesh primitive-view packer behavior as the implementation path, but drive it from ECS render components rather than `MeshPrimitiveViewSettings`.
- [x] Preserve graph `RenderEdges` / `RenderPoints` routing through the shared graph handle while making lane changes component-driven and stale-safe: adding/removing either component repacks the graph upload even without unrelated dirty tags.
- [x] Route point-cloud `RenderPoints` through the point lane system and keep `RenderSurface`/`RenderEdges` unsupported for point-cloud domains with deterministic diagnostics.
- [x] Forward `RenderPoints::Type` and uniform `SizeSource` consistently into retained point `GpuEntityConfig::PointMode` / `PointSize` for mesh vertices, graph nodes, and point-cloud points.
- [x] Remove `MeshPrimitiveViewSettings` from UI/engine as an authoritative user-facing control; compatibility wrappers translate to `RenderEdges` / `RenderPoints`.
- [x] Update sandbox UI render-hint commands/windows so mesh windows can edit surface, edges, and points; graph windows can edit edges and points; point-cloud windows can edit points; unsupported component/domain pairs produce deterministic diagnostics.
- [x] Update runtime counters/test seams so unsupported lanes, missing topology, invalid topology, upload failures, reuse hits, reuploads, releases, and retire-window frees are observable per lane/domain.

## Tests
- [x] Extend `SandboxEditorUi` contract coverage so mesh entities can add/remove/configure `RenderSurface`, `RenderEdges`, and `RenderPoints`; graph entities can add/remove/configure `RenderEdges` and `RenderPoints`; point-cloud entities can add/remove/configure only `RenderPoints`.
- [x] Add runtime extraction coverage for mesh surface-only, edge-only, point-only, and surface+edge+point combinations, proving separate render records/sidecars and independent release on component removal.
- [x] Add runtime extraction coverage for graph edge-only, point-only, and edge+point combinations under the component-driven graph routing.
- [x] Add runtime extraction coverage proving point clouds render only when `RenderPoints` exists and fail closed for unsupported `RenderSurface` / `RenderEdges`.
- [x] Add dirty/reuse/reupload tests proving each lane reuses clean residency, repacks on relevant dirty tags, and does not keep stale geometry after invalid source data.
- [x] Preserve existing BUG-028 vertex style coverage through `RenderPoints::Type` rather than `MeshPrimitiveViewSettings`.

## Docs
- [x] Update `src/runtime/README.md` to describe render-component domain composition and mark the old mesh primitive-view control surface as compatibility-only.
- [x] Update `src/graphics/renderer/README.md` only for renderer-facing point/line config facts, not runtime ECS ownership.
- [x] Update the now-retired `tasks/archive/RORG-031F-ui-integration.md` and
      `tasks/backlog/ui/README.md` for the UI scope/status change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening/retiring this task.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Render component presence, not `MeshPrimitiveViewSettings`, is the authoritative toggle for mesh surface, edge, and vertex rendering.
- [x] A mesh with only `RenderEdges` renders its wireframe without requiring `RenderSurface`.
- [x] A mesh with only `RenderPoints` renders vertices using `RenderPoints::Type` (`Flat`, `Sphere`, `Surfel`) and uniform `SizeSource`.
- [x] A mesh with all three render components produces independent surface, edge, and point render lanes over the same authoritative mesh domain data.
- [x] A graph with `RenderEdges`, `RenderPoints`, or both renders the requested lanes without stale lane-mask behavior.
- [x] A point cloud with `RenderPoints` renders points; point-cloud `RenderSurface` / `RenderEdges` requests fail closed with diagnostics and no stale rendering.
- [x] Unsupported domain/component pairs are deterministic and test-covered.
- [x] No promoted `src/graphics/*` code imports live ECS or owns topology traversal.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshPrimitiveView|GraphGeometry|PointCloudGeometry|RuntimeRenderExtraction|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction.AddingProceduralRefAfterMeshUploadReleasesMeshResidency|RenderWorldPoolPipelined.ConsumesRenderNMinusOneWhileExtractionWritesN' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing legacy ECS render-view components.
- Letting graphics read live ECS state or own mesh/graph topology traversal.
- Silently rendering unsupported domain/component pairs with fallback geometry.

## Slice plan
- **Slice A (landed).** Mechanically migrate user-facing `RenderLines` references to `RenderEdges` (leaving backend `GpuRender_Line` / line-pass names alone), then pin the domain × lane component matrix through focused contract coverage.
- **Slice B (landed).** Route mesh `RenderSurface`, `RenderEdges`, and `RenderPoints` through component-driven lane reconcilers. Reuse mesh primitive-view packers internally, but remove `MeshPrimitiveViewSettings` as the authoritative UI path.
- **Slice C (landed).** Preserve graph and point-cloud runtime-owned residency while making the component contract deterministic: graph lane toggles repack the shared graph handle, and point-cloud unsupported lanes fail closed with diagnostics and no stale residency.
- **Slice D (landed).** Update sandbox render-hint windows/commands, translate compatibility primitive-view commands, update docs, and run the CPU gate.

## Maturity
- Target: `CPUContracted`.
- Reached: `CPUContracted`.
- This task closes with the component/domain composition contract proven under the default CPU/null gate; no `Operational` follow-up is owed by default. `Operational` GPU screenshot proof is required only if CPU/null coverage exposes a backend-specific gap; if that happens, open a focused `gpu;vulkan` follow-up.
