---
id: UI-072
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI consolidation with no runtime behavior change; evidence is the diff, the ImGui panel tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-072 — Adopt the shared panel helpers (Show buttons, spec-driven widgets)

## Goal
Panels use the helpers `Sandbox.PanelSupport.*` already provides instead of raw ImGui
re-implementations, so labels, ranges, defaults and result feedback come from one place.

## Context
Source: 2026-10-01 duplication/consistency audit (findings 3.2, 3.5, 4.6), re-verified at
`665c693dd`. MPP = `src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp`. Line numbers are
at the audit revision. The runtime-side reuse items are in
[RUNTIME-314](../backlog/runtime/RUNTIME-314-reuse-existing-processing-helpers.md); the GPU row and
disabled-reason presentation are [UI-071](../backlog/ui/UI-071-gpu-transaction-controls-and-refusal-presentation.md).

1. "Show ..." buttons bypass `DrawProcessingPropertyShowButton`. Six sites (MPP ~2213, ~2304,
   ~2394, ~2463, ~2535, ~2724) each repeat `Button` + `DebugNameForEditorCommandStatus(ShowProcessingProperty(...))`
   + `Text("Display: %s")`, because the helper (`Sandbox.PanelSupport.cpp` ~270) fixes the label to
   "Show " + the property name. Add an optional label parameter and use the helper at all six.
2. `DrawSpecInputDouble`/`UInt`/`EnumCombo`/`Checkbox` are used only by CPD (MPP ~3038), Smoothing
   (~3464, ~3521) and Point Sampling (~4298). The runtime already defines tables for Eigenbasis,
   Harmonic Field, Scalar Gradient, Geodesics and Mesh Curvature (RUNTIME-276 slice A), and these
   panels still use raw `Combo`/`InputDouble`/`InputScalar`. The adoption list survives only in the
   retired [UI-057](UI-057-schema-driven-field-hints.md) ("when a panel is next touched").
3. The topology panels (denoise, remesh, subdivide, simplify) clamp in the panel
   (`std::clamp(NormalIterations, 1, 4096)`, sigma 0..1e6, float where the command takes double)
   although `EditorMeshDenoiseFieldSpecs()`, `...RemeshFieldSpecs()`, `...SubdivideFieldSpecs()` and
   `...SimplifyFieldSpecs()` now declare the ranges and the command validator enforces them
   (RUNTIME-312 slice 7). The panel should read the same tables rather than carry second copies.
4. Backend combos are hard-coded `\0`-literal strings (27 in MPP). The CPU backend is spelled six
   ways ("CPU octree", "CPU KD-tree", "CPU reference", "CPU KD-tree (reference)", "CPU",
   "CPU LBVH (cached)"). Take the labels from the owner's enum names/tables where a table exists.
5. Point families (outliers, density, spacing, ...) use raw `InputFloat` without bounds and show a
   generic "Controls were rejected..." message. These adopt the spec widgets only as RUNTIME-276
   slice B lands its tables; do not invent tables here.

Noted, not in scope (separate decisions): two config-editing models (apply-on-every-edit vs draft
with "Apply configuration"), anonymous-namespace helpers in MPP that MP re-implements, run-button
verb and `##` ID consistency, unit labels (deg vs radians), and widths (UI-049).

## Acceptance criteria
- [x] The six Show sites use `DrawProcessingPropertyShowButton` (optional label); no raw `ShowProcessingProperty` + `Display:` text remains in panels.
- [x] Eigenbasis, Harmonic Field, Scalar Gradient, Geodesics and Mesh Curvature panels use the `DrawSpec*` widgets with their runtime tables (tooltip, range and default from the table).
- [x] Topology panels derive ranges from `EditorMesh*FieldSpecs()`; panel-local `std::clamp` literals and float/double mismatches are gone.
- [x] Backend combo labels for migrated panels come from the owning enum/table; remaining hard-coded combos are listed in the task log.
- [x] Tests assert the migrated widgets reject/clamp out-of-range input per the table; no change to what the commands accept.
- [x] Point-family adoption is recorded as blocked on RUNTIME-276 slice B, not silently dropped.

## Completion

