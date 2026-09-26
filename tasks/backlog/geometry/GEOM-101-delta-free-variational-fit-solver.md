---
id: GEOM-101
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; the slice owes its own tests and smoke-benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# GEOM-101 — Delta-free variational-fit solver

## Goal

Solve the property-smoothing **Variational fit** (`HarmonicField::FitProperty`)
with total-variation and L1 penalties exactly, without the delta smoothing that
makes the current reweighting iteration slow and ill-conditioned.

## Context

The CPU reference minimizes the delta-smoothed energy by iteratively reweighted
least squares (lagged diffusivity; Vogel–Oman 1996, Chan–Mulet 1999). It
converges linearly, needs hundreds of refactorizations on kNN point clouds with
`delta = 1e-6`, and its weights grow like `1/delta`, so the 256-row TV step of
the harmonic-field smoke loses accuracy below `delta = 1e-9`.

Literature options reviewed on 2026-09-26 (abstracts only):

1. **Dynamic smoothing in IRLS.** Kümmerle, Mayrink Verdun and Stöger (NeurIPS
   2021, basis pursuit) and Peng, Kümmerle and Vidal (NeurIPS 2022, robust ℓ1
   regression) shrink the smoothing parameter from the current iterate and prove
   global linear convergence to the exact ℓ1 minimizer; fixed smoothing only
   reaches a delta-approximation. Proofs cover sparse recovery and regression,
   not graph TV. It helps only when delta should tend to zero; a decade-step
   delta continuation tried during the first slice did not reduce the ~270
   iterations of the kNN case. Not adopted: ADMM solves `delta = 0` exactly.
2. **ADMM / primal-dual with one factorization.** Goldstein–Osher split
   Bregman (2009), Chambolle–Pock (2011). With `z = D u` and the data and bound
   residuals split off, the u-update matrix `L + 2M` is independent of the data
   weight and the penalty parameter, so one factorization serves every
   iteration, every penalty-parameter change and every noise-level bisection
   step; penalties and bounds become closed-form proximal steps. **Selected.**
3. **Graph-TV specific solvers.** Cut pursuit (Landrieu–Obozinski 2017;
   Raguet–Landrieu, ICML 2018, including nondifferentiable separable terms such
   as ℓ1 data and box constraints; parallel cut pursuit 2019), active-set
   reconditioning (Ye, Möllenhoff, Wu, Cremers, AISTATS 2020) and exact dynamic
   programming on trees (Kolmogorov, Pock, Rolínek, SIIMS 2016). Candidates for
   a later optimized backend measured against ADMM.
4. **Second order.** Semismooth Newton augmented Lagrangian (Li, Sun, Toh,
   SIAM J. Optim. 2018) for lasso/fused lasso, asymptotically superlinear.

Staircasing of TV on smoothly varying fields such as curvature is tracked
separately as a second-order penalty in
[GEOM-102](GEOM-102-second-order-tgv-property-fit.md).

## Acceptance criteria

- [ ] An ADMM backend minimizes the same energy (parity with IRLS for `delta > 0`) and the undamped L1/TV energy for `delta = 0`, and reports backend identity, iterations and primal/dual residuals.
- [ ] Parity against the IRLS reference as delta shrinks, the two-row and step closed forms, and the perturbation-optimality test in `Test.VariationalFit.cpp`.
- [ ] Optional Euclidean-ball tolerances for vector properties.
- [ ] Smoke benchmark records iterations and runtime against IRLS on a kNN point cloud; `docs/methods/property-smoothing.md` limitations updated.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: 1–4 channel signal on a nonnegative weighted graph. |
| Compatible entity sources | Unchanged: all eight canonical domains through the shared property graph. |
| RuntimeModule | `Runtime.MeshFieldOperations.Smoothing.cpp` dispatch. |
| Config/agent | `sandbox.property_smoothing`; a solver field if both backends stay selectable. |
| UI | View → Smooth Property, Variational fit block. |
| Publication | Unchanged single guarded output transaction. |
| End-to-end tests | `Test.VariationalFit.cpp`, `Test.PropertySmoothingOperations.cpp`. |

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R "VariationalFit|PropertySmoothing" --timeout 60
```
