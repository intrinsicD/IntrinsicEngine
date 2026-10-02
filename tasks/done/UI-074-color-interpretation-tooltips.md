---
id: UI-074
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: one-slice mechanical UI text change; evidence is the diff and CPU gate
contract_schema: 1
contracts: []
contract_review: Reviewed contract-catalog.yaml; this adds hover text to an existing control and changes no property binding, domain eligibility, GPU coherence or editor-frame compile boundary.
---
# UI-074 — Explain "Color interpretation" with tooltips

## Goal
- Operator feedback 2026-10-02: the "Color interpretation" combo (`Components`, `Normal direction`) in Appearance (`src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp:765`) does not say what either option does or how the resulting color is read. Add a tooltip on the control and on each option.

## Non-goals
- No change to encoding behavior or to the `VisualizationConfig::ColorInterpretation` enum.
- Property-source selection is `UI-075`.

## Context
Behavior to describe (verified in code): with `Components` the property's own components are the color, a `vec3` read as R,G,B with alpha 1 and a `vec4` as R,G,B,A, taken as-is (expected range 0..1; the encoder does not rescale: `Runtime.VisualizationRecipes.cpp:470-479`). With `Normal direction` a `vec3` is treated as a direction: normalized (zero length falls back to +Z) and mapped from [-1,1] to [0,1] by `n*0.5+0.5`, alpha 1 (`:461-476`); intended for normals and other unit vectors. The control is only shown when a per-vertex/edge/face color buffer property is selected.
- Reuse the shared hover idiom (`DrawDisabledReasonTooltip`, `Sandbox.PanelSupport.hpp:228`, or `ImGui::SetItemTooltip`); the combo is disabled together with its section, so the tooltip should also show the reason (`UI-037`/`UI-058` own readiness reasons).
- `UI-075` will move this control into the unified Appearance panel; keep the strings in one shared helper so the move is a call-site change.

## Acceptance criteria
- [x] Hovering the combo explains both options in plain language, including the value-range expectation and what happens to out-of-range or zero-length vectors.
- [x] Each popup entry shows its own tooltip (`BeginCombo` + `Selectable` instead of the NUL-separated list).
- [x] The text matches the encoder behavior above; a test asserts the strings name the 0..1 and [-1,1] ranges.

## Completion

Commit: `a0041176b`, `0422c549e`, `2ffd59235`. Completed 2026-10-02; two independent Opus reviews.
- `DrawColorInterpretationCombo` (Sandbox.PanelSupport) is the single call site for meshes, graphs and point clouds, with a combo tooltip and per-entry tooltips.
- The text is verified against the encoder:
  - vec3/vec4 are RGB(A) and vec2 is RG.
  - Whole numbers >= 0 and bools get label colors; a negative, fractional or > uint32 value hides the property.
  - Normal direction is vec3 only; zero-length shows as +Z.
  - Colors are shown lit and tone-mapped.
- Exact strings are asserted by `SandboxDomainPanels` tests.
- Maturity: Operational for the UI text (CPU ImGui tests). No GPU behavior changed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxDomainPanels|SandboxEditorPresentation|SandboxEditorVisualization' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
