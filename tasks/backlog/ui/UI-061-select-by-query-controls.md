---
id: UI-061
theme: F
depends_on: [RUNTIME-280]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui panel tests, review and CI.
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# UI-061 — Select-by-query controls and "use selection as mask/source"

## Goal
Let users run selection queries from Selection Details and feed the current
selection into Harmonic Field and Laplacian Eigenbasis inputs.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Existing controls (index entry, All/Invert/Clear) in `Sandbox.DomainPanels.cpp` (`scene.selection`); runtime command from RUNTIME-280.

## Control surfaces
- Config: the Harmonic/Eigenbasis drafts receive `HardMask`/`DistanceSource` through their existing config drafts.
- UI: query kind combo, parameters, Combine mode, "Write mask property" name, Run (with RUNTIME-277 readiness); "Use selection as mask/source" buttons in the Harmonic/Spectral panels.
- Agent/CLI: `selection_query`/`selection_get` (RUNTIME-280).

## Acceptance criteria
- [ ] Select-by-query block next to the existing selection controls, on every domain window that shows selection.
- [ ] "Use selection as…" buttons fill the Harmonic `HardMask` (via a published mask) and Eigenbasis `DistanceSource`.
- [ ] ImGui tests: radius query on a mesh selects the expected count; mask property appears in the catalog; Harmonic panel accepts the selection as mask.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|DomainPanels|SelectionQuery' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Query evaluation in app code.
