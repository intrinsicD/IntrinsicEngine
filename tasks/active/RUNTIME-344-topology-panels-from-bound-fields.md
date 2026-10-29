---
id: RUNTIME-344
theme: F
depends_on: [ARCH-020, RUNTIME-343]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the mesh denoise, remesh, subdivide and simplify panels draw their parameter controls from the commands' bound field rows, so every declared field gets a control with the table's label, range and hint; the panels gain the controls they lacked (denoise epsilon; remesh lambda, curvature adaptation, approximation error, preserve boundary, projection neighbours and distance; subdivide max output faces); commands, validation and agent unchanged; no catalog contract covers panel layout
---
# RUNTIME-344 — Topology panels generated from the bound fields (moonshot M2, slice 20)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
23. (2026-10-29) "Yes, please do 1. - 4." (1: generate the panels from the bound rows; 2: the
    editor's own algorithm pieces into `src/geometry`; 3: one shared GPU run state machine; 4: a
    generic undo helper)

## Goal
- M2: "From that one declaration, a generator produces ... the ImGui panel". `RUNTIME-343` bound
  each field of the explicit-parameter commands to its command member. The panels still keep their
  own copy of each field (`DenoiseState::NormalIterations` and so on in
  `Sandbox.MeshProcessingPanels.cpp` ~279-341), draw one hand-written call per field, and lack some
  declared fields (denoise `degenerate_normal_length_epsilon`; remesh `lambda`,
  `curvature_adaptation`, `approximation_error`, `preserve_boundary`, `reference_projection_k`,
  `max_reference_projection_distance`; subdivide `max_output_faces`), which only the agent could set.

