---
id: RUNTIME-275
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
# RUNTIME-275 — Gaussian noise editor operation

## Goal
Expose the existing `ApplyGaussianNoise` kernels (point clouds and graphs) as an
editor operation so denoising, LOP/consolidation and outlier workflows can be
exercised on controlled noise. The Framework24 inventory row "Point-cloud Gaussian
noise" requires a disposition.

## Acceptance criteria
- [ ] Isotropic noise with sigma as an absolute value or a fraction of the bounding-box diagonal; optional displacement along normals; fixed seed for reproducibility.
- [ ] Applies to point clouds, graph nodes and mesh vertices (positions or any vec3 property), writing in place or to a named output; one guarded undoable transaction.
- [ ] Config section, panel and contract/ImGui tests.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A vec3 property on a point domain plus noise parameters. |
| Compatible entity sources | Point clouds, graph nodes, mesh vertices. |
| RuntimeModule | Point-set operation file with shared capture/history helpers. |
| Config/agent | `sandbox.gaussian_noise`. |
| UI | Processing → Add noise on the three domains. |
| Publication | In place or named output, undoable. |
| End-to-end tests | Determinism, statistics of the added noise, undo/redo, ImGui path. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GaussianNoise' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
