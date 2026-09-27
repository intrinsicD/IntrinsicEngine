---
id: UI-058
theme: F
depends_on: [RUNTIME-277]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui panel test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; tooltip and control-marker drawing over existing readiness data changes no binding, publication, module interface or format contract.
---
# UI-058 — All-reasons readiness tooltip and offending-control markers

## Goal
Show every reason an action is disabled and mark the control that causes it,
using RUNTIME-277's structured readiness, starting with the Smooth Property panel.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Helpers in `src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp/.cpp`: `DrawProcessingActionButton(label, readiness)`, `DrawDisabledReasonTooltip`. Coordinate adoption in other families with [UI-037](../../active/UI-037-linear-domain-action-readiness-tooltips.md).

## Control surfaces
- Config: N/A.
- UI: action tooltip lists all reasons; `DrawReadinessFieldMarker(readiness, "<field>")` next to a control.
- Agent/CLI: the same reasons are returned by preview/run agent operations (RUNTIME-277).

## Acceptance criteria
- [ ] `DrawProcessingActionButton` tooltip lists all reasons; `DrawReadinessFieldMarker` draws a marker plus tooltip for reasons whose `Field` matches.
- [ ] Smooth Property panel places markers on every configurable field.
- [ ] `Test.SandboxProcessingPanels.cpp` drives a multi-fault draft and checks the tooltip lists every reason and markers appear on the named fields.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- UI-computed readiness that differs from the runtime verdict.
