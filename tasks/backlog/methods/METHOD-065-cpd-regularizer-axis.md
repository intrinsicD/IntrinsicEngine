---
id: METHOD-065
theme: I
depends_on: [METHOD-064]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from an operator design discussion (2026-09-29); implementation follows the method workflow (paper intake, CPU reference, parity, sealed benchmark).
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# METHOD-065 — Selectable deformation regularizer for nonrigid CPD and BCPD

## Goal
- The nonrigid CPD M-step is Tikhonov smoothing of the residual field R = P1^-1 P X - Y:
  (G + lambda sigma^2 P1^-1) W = R, V = G W. The low-rank mode already filters R in the
  eigenbasis of G. BCPD has the same structure (prior N(0, lambda^-1 G)).
- Make the regularizer an axis; the default stays as it is:
  1. **Gaussian kernel** (current, extrinsic; full or low rank).
  2. **Geodesic Gaussian kernel** (METHOD-064), full or low rank. Paper intake: Hirose,
     "Geodesic-based Bayesian coherent point drift" (verify the citation and its formulation
     before use).
  3. **LBO spectral filter.** Solve (D + lambda sigma^2 L) V = D R: an implicit Laplacian
     smoothing step with the matching weights as mass.
     - Either in a truncated LBO eigenbasis Phi (k x k system), reusing the Laplacian
       eigenbasis module;
     - or by CG on the sparse operator, reusing the GPU sparse CG.
     - Mesh cotangent Laplacian; point clouds use the heat-kernel (Belkin-Niyogi) Laplacian.
- GPU: with a basis the M-step is two dense m x k products and a k x k solve. Together with the
  device E-step, nonrigid CPD/BCPD then runs on the GPU and reads back only a convergence value
  per iteration (RUNTIME-294 row, medium effort).

## Acceptance criteria
- [ ] CPU reference per regularizer. Analytic checks:
  - equal weights reduce to the diagonal spectral filter;
  - the Gaussian option is bitwise unchanged from today.
- [ ] Benchmark on the Vlasic articulated meshes and the existing CPD fixtures: accuracy against
      the ground-truth correspondence (same topology), run time, and iterations per regularizer;
      sealed evidence.
- [ ] Documented limits: the LBO null space (translation free, rotations penalized; rigid
      pre-alignment advised for plain CPD), point-cloud graph shortcuts, the basis on BCPD
      subsamples.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged two point sets; the geodesic and LBO options need source topology or a kNN graph. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds (LBO/geodesic built per domain). |
| RuntimeModule | Existing CPD operations; the eigenbasis comes from the Laplacian eigenbasis module. |
| Config/agent | New `regularizer` enum (gaussian, geodesic_gaussian, lbo_spectral) plus `basis_size`; the default keeps today's results. |
| UI | CPD panel Model section: regularizer combo, only its parameters shown. |
| Publication | Unchanged (displacement/positions). |
| End-to-end tests | Editor-command test per regularizer; gpu;vulkan smoke once the device M-step lands. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
