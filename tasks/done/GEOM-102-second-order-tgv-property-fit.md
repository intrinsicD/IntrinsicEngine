---
id: GEOM-102
theme: I
depends_on: [GEOM-101]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; the slice owes its own tests and smoke-benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# GEOM-102 — Second-order (TGV) penalty for the variational property fit

## Completion — 2026-09-26
Commit: the enclosing `claude/variational-fit-follow-ups` commit.
Implemented on `claude/variational-fit-follow-ups`; its commit records this
retirement. CPUContracted on every canonical domain through ADMM only; no
reweighted reference exists for second order, so the quadratic case is
checked against a dense oracle and the L1 case against affine reproduction,
staircasing and the smoke's ramp-with-jump comparison.

## Goal

Add a total generalized variation (TGV²) smoothness penalty to the
property-smoothing **Variational fit**, so edge-preserving fits of smoothly
varying fields such as mean curvature do not staircase into plateaus.

## Context

TV fits are piecewise constant. Mesh-denoising work moved to second order for
this reason: relaxed second-order TGV on triangulated surfaces (SIAM J. Imaging
Sci. 2022, doi:10.1137/21M1397945), TV of the normal with a shape Newton method
(SIAM J. Sci. Comput. 2025, doi:10.1137/24M1646121) and TGV of the normal field
(arXiv 2507.13530). TGV² minimizes `alpha1 |D u - w| + alpha0 |E w|` over an
auxiliary first-order field `w`; it needs the ADMM backend of GEOM-101.

Design decision (operator, 2026-09-26): non-local TGV after Ranftl, Bredies and
Pock (ECCV 2014). Every row carries a gradient `g` in the sample space; edges
penalize `rho(|u_a - u_b - <(g_a + g_b)/2, x_a - x_b>|)` and
`alpha rho(h |g_a - g_b|)`. It works on all eight domains with the existing
sample graphs; mesh face gradients with a dual graph were not chosen.

## Acceptance criteria

- [x] Design decision recorded for `w` and `E` per canonical domain, or domains explicitly deferred.
- [x] TGV² reproduces affine ramps exactly (no staircasing) and keeps a step, with closed-form tests.
- [x] Config, UI and publication follow the existing Variational fit surface.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | 1–4 channel signal on a weighted graph plus the second-order operator chosen above. |
| Compatible entity sources | All eight canonical domains through the shared sample positions. |
| RuntimeModule | `Runtime.MeshFieldOperations.Smoothing.cpp`. |
| Config/agent | `sandbox.property_smoothing` smoothness-penalty value and second weight. |
| UI | View → Smooth Property, Variational fit block. |
| Publication | Unchanged single guarded output transaction. |
| End-to-end tests | `Test.VariationalFit.cpp`, `Test.PropertySmoothingOperations.cpp`. |

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R "VariationalFit|PropertySmoothing" --timeout 60
```
