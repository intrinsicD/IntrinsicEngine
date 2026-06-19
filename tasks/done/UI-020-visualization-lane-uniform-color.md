---
id: UI-020
theme: F
depends_on: [RORG-031F, UI-002, UI-005, UI-013, UI-019]
maturity_target: CPUContracted
---
# UI-020 — Visualization lane uniform color controls

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: mesh, graph, point-cloud, and top-level visualization models now
  separate the selected entity's default visualization config from surface,
  edge, and point render-lane overrides. Domain visualization windows target
  lane presence instead of only the mutually exclusive active domain, so mesh
  vertices and graph nodes rendered as points can receive a uniform color
  independently of surface and edge rendering.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  succeeded, and focused CPU/null contract coverage for `SandboxEditorUi`,
  `MeshPrimitiveViewExtraction`, `GraphGeometryExtraction`, and
  `RuntimeSceneSerialization` passed 93/93 tests.

## Goal
- Let selected-entity visualization controls target render lanes by source-component presence so mesh/graph vertices rendered as points can carry a uniform color independent of edge or face rendering.

## Non-goals
- No new shader visualization modes.
- No Vulkan-only validation requirement for this slice.
- No UI ownership of geometry, render resources, assets, or material state.

## Context
- Owner/layer: `runtime` editor UI routes selected-entity commands; `graphics` owns data-only visualization components; runtime extraction owns ECS-to-render-instance wiring.
- Current `GeometrySources::ActiveDomain` is mutually exclusive. A mesh has vertices, edges, halfedges, and faces, so the point-cloud visualization window is disabled even though the mesh can render its vertices through `RenderPoints`.
- Current `VisualizationConfig` is one selected-entity config. Mesh edge/vertex sidecars and graph line/point lanes need per-lane config selection to color point-rendered vertices without changing faces or edges.

## Required changes
- [x] Add a data-only per-lane visualization override component for surface, edge, and point render lanes.
- [x] Route sandbox visualization commands to the selected lane when a domain visualization window targets a render lane.
- [x] Make visualization-window editability depend on the presence of the source components needed by that lane, not only the mutually exclusive active domain.
- [x] Ensure runtime extraction submits lane-specific visualization configs for mesh and graph line/point lanes.

## Tests
- [x] Add/update `contract;runtime` UI coverage for point-lane controls on mesh/graph selections.
- [x] Add/update extraction coverage for independent mesh/graph point-lane uniform colors.
- [x] Run focused runtime contract tests.

## Docs
- [x] Update runtime/UI notes for lane-targeted visualization controls.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring this task.

## Acceptance criteria
- [x] A mesh with `RenderPoints` can set a point-lane uniform color without changing its surface or edge visualization config.
- [x] A graph with `RenderEdges` and `RenderPoints` can submit distinct edge-lane and point-lane visualization configs.
- [x] Point-cloud visualization controls are active for selected entities that have point-renderable vertex/node sources.
- [x] Existing top-level visualization controls keep editing the selected entity's default visualization config.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshPrimitiveViewExtraction|GraphGeometryExtraction|RuntimeSceneSerialization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Replacing the promoted `GeometrySources` domain model.
- Adding renderer, RHI, asset-service, or geometry storage ownership to the UI.

## Maturity
- Target: `CPUContracted`.
- This slice closes the backend-neutral UI/extraction contract; no `Operational` follow-up is owed.
