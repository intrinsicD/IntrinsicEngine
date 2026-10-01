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
[RUNTIME-314](../runtime/RUNTIME-314-reuse-existing-processing-helpers.md); the GPU row and
disabled-reason presentation are [UI-071](UI-071-gpu-transaction-controls-and-refusal-presentation.md).

1. "Show ..." buttons bypass `DrawProcessingPropertyShowButton`. Six sites (MPP ~2213, ~2304,
   ~2394, ~2463, ~2535, ~2724) each repeat `Button` + `DebugNameForEditorCommandStatus(ShowProcessingProperty(...))`
   + `Text("Display: %s")`, because the helper (`Sandbox.PanelSupport.cpp` ~270) fixes the label to
   "Show " + the property name. Add an optional label parameter and use the helper at all six.
2. `DrawSpecInputDouble`/`UInt`/`EnumCombo`/`Checkbox` are used only by CPD (MPP ~3038), Smoothing
   (~3464, ~3521) and Point Sampling (~4298). The runtime already defines tables for Eigenbasis,
   Harmonic Field, Scalar Gradient, Geodesics and Mesh Curvature (RUNTIME-276 slice A), and these
   panels still use raw `Combo`/`InputDouble`/`InputScalar`. The adoption list survives only in the
   retired [UI-057](../../done/UI-057-schema-driven-field-hints.md) ("when a panel is next touched").
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
- [ ] The six Show sites use `DrawProcessingPropertyShowButton` (optional label); no raw `ShowProcessingProperty` + `Display:` text remains in panels.
- [ ] Eigenbasis, Harmonic Field, Scalar Gradient, Geodesics and Mesh Curvature panels use the `DrawSpec*` widgets with their runtime tables (tooltip, range and default from the table).
- [ ] Topology panels derive ranges from `EditorMesh*FieldSpecs()`; panel-local `std::clamp` literals and float/double mismatches are gone.
- [ ] Backend combo labels for migrated panels come from the owning enum/table; remaining hard-coded combos are listed in the task log.
- [ ] Tests assert the migrated widgets reject/clamp out-of-range input per the table; no change to what the commands accept.
- [ ] Point-family adoption is recorded as blocked on RUNTIME-276 slice B, not silently dropped.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'SandboxEditor|SandboxProcessingPanels|SandboxEditorMeshMethods' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
