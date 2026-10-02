---
id: UI-075
theme: J
depends_on: [UI-051, RUNTIME-315]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
maturity_target: Operational
contract_schema: 1
contracts: [geometry.element-domain-sources, runtime.editor-prepared-frame-locality]
---
# UI-075 — One Appearance panel for all domains with per-attribute source selectors

## Goal
- Operator feedback 2026-10-02: replace the three per-kind Appearance windows with one Appearance panel for every entity. For each element domain the entity has (Vertices, Edges, Faces) it shows a table of render attributes (Position, Normal, Color, Size/Radius or Width, Texcoord) with a source-property selector per row (current source, compatible candidates, incompatible ones disabled with a reason, "Default"), as in Framework24's rendering UI.

## Non-goals
- No binding model or validation (`RUNTIME-315`); no conversion entities; no change to Properties/Selection windows (`UI-051` gates them).
- Method panels stay where they are.

## Context
- Operator decisions 2026-10-02 (model unification, picking/culling follow displayed positions, canonical normals, single Color mechanism, pixel sizes) are recorded in [RUNTIME-315](../runtime/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02).
- Today: `pointcloud.appearance`, `graph.appearance`, `mesh.appearance` are registered separately (`Sandbox.DomainPanels.cpp:996-1017`); the Mesh window also draws graph and point sections, graph draws point (`:1118-1135`), so the three windows already duplicate one another. `DrawDomainRenderWindow` (`:679`) shows a lane checkbox, render hints (point type/size, edge width, surface domain), a single "Property" dropdown that drives only color/scalar overlays (`:417`), and hides the structural channel selector and read-only slot tables under "Advanced" (`:741-743`; "Vertex channels" `:175`, "Binding targets" `:144`). Position/Normal/Texcoord/Size/Width cannot be chosen.
- `UI-051` (capability-based domain gating) is the prerequisite; this task replaces its Appearance part with the unified window.
- Shared helpers: `Sandbox.PanelSupport` property pickers (`:236`), `DrawDisabledReasonTooltip` (`Sandbox.PanelSupport.hpp:228`); `UI-037`/`UI-058` own the disabled-reason presentation, `UI-074` the Color interpretation tooltip text, `UI-072` shared helper adoption.

## Control surfaces
- UI: one `Appearance` window; selection-driven; calls `ApplyEditorAttributeBindingCommand` from `RUNTIME-315`.
- Agent/CLI: `RUNTIME-316` mirrors every selector (`attribute_bindings`, `bind_attribute`).
- Config: window layout persists through the existing workspace layout (`UI-048`).

## Maturity
- Slices 1-3 close `CPUContracted` (UI model and ImGui draw tests); the final slice closes `Operational` with a GUI smoke under the Xephyr nested X server plus the `GRAPHICS-158` Vulkan smoke.

## Slice plan
1. **Unified window shell (~350 lines).** One registered `Appearance` window driven by the selected entity's available element domains (via `UI-051` capability model); remove the per-kind registrations and duplicated section loops; keep lane visibility, render hints and the existing overlay Property dropdown unchanged inside it.
2. **Attribute table (~400 lines).** Per element domain a table: attribute, current source (property name, type, count), selector combo listing compatible properties (name, type, count) with incompatibility reasons and a Default entry, diagnostics line (fallback counts, `AttributeBindStatus`). Uses `BuildEditorAttributeBindingModel`; no logic in the draw code.
3. **Controls and tooltips (~300 lines).** Move Color interpretation (with `UI-074` text), color mapping, point size/edge width fields into their attribute rows (a bound Size/Width shows the source instead of the uniform field); hover text per attribute naming expected type, domain and fallback.
4. **Cleanup and docs (~200 lines).** Remove "Vertex channels"/"Binding targets" from Advanced, update `src/app/Sandbox/README.md` Appearance prose and the agent-control-lane parity note.
5. **Operational evidence (~250 lines).** Xephyr GUI smoke driving the real selector: bind an offset `vec3` as Position on a mesh, a graph and a point cloud and read back the rendered result; capture of the panel; cite the run.

## Acceptance criteria
- [ ] Exactly one Appearance window exists; it works for a point cloud, a graph and a mesh (including a mesh read as points/edges, per `UI-051`).
- [ ] Every attribute on every present element domain is listed with its current source and a selector offering compatible properties; incompatible ones are disabled with the runtime's reason.
- [ ] Choosing another `vec3` property as Position changes the rendered geometry, Default restores it, and both are undoable.
- [ ] Color interpretation has the `UI-074` tooltips in its new location.
- [ ] No draw-code decision logic: the panel renders `BuildEditorAttributeBindingModel`.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxDomainPanels|SandboxEditorVisualization|SandboxEditorPresentation|EditorWorkspaceSnapshots' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
