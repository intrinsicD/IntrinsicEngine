---
id: UI-057
theme: F
depends_on: [RUNTIME-276]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui panel test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; panel tooltips and range-aware inputs read runtime field specs and change no property binding, publication, module interface or format contract.
---
# UI-057 — Schema-driven field hints and ranges in processing panels

## Completion — 2026-09-28
Commit: the enclosing `claude/config-schemas` commit records this retirement.
`Sandbox.PanelSupport` gains `FormatConfigFieldHint`/`DrawConfigFieldHint` (description,
accepted values, default) and `DrawSpecInputDouble`, `DrawSpecInputUInt`,
`DrawSpecEnumCombo` and `DrawSpecCheckbox`, which read an owner's `ConfigFieldSpec`
table: numeric input is clamped to the declared range and combos are labeled from
`EnumNames` (built on `ImGui::Combo`, so item IDs match the literal combos). The Smooth
Property panel uses them for every parameter; property bindings show their field hint.
`SandboxProcessingPanels.PropertySmoothingControlsClampToTheFieldTableAndShowItsHint`
types 5000 neighbors and sees 1024 applied, and checks the hint text.

Deviation: the helpers take the owner's table (`PropertySmoothingConfigFieldSpecs()`)
plus a field name rather than a section name (RUNTIME-276's accessor shape), and the
default shown comes from the default-constructed config.

Follow-up adoption, when a panel is next touched (tables exist for these today):
- Harmonic Field (`HarmonicFieldConfigFieldSpecs()`), Spectral Modes
  (`LaplacianEigenbasisConfigFieldSpecs()`), Scalar Gradient, Geodesics, Mesh Curvature.
- Point families and remaining sections after RUNTIME-276 slices B and C.

## Goal
Show every processing parameter's description, valid range and default in a
tooltip and clamp inputs to the declared range, using the RUNTIME-276 field
tables, starting with the Smooth Property panel.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Shared helpers live in `src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp/.cpp` (`DrawProcessingActionButton`, `DrawDisabledReasonTooltip`, `DrawProcessingPropertyInput`); data from `FindConfigFieldSpec(section, field)` (RUNTIME-276), no JSON in app.
- The agent sees the same descriptions and ranges through the section schema (`config_schema`).

## Control surfaces
- Config: N/A (reads specs; writes go through the existing draft/apply path).
- UI: `DrawFieldHint(section, field)` tooltip; range-aware `DrawSpecSliderFloat/Int`/enum combo using `EnumNames`.
- Agent/CLI: same data via `config_schema` (RUNTIME-276/CORE-010).

## Acceptance criteria
- [x] Helpers added to `Sandbox.PanelSupport`; Smooth Property panel adopts them for every parameter.
- [x] Enum combos label entries from `EnumNames` so UI names and schema `x-enum-names` cannot diverge.
- [x] `Test.SandboxProcessingPanels.cpp` checks a hinted control clamps to the declared range and the tooltip text contains the description.
- [x] Follow-up adoption for other families recorded here as a slice list (adopt when a panel is touched).

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|PropertySmoothing' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hand-written range/description strings duplicating the field tables.
- UI-only config writes.
