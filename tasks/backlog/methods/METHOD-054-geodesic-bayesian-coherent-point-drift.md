---
id: METHOD-054
theme: I
depends_on: [METHOD-050]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes paper intake, parity tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# METHOD-054 — Geodesic Bayesian Coherent Point Drift (GBCPD)

## Goal
- Register meshes whose parts touch or fold (limbs against a body, fingers) by replacing
  the Euclidean deformation kernel of BCPD (METHOD-050) with a geodesic kernel on the
  source mesh, following Hirose's geodesic-based BCPD.

## Context
- Paper intake first (citation, kernel construction, low-rank form, BCPD++ interplay);
  implement from the paper, not from Hirose's code.
- The engine's mesh geodesic distances are the candidate kernel input; the low-rank
  (Nystroem/eigen) path of METHOD-049 must accept a geodesic Gram matrix.
- Point clouds and graphs need a neighbourhood graph as the geodesic domain; record which
  sources are supported.

## Acceptance criteria
- [ ] `paper.md` intake section with the formulation and the implemented kernel.
- [ ] `Params` option selecting the geodesic kernel for `Variant::Bayesian` (mesh sources; graph and k-NN graph sources if the paper's construction applies).
- [ ] Test: a folded fixture (two parts touching in Euclidean space) that Euclidean BCPD mis-registers and GBCPD registers within a stated RMS.
- [ ] Config field, panel control and agent field.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Source points plus source connectivity (mesh or graph). |
| Compatible entity sources | Meshes and graphs; point clouds via a k-NN graph or refused with a reason. |
| RuntimeModule | None; RUNTIME-273 passes the option and captures connectivity. |
| Config/agent | `sandbox.coherent_point_drift` field. |
| UI | CPD panel Bayesian controls. |
| Publication | Unchanged. |
| End-to-end tests | Contract test through the editor command on a mesh source. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
