---
id: GEOM-106
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-106 — Center-surround saliency of scalar properties

## Goal
Compute the Framework24 saliency: the difference of Gaussian-weighted averages of a
scalar property at scales σ and 2σ (multi-scale optional), per sample — distinct
from the existing ISS keypoint saliency. The inventory row lists it as open.

## Context
- Lee, Varshney & Jacobs, *Mesh Saliency*, SIGGRAPH 2005 (center-surround on mean curvature, multi-scale with nonlinear normalization).
- Framework24 `bcg_point_cloud_vertex_saliency.h` as a checklist.

## Acceptance criteria
- [ ] Kernel over any scalar property on any domain (default input: mean curvature), single and multi-scale with the paper's normalization; analytic tests on synthetic bumps.
- [ ] Editor operation and panel publishing a float property; contract and ImGui tests.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Scalar property plus sample positions and scales. |
| Compatible entity sources | All canonical domains via sample capture. |
| RuntimeModule | Point/mesh analysis operations. |
| Config/agent | `sandbox.scalar_saliency`. |
| UI | View → Saliency and domain redirects. |
| Publication | Named float property. |
| End-to-end tests | Kernel, contract and ImGui tests. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Saliency' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
