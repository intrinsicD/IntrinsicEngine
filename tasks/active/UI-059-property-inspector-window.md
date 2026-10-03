---
id: UI-059
theme: F
depends_on: [RUNTIME-278]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: operator-directed interactive implementation; diff, focused tests, combined verification and Claude review provide evidence.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# UI-059 — Property Inspector window

## Goal
- Give users one window to inspect any property of any entity: statistics,
  histogram, comparison against a second property, and one-click visualization.

## Non-goals
- No property editing; no new statistics code in the app (RUNTIME-278/GEOM-109 own it).

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
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
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxPropertyInspector|SandboxProcessingPanels|DomainPanels|PropertyInspection)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Statistics computed in app code; property writes from the inspector.

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.
