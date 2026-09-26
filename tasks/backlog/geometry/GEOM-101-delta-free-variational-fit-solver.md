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
the harmonic-field smoke loses accuracy below `delta = 1e-9`. The literature
alternatives are ADMM / split Bregman (Goldstein–Osher 2009), Chambolle–Pock
primal-dual (2011) and primal-dual Newton (Chan–Golub–Mulet 1999). With
`z = D u`, ADMM keeps `lambda M + rho D^T D` fixed: one factorization, then
back-substitutions, soft shrinkage and a box or ball projection for tolerances.

## Acceptance criteria

- [ ] An ADMM (or primal-dual) backend minimizes the undamped L1/TV energy and reports backend identity, iterations and primal/dual residuals.
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