Commit: `f16338564`, `63a1836b0`, `099f47115`, `b0b9eebf4`, `110a1ff71`. Completed 2026-10-02 with independent Opus reviews per slice and a final acceptance review.
- Full CPU suite 5650/5650.
- Panels share the Show/Display helpers and the `DrawSpec*` widgets. Topology drags read `EditorMesh*FieldSpecs()`.
- Typed input clamps to the table range, and exclusive bounds are respected (`110a1ff71`).
- Backend display labels and point-family adoption are owned by RUNTIME-276 slice B.
- Maturity: Operational for the UI (CPU ImGui tests). No GPU behavior changed.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'SandboxEditor|SandboxProcessingPanels|SandboxEditorMeshMethods' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Slice log
- Slice 1 (`f16338564`): `DrawProcessingPropertyShowButton` takes an optional label, `normalDirection` and `forceShow`; `DrawProcessingDisplayDiagnostic` renders the status; the six Show sites and the point-family sites use them. Review follow-up (folded into slice 2): the remaining raw `TextWrapped` display diagnostics (Smoothing, Eigenbasis, Harmonic, Gradient, Geodesics, Scalar Ridges) use `DrawProcessingDisplayDiagnostic` too, so those panels now read "Display: <status>"; `ShowPropertyHelperForwardsNormalDirectionAndPropertyName` pins `normalDirection` forwarding and a non-default property name. The helper keeps positional optional parameters (label, normalDirection, forceShow); an options struct is deferred until a fourth appears.
- Slice 2: Eigenbasis (Spectral Modes), Harmonic Field, Mesh Curvature and Geodesics draw their enum combos, numeric inputs and checkboxes through `DrawSpecEnumCombo`/`DrawSpecInputUInt`/`DrawSpecInputDouble`/`DrawSpecCheckbox` with the runtime `*ConfigFieldSpecs()` tables, and show the table description on property pickers and name inputs through the new `DrawSpecFieldHint`; Scalar Gradient has only property fields, so it gains hints only. Recorded differences: numeric inputs clamp to the declared closed range (Eigenpairs 1..256, Neighbors 1..1024, iterations, shell weights >= 0, expansion budget >= 1) where the raw inputs stored the typed value and the validator rejected it; doubles display with `%.6g` instead of `%.6f`; Harmonic's Mode combo reads "Field (interpolate values)"/"Labels (propagate seed labels)" and Curvature's Output reads "Principal directions" (the table's names; the literals differed); Harmonic label mode still offers only Fail and Keep input (`DrawSpecEnumCombo` gained `visibleCount`). Left raw: Harmonic's Unlabeled `InputInt` and Output-storage combo (no numeric-int widget or table entry), the Eigenbasis signature slider (range read from the table, drawn as a slider). Tests: `EigenbasisClampsToTheSpecRange`, `GeodesicsExpansionBudgetClampsToTheSpecRange`.
- Slice 3: Denoise, Remesh, Subdivide and Simplify read their drag bounds, hints and defaults from `EditorMeshDenoiseFieldSpecs()`/`...RemeshFieldSpecs()`/`...SubdivideFieldSpecs()`/`...SimplifyFieldSpecs()` through the new `DrawSpecDragInt`/`DrawSpecDragDouble` (clamp to the table's closed range before drawing, `AlwaysClamp`); the per-panel `std::clamp` literals (Remesh 1..64, Subdivide 1..10, 0..1e6 sigmas and target edge length, Simplify `max(.., 0)`) are gone and the draft state is `double` where the command takes `double` (sigmas, target edge length, max error, FA-QEM weights), so the `static_cast<double>` conversions are gone. Ranges are unchanged: the tables declare the literals' values (iterations 1..4096/64/10, target faces 0..1e9, FA-QEM weights 0..1000, feature angle 0..180); the only differences are that Remesh target edge length and Simplify max error are unbounded where the panel capped them at 1e6/1e30 (the table declares no upper bound; the Denoise sigmas keep their 1e6 cap, and Remesh has no sigma), and the Drag value now has `double` precision. `ClampToConfigFieldRange` steps one inside an exclusive integer bound; an exclusive Float bound is kept as typed (a nudged denormal would give NaN weights), so validation refuses it with its own diagnostic. Checkboxes gained the table hint. Left raw (not in the tables, or option-by-option disabled reasons): the Stage/Mode/Sizing law/Operator/Metric combos. `TopologyPanelDragsShowTheTableBounds` pins the table's upper bound per drag (read from the table, one double case included); it passes on the old code too, since the ranges did not change. `ConfigFieldClampStepsInsideExclusiveIntegerBounds` fails on the old clamp.
- Slice 4 (inventory, no code): the combos whose owner declares a table (CPD `method`/`e_step`/`output`, Smoothing `backend`/`method`/`weight`/..., plus the slice-2 Eigenbasis/Harmonic/Curvature enums) already take their labels from `DrawSpecEnumCombo`. The remaining backend combos have no owner table to read: the point-family configs (outliers, density, density weights, spacing, bilateral, descriptors, keypoints, construction, registration/ICP, normals) declare their backends only as enums with wire strings (`ToString`: `cpu_octree`, `cpu_lbvh`, `vulkan_lbvh`), not labels or `ConfigFieldSpec` entries, and inventing labels in the panel would be the second copy this task removes. Remaining hard-coded `\0` backend combos in `Sandbox.MeshProcessingPanels.cpp`: Normals point/mesh/face ("CPU KD-tree|CPU LBVH (cached)|Vulkan LBVH (resident PCA)", "CPU reference|Vulkan (GPU property residency, double precision)" twice), Outliers ("CPU octree|CPU LBVH (cached)|Vulkan LBVH"), Keypoints ("CPU|Vulkan" and "Acceleration: CPU KD-tree|...|Vulkan LBVH neighborhoods"), Descriptors and Density weights ("CPU KD-tree|..."), Kernel density, Spacing and Bilateral ("CPU octree|..."), Construction ("CPU reference|..."), ICP ("CPU KD-tree (reference)|...|Vulkan LBVH (CPU solve)"), plus `Sandbox.MethodPanels.cpp` consolidation ("CPU|Vulkan"). The CPU spellings (octree, KD-tree, reference, plain) differ by the index each method really uses, so unifying them waits for the tables. **Blocked on RUNTIME-276 slice B**: point-family numeric/enum controls (raw `InputFloat` without bounds, generic "Controls were rejected..." message) and these backend combos adopt the `DrawSpec*` widgets only when slice B lands their tables; nothing is invented here.
