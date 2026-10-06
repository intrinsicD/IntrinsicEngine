---
id: UI-077
theme: F
depends_on: [UI-058]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive mechanical UI adoption; evidence is the diff, ImGui tests, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. This adopts the existing UI-058 marker helper in more panels; it changes no property, publication, recipe-slot or locality contract.
---
# UI-077 — Readiness field markers in every config panel

## Goal
Every processing panel whose runtime readiness reports field-keyed reasons
(RUNTIME-277) marks the offending config controls with the UI-058
`ReadinessMarkerScope` / `DrawReadinessFieldMarker`. Today only Smooth Property
does.

## Context
- UI-058 added the marker helper, hooked into `DrawConfigFieldHint`, so every
  `DrawSpec*` control inside a `ReadinessMarkerScope` is covered.
- Adopting a panel means one scope around its config controls, fed with the same
  readiness the panel's action button uses.
- Field-keyed reasons exist only where a family has adopted RUNTIME-277: the
  mesh-field family now, others as UI-037 migrates them. Adopt a panel only once
  its family produces field keys.
- Smooth Property's inline `TextWrapped` reason shows only the first reason. Align
  it with the tooltip when adopting.
- REVIEW-007 E03/E07/E08 (2026-10-06): while touching these panels, consolidate
  the truly identical parts of the result-header blocks (E03), the enum-combo
  idioms (E07; `DrawSpecEnumCombo` in `Sandbox.PanelSupport.hpp` already exists)
  and the GPU-start lambdas (E08) in `src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp`
  and `Sandbox.PanelSupport`. Keep the parts that differ. About −75 lines.

## Acceptance criteria
- [ ] The Eigenbasis, Harmonic Field and Scalar Gradient panels wrap their config controls in `ReadinessMarkerScope`. So does each further family once UI-037 gives it field-keyed reasons; record which in a slice log.
- [ ] An ImGui test per adopted panel shows the "(!)" marker on an out-of-range field and none on valid ones.
- [ ] Inline reason text lists every reason, or points to the tooltip, consistently across panels.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|SandboxDomainPanels|SandboxEditor' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
