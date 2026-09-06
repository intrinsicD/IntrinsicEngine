---
id: UI-053
theme: J
depends_on: [METHOD-041]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive follow-up for the agreed detector-first inspection sequence."
owner: codex
branch: codex/method-040-boundary-partition
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-06T23:26:45+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "Publishes detached geometric feature curves through shared validated engine/config/UI operations."
---
# UI-053 — Curvature-extremum engine overlay

## Goal
- Expose METHOD-041 exact curve inspection in the Sandbox after its standalone CPU/overlay slice, with shared configuration and source-preserving publication.

## Acceptance criteria
- [ ] Shared serializable config validates signal, physical support, confidence and scale selection for UI and agents.
- [ ] Runtime uses canonical mesh preflight and detached extraction; failures preserve prior publication.
- [ ] Exact subtriangle curve geometry is inspectable alongside existing METHOD-040 boundaries without changing source topology or positions.
- [ ] Config/runtime/UI tests cover identity, controls, publication and failure; build the Sandbox for operator inspection.

## Engine integration
| Field | Decision |
| --- | --- |
| Least-structured input | Embedded triangle surface accepted by METHOD-041. |
| Compatible entity sources | Canonical mesh sources accepted by common runtime preflight. |
| RuntimeModule | Existing geometry-processing operation family. |
| Config/agent | Shared serializable parameters with preview/validate/apply. |
| UI | Existing Curvature window with explicit extraction and overlay controls. |
| Publication | Source-bound detached curve geometry, preserving source fields and topology. |
| End-to-end tests | Config/runtime/publication and headless UI contracts. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureExtrema|SandboxCurvatureSegmentationPanel' --timeout 120
cmake --build --preset dev --target ExtrinsicSandbox
python3 tools/agents/check_task_policy.py --root . --strict
```
