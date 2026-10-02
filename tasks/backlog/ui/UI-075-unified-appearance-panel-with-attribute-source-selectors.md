---
id: UI-075
theme: J
depends_on: [UI-051, RUNTIME-315, RUNTIME-318]
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
- Operator decisions 2026-10-02 (model unification, picking/culling follow displayed positions, canonical normals, single Color mechanism, pixel sizes) are recorded in [RUNTIME-315](../../done/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02).
- Before this task: `pointcloud.appearance`, `graph.appearance` and `mesh.appearance` were registered separately and each drew the others' sections; the attribute table sat under Advanced as "Attribute sources", with the structural channel selector ("Vertex channels") and read-only "Binding targets" beside it. Current state is in the Log.
- `UI-051` (capability-based domain gating) is the prerequisite; this task replaces its Appearance part with the unified window.
- Shared helpers: `Sandbox.PanelSupport` property pickers, `DrawDisabledReasonTooltip`, `DrawAttributeSourceTable`; `UI-037`/`UI-058` own the disabled-reason presentation, `UI-074` the Color interpretation tooltip text, `UI-072` shared helper adoption.

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

## Log
- 2026-10-02: Slice 1. `scene.appearance` (`View > Appearance`) replaces the three per-kind
  windows. `kAppearanceSections` maps Vertices/Edges/Faces to the PointCloud/Graph/Mesh lane
  models; a section is built only where `GeometryDomainReadingIncludes(entity provenance, probe
  element)` holds, and a section the predicate reaches but the model refuses is drawn disabled with
  the model's diagnostics. Lane visibility, settings, the Property dropdown and Advanced moved
  into the sections unchanged.
- 2026-10-02: Slice 2. `DrawAttributeSourceTable` (PanelSupport) draws one table per section from
  `EditorAttributeBindingModel` (attribute, selector with Default plus candidates, status/fallback);
  a pick calls `ApplyEditorAttributeBindingCommand`; incompatible candidates are disabled with the
  runtime's reason. The old Advanced "Attribute sources" block is gone. Tests: tables per entity
  kind, bind/refuse/Default/undo through the real combo, reason text, unavailable section.
  Not covered: a usable domain whose lane target is unavailable has no runtime reason text yet
  (no reachable case on today's provenance rules).
- 2026-10-02: Slice 3. `EditorAttributeBindingRow::OverlayTarget` (runtime) names the lane overlay a
  Color row binds, so the panel picks the lane model without a rule of its own. A bound Color row
  shows the interpretation combo (`DrawColorInterpretationCombo`, UI-074 tooltips) and the Color
  mapping block; Point size / Line width rows show the uniform pixel field only while on Default
  (a bound row's source is its selector). The row tooltip now states the fallback. The lane
  Settings keep the render-hint combos, the Property dropdown, uniform color and texture baking.
- 2026-10-02: Slice 4. The read-only "Binding targets" list is removed from Advanced
  (`DrawPropertyBindingTargets`; the "Vertex channels" block went with RUNTIME-315 slice 3);
  the catalog's `BindingTargets` model stays for the inspector. README Appearance prose and the
  agent-control-lane parity note (RUNTIME-316 mirrors the selectors) are updated.
- 2026-10-02: Review fixes. Row controls moved to full-width lines under the section table (the
  table cell clipped them); status text wraps; a test at the default 340 px width asserts the
  window's content size stays within its inner width. Every Color row is covered (mesh V/E/F,
  graph V/E, cloud V): an interpretation edit lands on the lane named by `OverlayTarget`
  (graph nodes edit the Edges lane); colormap edits share the same lane model but are not
  separately driven. The entity's attribute binding model is built once per cache key and
  shared by the inspector and the three domain lanes. Uniform size/width fields disable
  without scene commands. Default for size/width restores the component default (6 px / 1 px),
  not the previous uniform value: binding a name replaces the single `std::variant<float,
  std::string>` source, so the old float is not kept.
- 2026-10-02: Layout test fix (review). `AppearanceContentFitsTheDefaultWindowWidth` could not
  see clipping inside the table: `EndTable` caps the window's `CursorMaxPos.x` at the table edge,
  so the window content size stayed within the inner width with the row controls in the cell.
  The test now also asserts every column's submitted content (`ContentMaxXUnfrozen`) stays
  within its column, that the Color bind succeeded and that the interpretation combo was
  submitted. With the pre-fix layout restored locally (row details back in the cell, no
  half-width item width) it fails with column 1 overflowing by 80 px; the window check alone
  still passed.
