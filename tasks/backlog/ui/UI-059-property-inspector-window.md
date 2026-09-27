---
id: UI-059
theme: F
depends_on: [RUNTIME-278]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# UI-059 — Property Inspector window

## Goal
- Give users one window to inspect any property of any entity: statistics,
  histogram, comparison against a second property, and one-click visualization.

## Non-goals
- No property editing; no new statistics code in the app (RUNTIME-278/GEOM-109 own it).

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Panel helpers in `src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp/.cpp`: `DrawProcessingEntity`, `DrawProcessingPropertyInput`, `ShowProcessingProperty` (applies a visualization recipe via `ApplyEditorVisualizationRecipeCommand`). Histogram widget `DrawEditorScalarPropertyPlotWidget` (ImPlot) in `Runtime.EditorPropertyWidgets`, used only in `Sandbox.DomainPanels.cpp`.
- Windows register through `EditorShell::RegisterEditorWindow(EditorWindowDescriptor{Id, MenuPath, Title, OpenByDefault, Draw})`; rules in `docs/architecture/sandbox-editor-feature-boundaries.md`.

## Control surfaces
- Config: N/A.
- UI: `view.property_inspector` ("Property Inspector") under View.
- Agent/CLI: the same data via `property_stats`/`property_compare`/`property_values` (RUNTIME-278).

## Required changes
- [ ] Window in `Sandbox.DomainPanels.cpp`: entity chooser, property chooser over all domains, stats table (counts, NaN/Inf/zero, per-component min/max/mean/RMS/std), histogram (widget generalized to accept precomputed statistics), "Compare with" chooser + error table, "Show" button.
- [ ] Appearance histogram in domain windows reuses the same model.

## Tests
- [ ] `Test.SandboxProcessingPanels.cpp` (or a domain-panel test) drives the window on a mesh fixture: select a face property, read stats rows, compare two properties, press Show and observe the visualization recipe.

## Docs
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` window list.

## Acceptance criteria
- [ ] Any property on any domain of a mesh, graph or point cloud can be inspected and compared from the window.
- [ ] Displayed numbers equal the RUNTIME-278 query results for the same inputs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|DomainPanels|PropertyInspection' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Statistics computed in app code; property writes from the inspector.
