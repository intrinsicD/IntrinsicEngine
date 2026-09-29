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
- **Starting point (verified by Fable 5.1 and Codex 6 Astra, 2026-09-29, to 1e-15).**
  - The code solves (D G + lambda sigma^2 I) W = P X - D Y with D = diag(P1),
    `Geometry.Registration.CoherentPointDrift.cpp:563-576`.
  - This minimizes sum_i p_i ||v_i - r_i||^2 + lambda sigma^2 v^T G^-1 v, i.e. Tikhonov
    smoothing of the residual field R = D^-1 P X - Y.
  - With weights p_i = c: the spectral filter g_i / (g_i + lambda sigma^2 / c), where
    c ~ N_p / M.
  - The low-rank Woodbury path is basis-agnostic: it needs Lambda^-1 but not orthonormal
    columns, verified with a non-orthonormal Q to 2e-13.
- **Axis** (the default stays Gaussian, unchanged on the unchanged backend):

  | Regularizer | Prior on V | M-step | Notes |
  |---|---|---|---|
  | Gaussian kernel (today) | lambda/2 tr(W^T G W) | full or low rank, as today | unit-diagonal kernel |
  | Heat kernel G_t (METHOD-064) | lambda/2 tr(z^T Lambda^-1 z) in the basis | low-rank Woodbury with Phi_k, e^{-t Lambda_k} (tiny modes dropped) | unit-mean-diagonal scaling; mesh-independent |
  | Rational (M + tau L_w)^-n, n in {1, 2}, **default n = 2** | lambda/2 tr(V^T L_w (M^-1 L_w)^{n-1} V) | basis-free: (D + a L_w) V = D R for n = 1; (D + a L_w M^-1 L_w) V = D R for n = 2, by sparse CG | n = 1 (H^1) is mesh-dependent on surfaces |

- **Optional normal consistency for the Gaussian kernel** (operator, 2026-09-29). It keeps the
  Gram matrix PD: products of PD kernels are PD (Schur); a clipped max(0, n_i . n_j)^p is not
  PD and is used only for Laplacian weights (METHOD-064).
  - Oriented source normals: k_ij = exp(-||y_i - y_j||^2 / 2 beta^2) * exp(kappa (n_i . n_j - 1)).
    This is exactly the Gaussian kernel on augmented coordinates [y, n / sqrt(kappa)] (since
    ||n_i - n_j||^2 = 2 - 2 n_i . n_j), so full, low-rank, Nystroem and the error estimates
    work unchanged in 6-D.
  - Unoriented normals (PCA without MST): exp(kappa ((n_i . n_j)^2 - 1)), which is PD and
    sign-invariant; the low-rank builder then needs the kernel directly rather than the 6-D
    embedding.
  - Normals of the fixed source are computed once; kappa = 0 is bitwise today's kernel.
  - Also offered for the heat kernel? No: the heat kernel already follows the surface. Normal
    weights enter its point-cloud Laplacian instead (METHOD-064).
- **Why n = 2 is the default.** On a 2-manifold the Green's function of (D + a L_w) is
  log-singular. One confident correspondence makes a spike that sharpens under refinement.
  Fable's measurement of the centre/neighbour ratio as h halves:
  - n = 1: 4.9 -> 8.9 -> 11.0 -> 12.9 -> 14.9;
  - biharmonic (n = 2): 1.19 at every h;
  - heat kernel: about 2.0.

  Sobolev order > d/2 (Duchon, verify) makes the result mesh-independent. n = 1 stays available
  and is documented as mesh-dependent.
