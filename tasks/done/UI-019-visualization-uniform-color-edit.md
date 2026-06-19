---
id: UI-019
theme: F
depends_on: [RORG-031F, UI-002, UI-005, UI-013]
maturity_target: CPUContracted
---
# UI-019 — Visualization uniform color edit widget

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: mesh, graph, point-cloud, and top-level geometry visualization UI
  windows now expose an ImGui color edit widget when
  `VisualizationConfig::ColorSource::UniformColor` is active. Edits route
  through the existing visualization config command and preserve the rest of
  the current visualization config payload.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  succeeded, and `ctest --test-dir build/ci --output-on-failure -R
  '^SandboxEditorUi\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
  passed 51/51 tests.

## Goal
- Let the promoted mesh, graph, and point-cloud visualization UIs edit the active uniform visualization color with an ImGui color edit widget.

## Non-goals
- No renderer, RHI, Vulkan, or shader behavior changes.
- No generic property-buffer residency or arbitrary visualization upload work.
- No new visualization modes beyond editing the existing `UniformColor` source.

## Context
- Owner/layer: `runtime` editor UI (`Extrinsic.Runtime.SandboxEditorUi`) consumes selected-entity state and routes edits through runtime-owned command surfaces.
- `UI-005` added promoted visualization property presets and `UI-013` added mesh, graph, and point-cloud domain controls, but the current uniform-color affordance only enables `VisualizationConfig::ColorSource::UniformColor` with the default command color.
- `Graphics::Components::VisualizationConfig` already carries a `glm::vec4 Color`; this task exposes that existing field through the UI without adding graphics ownership.

## Required changes
- [x] Add a shared uniform-color control to the mesh, graph, point-cloud, and top-level geometry visualization windows.
- [x] Route edits through `ApplySandboxEditorVisualizationConfigCommand(...)`.
- [x] Preserve the existing visualization config fields when switching to or editing uniform color.

## Tests
- [x] Add/update `contract;runtime` coverage proving custom uniform colors round-trip through the command/model seam.
- [x] Run the focused `SandboxEditorUi` contract test subset.

## Docs
- [x] Update runtime/UI task notes for the promoted uniform-color edit affordance.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring this task.

## Acceptance criteria
- [x] When selected visualization source is `UniformColor`, the mesh, graph, and point-cloud visualization UIs expose a color edit widget.
- [x] Editing the widget updates the selected entity's `VisualizationConfig::Color`.
- [x] Non-uniform visualization modes keep their existing property/preset behavior.
- [x] No graphics, renderer, RHI, asset, or ECS ownership boundary changes are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding UI ownership of renderer, RHI, asset-service, worker, or geometry storage state.

## Maturity
- Target: `CPUContracted`.
- This slice closes the backend-neutral editor command/model contract; no `Operational` follow-up is owed.
