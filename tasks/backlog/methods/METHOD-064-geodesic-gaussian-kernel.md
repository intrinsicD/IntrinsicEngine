---
id: METHOD-064
theme: I
depends_on: [RUNTIME-291]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from an operator design discussion (2026-09-29); implementation follows the method workflow (paper intake, CPU reference, parity, sealed benchmark).
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# METHOD-064 — Geodesic Gaussian kernel as a shared smoothing/regularization kernel

## Goal
- Operator (2026-09-29): offer smoothing with the geodesic Gaussian kernel
  exp(-d_g(x_i, x_j)^2 / (2 beta^2)) next to the extrinsic Gaussian kernel. It couples points
  along the surface, not across gaps: on an articulated body the hand next to the hip, or the
  touching knees, stay decoupled.
- One owner in the geometry layer: a sparse kernel truncated at a cutoff (a few beta), built
  from geodesic distances:
  - meshes: the existing geodesics module (heat method or exact, whichever the module offers);
  - graphs and point clouds: shortest paths on the kNN graph, with the neighbor radius from
    RUNTIME-291.
- It is keyed by topology/position revision and reused by every consumer.
- Consumers: property smoothing (a new neighbor weighting "Geodesic Gaussian" next to the
  existing "Gaussian kNN"), and CPD/BCPD (METHOD-065).
- Paper intake: geodesic kernels on manifolds (positive-definiteness is not guaranteed for
  geodesic Gaussians on curved surfaces; record how consumers stay well-posed, e.g. a diagonal
  shift or a low-rank eigen-truncation).

## Acceptance criteria
- [ ] CPU reference kernel with analytic tests: on a plane it equals the Euclidean Gaussian; on
      a folded strip it differs; the cutoff is respected; the result is independent of the
      element domain for the same points.
- [ ] Property smoothing offers the geodesic Gaussian weighting on mesh vertices, graph nodes
      and point clouds; parity of the Vulkan path where one exists.
- [ ] Documented behavior on disconnected components and on noisy point-cloud graphs (shortcut
      edges).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions plus optional mesh/graph topology; point clouds build a kNN graph. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | Existing property-smoothing and geodesics modules; no new module unless a cache owner is needed. |
| Config/agent | `weight` enum value "geodesic_gaussian" plus `geodesic_cutoff`; agent schema follows. |
| UI | Smooth Property panel weighting combo. |
| Publication | Unchanged property publication. |
| End-to-end tests | Panel test and one editor-command test per domain. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
