---
id: GEOM-108
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, repo.source-documentation]
---
# GEOM-108 — Small Framework24 parity helpers

## Goal
Close three small gaps from the 2026-09-27 audit in one change each.

## Acceptance criteria
- [ ] Gaussian divergences on `Geometry.GaussianMixture::MultivariateGaussian`: KL divergence and 2-Wasserstein (Bures) distance with a true matrix square root (Framework24's version takes an elementwise square root and is wrong); tests against closed forms.
- [ ] Similarity (scale) option for point-to-point ICP via Umeyama in `Geometry.Registration`, exposed in the registration config/UI; tests recover a known similarity.
- [ ] Vertex gradients as the area-weighted average of incident face gradients (Mancinelli, Livesu & Puppo, *Gradient Field Estimation on Triangle Meshes*, 2018), published beside the existing face gradients; tests on linear fields.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Gaussians; point sets; mesh scalar fields. |
| Compatible entity sources | As the existing owners. |
| RuntimeModule | Existing registration and gradient operations. |
| Config/agent | ICP scale flag; gradient output option. |
| UI | Existing panels. |
| Publication | Unchanged patterns. |
| End-to-end tests | Unit tests plus contract tests for the ICP flag and gradient output. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GaussianMixture|Registration|Gradient' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
