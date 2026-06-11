---
id: UI-013
theme: F
depends_on: [UI-002, RUNTIME-085, RUNTIME-086, RUNTIME-087, RUNTIME-102]
maturity_target: CPUContracted
---
# UI-013 — Sandbox EditorUI domain render hint controls

## Goal
- Let the promoted sandbox editor edit mesh, graph, and point-cloud render hints through runtime/editor command seams so selected-domain windows can enable/disable and tune the existing `RenderSurface`, `RenderLines`, and `RenderPoints` components.

## Non-goals
- No new renderer pass, Vulkan pipeline, geometry packer, or GPU readback implementation.
- No broad arbitrary per-property line-width or point-size buffer upload support.
- No native file dialog, sample scene, or debug-workflow expansion.

## Context
- Owner/layer: `runtime/editor` UI command surface mutates ECS and graphics render-hint value components; renderer and RHI remain consumers of immutable extraction snapshots.
- Legacy had mesh/graph/point-cloud render controls. Promoted renderer/runtime already expose retained surface, line, and point rendering via `GeometrySources`, `RenderSurface`, `RenderLines`, `RenderPoints`, and the `RUNTIME-085..087` residency bridges.
- `UI-002` intentionally stopped at render-hint status windows and called out follow-up workflow tasks for render-hint edits once justified.

## Required changes
- [x] Add a sandbox editor render-hint command/model surface for selected-domain render components.
- [x] Wire mesh, graph, and point-cloud domain render windows to edit the existing promoted render hints.
- [x] Keep command behavior reversible through `EditorCommandHistory` when available and deterministic without it.

## Tests
- [x] Add focused `contract;runtime` coverage for command application, model reporting, and history undo/redo.
- [x] Add extraction-facing coverage proving UI-applied render hints drive graph/point-cloud residency lane changes.

## Docs
- [x] Update UI backlog notes to record that render-hint editing is now covered by `UI-013`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after task state changes.

## Acceptance criteria
- [x] Mesh render windows can toggle surface rendering and mesh edge/vertex primitive views.
- [x] Graph render windows can independently toggle line and point lanes and edit uniform line width / point radius.
- [x] Point-cloud render windows can toggle point rendering and edit point render type / uniform radius.
- [x] Existing import materialization still creates renderable mesh, graph, and point-cloud entities with compatible defaults.

## Completion
- Completed 2026-06-11 in this change. The slice retired at `CPUContracted`: editor commands and domain render windows now mutate promoted render-hint value components, graph line-lane edits repack runtime graph residency, and uniform point settings flow into retained point `GpuEntityConfig`.
- PR/commit: this retirement commit.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorCommandHistory|GraphGeometryExtraction|PointCloudGeometryExtraction' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- No changes to promoted graphics/RHI layer ownership.
- No live ECS reads from `src/graphics/*`.
- No deletion or copy-forward of legacy graphics/editor code.

## Maturity
- Target: `CPUContracted`.
- This slice closes the current editor workflow gap over existing promoted renderer/runtime paths; no Operational follow-up is owed unless a future task asks for GPU readback proof of edited hint states.