- **Objective and EM.**
  - `Coherence()` (`CoherentPointDrift.cpp:586-595`) must use the matching prior (lambda/2 of
    the table's form); convergence otherwise tests a wrong objective.
  - With a fixed L_w this is MAP/generalized EM: NLL + prior decreases monotonically.
  - The sigma^2 update is unchanged (the prior carries no sigma^2).
  - Rebuilding L_w from deformed positions would change the objective, so it is not done.
- **SPD and zero weights.**
  - SPD holds iff ker D intersected with ker L_w = {0}.
  - P1 underflows to exactly 0 under the truncated, Nystroem and device E-step policies, so a
    whole component can lose all weight. An epsilon M shift or component pinning is part of the
    design, not a fallback.
  - Conditioning ~ (max p + a mu_max) / min p.
- **BCPD.**
  - The default (`PosteriorVarianceTerms` off, `CoherentPointDrift.cppm:131`) needs only
    Sigma b, which CG provides.
  - With variance terms on, the sigma^2 trace may use Hutchinson (weighted trace error
    1e-3..4e-3 with 16 probes). The per-point variances in the E-step need an exact diagonal:
    the truncated basis (exact for the restricted model) or selected inversion on the sparse
    Cholesky `Geometry::Sparse::SparseLLT` (Takahashi; Bekas-Kokiopoulou-Saad probing; verify).
    Hutchinson's per-entry error (median 21%, max 100% at 16 probes) is not acceptable there.
  - The heat prior's precision is used only in the basis.
- **Subsamples.** Build the basis/L_w on the full mesh and restrict rows to the BCPD subsample.
  The full-source interpolation (`CoherentPointDrift.cpp:940-966`) is Gaussian-specific
  (Nystroem extension) and needs a basis-restriction path for non-Gaussian regularizers.
- **Point clouds.** The METHOD-064 Laplacian fixes apply: Belkin-Niyogi scaling, density
  normalization, and normal-consistency weights against shortcut edges.
- **GPU cost (corrected).**
  - Basis variant: forming Phi^T D Phi is O(m k^2) per EM iteration, plus O(k^3). With k = 500
    and m = 1e5 that is 2.5e10 flop per iteration, so it suits moderate k.
  - CG variant: iterations scale with sqrt(a mu_max / min p). Warm starts saved 0..10% in
    Fable's run; the gain comes from a shrinking a = lambda sigma^2.
  - At `ChunkDispatches` = 2048, about 227 CG iterations per chunk, early EM iterations take
    several chunks.
  - The objective and sigma^2 need V on the host unless their reductions move to the device
    (RUNTIME-294).
- Literature to verify: Myronenko-Song; Hirose (BCPD, geodesic BCPD); Belkin-Niyogi;
  Coifman-Lafon; Sharp-Crane 2020; Feragen et al. 2015; Duchon; Lindgren-Rue-Lindstroem
  (SPDE/Matern sparse precisions, a possible principled form of the n = 2 prior); Takahashi
  selected inversion.

## Acceptance criteria
- [ ] CPU reference per regularizer; `Coherence()` matches the prior; the NLL plus the prior
      decrease monotonically on the fixtures.
- [ ] Analytic filter check with p_i = c m_i (or unit mass), stating c; the heat-kernel Woodbury
      path agrees with a dense solve.
- [ ] Normal-consistent Gaussian:
  - the Gram matrix is PD on a fixture with opposed normals (smallest eigenvalue > 0);
  - kappa = 0 is bitwise the current kernel;
  - the oriented form equals the 6-D Gaussian embedding;
  - on two touching parts with opposed normals (a folded strip, the Vlasic hand at the hip) the
    cross-part coupling drops.
- [ ] Mesh-independence test: the spike ratio stays bounded under refinement for n = 2 and the
      heat kernel; n = 1 is documented as growing.
- [ ] A zero-weight component (forced P1 underflow) still solves (epsilon shift or pinning) and
      is reported.
- [ ] BCPD: default mode through CG; with variance terms on, an exact diagonal (basis or
      selected inversion); Hutchinson only for the trace, with its error measured.
- [ ] Subsample: basis on the full mesh restricted to the samples; the full-source
      interpolation matches a full-resolution run within a stated tolerance.
- [ ] GPU: CG iterations per EM iteration against a = lambda sigma^2, and chunks per EM
      iteration, reported (cold vs warm start measured, not assumed); parity with the CPU
      reference within the CG tolerance.
- [ ] Benchmark on the Vlasic meshes (first verify that the release keeps vertex correspondence
      across frames) and the existing CPD fixtures: accuracy, time and iterations per
      regularizer; sealed evidence. The Gaussian option is bitwise unchanged on the unchanged
      backend.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged two point sets; the geodesic and LBO options need source topology or a kNN graph. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds (LBO/geodesic built per domain). |
| RuntimeModule | Existing CPD operations; the eigenbasis comes from the Laplacian eigenbasis module, the CG from the existing sparse CG workspace. |
| Config/agent | New `regularizer` enum (gaussian, heat_kernel, rational) plus `diffusion_time`, `basis_size`, `rational_order` (1 or 2, default 2), `normal_consistency` (kappa, 0 = off) with `normals` (a vec3 property ref) and `normals_oriented`; for BCPD with variance terms, `diagonal` (basis, selected_inversion); the default keeps today's results. |
| UI | CPD panel Model section: regularizer combo, only its parameters shown. |
| Publication | Unchanged (displacement/positions). |
| End-to-end tests | Editor-command test per regularizer; gpu;vulkan smoke once the device M-step lands. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
