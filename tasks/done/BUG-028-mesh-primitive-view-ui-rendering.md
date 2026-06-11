---
id: BUG-028
theme: F
depends_on: [RUNTIME-088, UI-013, BUG-027]
maturity_target: CPUContracted
---
# BUG-028 — Mesh primitive view UI toggles do not render

## Goal
- Make selected-mesh edge and vertex view UI controls produce renderer-visible mesh wireframe and mesh-vertex point views through the promoted runtime/editor path.

## Non-goals
- No legacy `MeshEdgeView` / `MeshVertexView` component resurrection.
- No graphics-side ECS reads or mesh-topology traversal.
- No broad renderer rewrite, new framegraph recipe, or unrelated graph/point-cloud render-hint changes.
- No Vulkan-only proof in this slice unless the CPU/null diagnosis shows the backend path is the only failing seam.

## Context
- Owner/layer: `runtime/editor` for the UI command/model seam and `runtime` extraction for mesh primitive view sidecars; graphics consumes immutable render snapshots and `GpuWorld` instance config only.
- Legacy made wireframe/vertex rendering presence-controlled with per-pass components. The promoted path deliberately uses `RenderExtractionCache`-owned `MeshPrimitiveViewSettings` plus sidecar line/point instances, not ECS components.
- Reported symptoms on 2026-06-11: toggling vertex or edge view does nothing; vertex view needs flat circle, surface-aligned circle, and impostor-sphere modes; edge view should draw mesh edges/wireframe.
- Initial ranked hypotheses:
  1. UI commands mutate settings, but live engine extraction does not submit derived line/point renderables into the renderer-visible frame world.
  2. The UI model/command uses a different command surface than the engine-owned extraction cache.
  3. Derived view renderables are submitted but miss material/config needed by retained line/point passes, making them effectively invisible.
  4. Vertex view lacks a render-style control/config path, so it cannot select the requested point modes.
  5. Imported meshes may lack explicit `GeometrySources::Edges`, causing edge views to fail closed even while synthetic tests pass.

## Required changes
- [x] Reproduce the current edge-view and vertex-view failures through a deterministic runtime/editor feedback loop.
- [x] Fix the UI command/extraction/snapshot path so edge-view toggles render a mesh line/wireframe lane.
- [x] Extend mesh vertex-view settings and UI so vertex rendering can select flat circles, surface-aligned circles, and impostor spheres.
- [x] Ensure vertex-view point settings propagate to renderer-visible point config for the derived point sidecar.
- [x] Preserve the promoted runtime-sidecar ownership model.

## Tests
- [x] Add focused `contract;runtime` or `integration;runtime` coverage that drives the actual sandbox editor command surface and proves the live engine extraction snapshot contains mesh surface, line, and point renderables.
- [x] Add regression coverage for vertex-view point mode/radius propagation to the derived point renderable.
- [x] Add or extend packer/extraction coverage for edge topology fallback if imported mesh fixtures lack explicit edge rows.

## Docs
- [x] Update `src/runtime/README.md` with the corrected mesh primitive-view control/config contract.
- [x] Update bug/task notes with the root cause and verification result.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening/retiring the task.

## Acceptance criteria
- [x] A mesh entity selected through the sandbox editor command surface can enable edge view and the next extraction exposes a line renderable for the same stable id.
- [x] A mesh entity can enable vertex view and the next extraction exposes a point renderable for the same stable id.
- [x] Vertex view exposes and applies flat-circle, surface-aligned-circle, and impostor-sphere modes without changing graph/point-cloud render-hint semantics.
- [x] Edge view works for the mesh topology produced by current promoted OBJ/OFF import materialization.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshPrimitiveView|RuntimeRenderExtraction|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical moves with semantic refactors.
- Introducing unrelated feature work.
- Reverting UI-013 render-hint command behavior.
- Adding live ECS ownership to `src/graphics/*`.

## Status
- Retired 2026-06-11; owner: Codex; branch: `main`.
- PR/commit: this task-retirement commit (`BUG-028 fix mesh primitive view rendering`).
- Root cause: mesh primitive views are promoted runtime extraction sidecars, not ECS view components. The UI and command DTO preserved only booleans, vertex sidecars never wrote `GpuEntityConfig::PointSize` / `PointMode`, the forward point shader ignored point mode, and the edge packer required explicit `Edges` rows instead of deriving wireframe lines from valid surface topology.
- Fix summary: `MeshPrimitiveViewSettings` now carries vertex mode/radius, sandbox UI exposes the controls and routes them through command history, extraction writes point config on upload and reuse, edge view derives unique wireframe lines from halfedge/face topology when explicit edge rows are absent, mesh/point-cloud packers use the shared UV slot for optional oct-encoded normals, graph uploads write the no-normal sentinel, and `forward/point.vert/frag` renders flat circles, screen-space sphere impostors, and normal-aligned surfel ellipses.
- Verification: `cmake --build --preset ci --target IntrinsicTests`; focused `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshPrimitiveView|GraphGeometryPacker|PointCloudGeometryPacker|RuntimeRenderExtraction|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 114/114; full default CPU-supported `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2973/2973; direct `glslc` compile of `assets/shaders/forward/point.vert` and `.frag` passed.

## Maturity
- Retired at `CPUContracted`. Runtime/editor/extraction behavior and shader compilation are deterministic under the CPU/null gate; broader GPU screenshot proof remains outside this bug slice.
