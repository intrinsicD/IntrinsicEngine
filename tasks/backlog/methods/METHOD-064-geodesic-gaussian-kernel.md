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
# METHOD-064 — Heat-kernel Gram matrix as the positive-definite geodesic Gaussian

## Goal
- Operator (2026-09-29): a positive-definite "geodesic Gaussian" kernel that couples points
  along the surface rather than through ambient space. It is shared by CPD/BCPD (METHOD-065)
  and property smoothing.
- **Notation (reviewed by Fable 5.1 and Codex 6 Astra, 2026-09-29).**
  - L_w is the weak (cotangent or graph) Laplacian and M the mass, with the generalized problem
    L_w phi = mu M phi and Phi^T M Phi = I (the eigenbasis module's convention).
  - Propagation operator H_t = exp(-t M^-1 L_w) = Phi e^{-t Lambda} Phi^T M (not symmetric).
  - **Gram matrix G_t = H_t M^-1 = Phi e^{-t Lambda} Phi^T**: symmetric; SPD in full, PSD of
    rank k when truncated. G_t is what CPD needs, not H_t and not M^{1/2} H_t M^{1/2}.
  - Its precision G_t^-1 = M Phi e^{t Lambda} Phi^T M is finite but explodes numerically
    (e^{t mu_max}), so it is used only in a truncated basis.
- **Amplitude and units.**
  - k_t(x, x) ~ (4 pi t)^{-d/2}, not 1 like the current Gaussian kernel. For CPD use G_t is
    rescaled to unit mean diagonal so that lambda stays comparable across regularizers.
  - t is in length^2 of the normalized coordinates.
- **Varadhan** (-4t log k_t -> d_g^2) holds under refinement, not as t -> 0 on a fixed mesh. The
  flat comparison needs t >> h^2 and interior points.
- **Why not "geodesic distance, then Gaussian":** it is not PD in general; only flat spaces make
  it PD for all bandwidths (Feragen et al. 2015, verify). It stays a benchmark row only.
- **Basis-free application.**
  - (M + (t/n) L_w)^-1 M applied n times, starting from M^-1 v for the Gram.
  - This is a rational filter (1 + t mu/n)^-n, not the heat kernel. It leaks high modes by
    orders of magnitude for small n (Fable: t mu = 10, n = 4 gives 6.7e-3 against 4.5e-5), so
    it is treated as its own regularizer (METHOD-065), not as a heat-kernel approximation.
- **Point clouds.**
  - The eigenbasis module's Gaussian-kNN Laplacian (D - W) lacks the Belkin-Niyogi scale and
    density normalization, so t has no length^2 meaning there. Add the scaling and a
    Coifman-Lafon alpha = 1 normalization (verify).
  - Ambient kNN edges join touching parts (hand-hip, knees) and diffusion faithfully follows
    them. Mitigate with normal-consistency weights max(0, n_i . n_j)^p or the Sharp-Crane 2020
    nonmanifold/point-cloud Laplacian (verify). Mesh cotangent L is immune unless the scan is
    welded.
- **Right-sizing:** property smoothing already has `SpectralHeat` (exp(-tL) in a basis) and
  `Implicit` ((M + tL) x = M b) in `Geometry.PropertySmoothing`. This task adds only what they
  lack: the Gram form, the amplitude normalization and the point-cloud Laplacian fixes, shared
  with CPD. There is no new smoothing filter.

## Acceptance criteria
- [ ] G_t = Phi e^{-t Lambda} Phi^T built from the eigenbasis module:
  - symmetric, PSD of rank k, SPD in full on a small fixture;
  - modes with e^{-t mu} below 1e-12 of the maximum are dropped (inverse-safe);
  - unit-mean-diagonal scaling applied.
- [ ] Refinement test on a flat patch: interior k_t, including the (4 pi t)^{-1} prefactor,
      approaches the Euclidean heat kernel for t >= 4h^2 as h halves; folded strip: k_t follows
      the surface.
- [ ] Rational-filter error per mode, (1 + t mu/n)^-n - e^{-t mu}, tabulated on the truncated
      basis for n = 1, 2, 4.
- [ ] Point-cloud Laplacian: scaling and density normalization, so a sampled flat patch passes
      the same refinement test; normal-consistency weights remove a planted shortcut edge.
- [ ] Identical operator, mass and ordering give identical results across element domains; a
      disconnected component gives a block-diagonal G_t.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions plus optional mesh/graph topology; point clouds build a kNN graph. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | Existing Laplacian eigenbasis module (basis and G_t factor, cached per topology/position revision). |
| Config/agent | No new smoothing filter; the point-cloud Laplacian gains `normal_consistency_power` and the density normalization in the eigenbasis config; CPD fields in METHOD-065. |
| UI | Laplacian eigenbasis panel: the new point-cloud Laplacian options. |
| Publication | Unchanged property publication. |
| End-to-end tests | Eigenbasis editor-command test per domain with the new options. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
