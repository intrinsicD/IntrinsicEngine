---
id: UI-066
theme: F
depends_on: [RUNTIME-286, RUNTIME-280]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui window test, review and CI.
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# UI-066 — Mesh Health window

## Goal
Show the mesh health report for the selected mesh and let users publish, select
and show problem markers.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Report and marker publication from RUNTIME-286; problem selection through RUNTIME-280's mask → selection helper; "Show" via `ShowProcessingProperty`.

## Control surfaces
- Config: N/A.
- UI: `mesh.analysis.health` under `Mesh > Analysis` ("Mesh Health"): Run (with readiness), report grouped Topology / Defects / Scale / Quality, "Publish problem markers", "Select problem vertices/faces", "Show" markers.
- Agent/CLI: `mesh_health` (RUNTIME-286).

## Acceptance criteria
- [ ] Window registered under `Mesh > Analysis`; report table matches the runtime result.
- [ ] ImGui test on a non-manifold fixture: Run, publish markers, select problem vertices, observe the selection count.
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` window list updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|MeshHealth' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Analysis computed in app code.