## Plan
1. **Generic controls** (`Sandbox.PanelSupport`): `DrawBoundFields(rows, command, id, filter)`
   draws each bound row the filter keeps, in table order, through one `DrawFieldControl(spec, id,
   wire, default)` built on the existing `DrawSpec*` helpers: Bool a checkbox with its hint; UInt
   an unsigned drag, or an unsigned input when its range exceeds `int` (no signed narrowing); Float
   a drag with format `%.6g`. Enums are not generated (O1): every enum of these panels previews its
   options' readiness or has a readable name (simplify's "FA-QEM (feature-aware)") a raw token would
   lose, so `DrawFieldControl` draws nothing for them, nor for Int (no such row). The label is the
   field name with spaces and its first letter upper-case (`normal_iterations` -> "Normal
   iterations"), plus `##<id>`, which keeps the labels the UI tests click. Values go through
   `FieldNumber`/`SetFieldNumber`; the controls clamp to the declared range first.
2. **Panels**: each state holds the command (`Runtime::EditorMesh*Command Params{}`) instead of its
   field copies (feature edges live there too; the denoise stage stays the panel's combo index and is
   written into the request). After drawing, the request is a copy of `Params` with the current
   `StableEntityId` (and stage), the same request for readiness and run (B2); subdivide clears
   `PreserveLoopFeatureEdges` before building it when the operator is not Loop. The per-option
   readiness probes stay on default requests (`{entity, option}`), so an unrelated draft value never
   disables the choice needed to recover. Generated: denoise five fields, its epsilon (default
   1e-12) a typed input under the generated label (O2: a drag cannot adjust it; the exclusive
   bound still refuses 0); remesh all but `mode`, `sizing_law` and `project_to_surface`; subdivide
   all but `operator` and `preserve_loop_feature_edges`; simplify all but `metric`, the six FA-QEM
   fields inside the existing collapsing header (disabled unless FA-QEM). Stay hand-written, because they preview each option's readiness
   or pair with a property picker: remesh mode, sizing law and project-to-surface; subdivide operator
   and preserve Loop features with the feature-edge picker; denoise stage. The scalar ridge panel
   stays hand-written in this slice (percent display, method-dependent fields, same-line layout;
   M2's escape hatch, named for a later slice).
3. **Visible changes** (O1): the new controls above; labels "Max error (0 = unlimited)", "Feature
   angle (deg)", "Preserve UV seams" become "Max error", "Feature angle threshold degrees", "Preserve
   uv seams" (zero meaning and units stay in the label or the hover hint); float drags show `%.6g`
   with one drag speed, 0.01 (simplify's max error was 0.001, its feature angle 0.5); remesh shows
   its sizing law before the generated fields.

## Tests (predictions as assertions)
- `SandboxProcessingPanels.TopologyPanelDragsShowTheTableBounds` keeps passing with its Real
  cases' expected text in `%.6g` (the bound still read from the table).
- New `SandboxProcessingPanels.TopologyPanelsDrawEveryDeclaredField`: each of the four panels'
  logged text contains the generated label of every bound row it does not draw by hand, the
  expected labels derived from the rows with the test's own label rule (red before: the missing
  controls).
- New `SandboxProcessingPanels.TopologyPanelEditsReachTheCommand` (B1): the denoise "Preserve
  boundary" checkbox toggles off and stays off, the typed epsilon keeps 1e-6, and an iteration
  count edited to 3 is what the denoise run reports (the request is the edited one); subdivide's
  "Max output faces" keeps 4294967295 (unsigned, no narrowing) and then subdivides to 32 faces;
  with 16 the run is refused and the mesh keeps its 8 faces (the face cap reaches execution).
- Refactoring: `SandboxProcessingPanels.*` (including the existing variant and feature-clearing
  tests), `SandboxEditorUi.*`, `DeclaredFieldContracts.*` pass before and after.
- Mutations, each must turn a test red: edits not written back (`SetFieldNumber` removed); the
  unsigned input narrowed to `int`; `DrawBoundFields` skipping Float rows; the label rule without
  upper-casing; a panel's filter dropping one generated field.

## Acceptance criteria
- [x] `DrawBoundFields` and the four panels on it; field copies in panel state gone.
- [x] Every declared field of the four commands has a control; tests green.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -R '^(SandboxProcessingPanels|SandboxEditorUi|DeclaredFieldContracts)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Red before: `TopologyPanelsDrawEveryDeclaredField` written first and run on the
  unchanged panels failed on exactly the missing controls (denoise epsilon; remesh lambda,
  curvature adaptation, approximation error, preserve boundary, projection k and distance;
  subdivide max output faces; simplify feature angle threshold; and "Preserve uv seams", the
  label change).
- 2026-10-29: Codex plan review (`plan344.answer.md` in this session's scratchpad): revise. B1 the
  tests proved rendering only: the interaction test added. B2 request assembly and probe isolation
  specified (Plan 2). O1 visible changes completed; the simplify metric stays hand-drawn, so no
  enum is generated in this slice. O2 the epsilon is a typed input.
- 2026-10-29: Implemented: `BoundFieldLabel`, `DrawFieldControl`, `DrawBoundFields`
  (`Sandbox.PanelSupport`); the four panels hold their commands and draw the declared fields from
  the rows (remesh, subdivide and simplify's enum combos now write the command's enums directly, no
  index copies). Mutations, each alone and restored, all red: edits not written back (interaction,
  bounds and admission tests); the unsigned input narrowed to `int` (interaction); Float rows
  skipped (coverage, bounds); the label without its capital (coverage, interaction, bounds,
  admission); subdivide's filter dropping `max_output_faces` (coverage, interaction). Focused
  `SandboxProcessingPanels|SandboxEditorUi|DeclaredFieldContracts` 331/331; `SourceRules` 8/8;
  layering clean; changed lines clang-format 20.1.8. Lines: `Sandbox.MeshProcessingPanels.cpp`
  -91, `Sandbox.PanelSupport` +77 (net -14 with eight more controls); `src/runtime/Editor`
  unchanged.
- 2026-10-29: Codex review of the revision and the change (`review344.diff` in this session's
  scratchpad): approve; B1, B2, O1, O2 resolved. Optional, all done: the interaction test also
  types the epsilon and an iteration count and checks the denoise run reports the edited count (a
  request rebuilt from defaults would report 5); the drag-speed changes recorded; two comments made
  exact.
  The extended test was checked by one more mutation, red: the denoise request built from defaults
  instead of the panel state (`EditorMeshDenoiseCommand command{}`). Suites rerun 331/331.
