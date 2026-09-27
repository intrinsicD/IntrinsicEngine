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
- [ ] Helpers added to `Sandbox.PanelSupport`; Smooth Property panel adopts them for every parameter.
- [ ] Enum combos label entries from `EnumNames` so UI names and schema `x-enum-names` cannot diverge.
- [ ] `Test.SandboxProcessingPanels.cpp` checks a hinted control clamps to the declared range and the tooltip text contains the description.
- [ ] Follow-up adoption for other families recorded here as a slice list (adopt when a panel is touched).

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|PropertySmoothing' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hand-written range/description strings duplicating the field tables.
- UI-only config writes.
