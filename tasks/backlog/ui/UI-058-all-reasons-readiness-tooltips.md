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

- Handed over from UI-071 (2026-10-02):
  - three MeshProcessingPanels controls still sit in a bare `BeginDisabled` with no reason: the Segmentation Show buttons (~1377), Geodesics "Use selected vertices" (~4120) and "Clear##GeodesicsSourceProperty" (~4163);
  - the sandbox-editor-feature-boundaries "panel's own gating" wording names only Method panels, but MPP now uses `kPendingGpuRunReason` too;
  - the CPD Apply Running reason says "cancel it", but the panel offers Discard.

## Slice log
- Slice 1 (RUNTIME-277 is not done, so no structured `Reasons` exist yet): the three handed-over MPP controls
  (Segmentation Show buttons, Geodesics "Use selected vertices as sources", "Clear source property") now show a
  reason through `DrawProcessingActionButton` (Show buttons via a trailing readiness argument of
  `DrawProcessingPropertyShowButton`); the boundaries doc covers MPP's own gating; the CPD running reason says
  Discard; a source scan rejects bare-`BeginDisabled` buttons in MPP and MP. Still open and blocked on
  RUNTIME-277: the all-reasons tooltip, `DrawReadinessFieldMarker`, Smooth Property field markers and the
  multi-fault panel test. Left for later: EditorShell and PanelSupport still have bare-`BeginDisabled` buttons
  (Undo/Redo, New scene, Bake, Disconnect agent, scene draft buttons, screenshot controls) outside the scan.

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
