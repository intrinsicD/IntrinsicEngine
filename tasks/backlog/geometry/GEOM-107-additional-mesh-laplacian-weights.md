---
id: GEOM-107
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
# GEOM-107 — Wachspress, mean-value and modified-face-normal Laplacian weights

## Goal
Complete the Laplacian weight set with the Framework24 variants still missing from
`Geometry.HalfedgeMesh.DEC`'s `EdgeWeightMode` (retired GEOM-041 added cotan,
heat-kernel, graph, Fujiwara and modified-normal).

## Context
- Floater, *Mean value coordinates*, CAGD 2003; Wachspress, *A Rational Finite Element Basis*, 1975 (and Meyer et al. 2002 for polygonal generalizations).
- Mean-value weights are nonsymmetric; document how the symmetric operator is formed (or expose them only where asymmetric operators are accepted).

## Acceptance criteria
- [ ] New modes with linear-precision tests on planar meshes and sign/positivity properties documented.
- [ ] Available wherever `EdgeWeightMode` is configurable (smoothing, harmonic fields, parameterization comparisons); docs updated.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle mesh positions and connectivity. |
| Compatible entity sources | Mesh vertices. |
| RuntimeModule | Existing consumers of `EdgeWeightMode`. |
| Config/agent | New enum values in the existing sections. |
| UI | New combo entries. |
| Publication | Unchanged. |
| End-to-end tests | Unit tests plus one consumer contract test. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'DEC|Laplacian' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
