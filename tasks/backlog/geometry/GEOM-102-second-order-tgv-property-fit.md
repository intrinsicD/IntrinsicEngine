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

Open design decision before implementation: where `w` lives and what `E` is on
each domain — face gradients and a face-adjacency (dual) operator on mesh
vertices, versus per-edge slopes on kNN and graph domains, where a symmetrized
derivative is not canonical.

## Acceptance criteria

- [ ] Design decision recorded for `w` and `E` per canonical domain, or domains explicitly deferred.
- [ ] TGV² reproduces affine ramps exactly (no staircasing) and keeps a step, with closed-form tests.
- [ ] Config, UI and publication follow the existing Variational fit surface.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | 1–4 channel signal on a weighted graph plus the second-order operator chosen above. |
| Compatible entity sources | Mesh vertices first; other domains per the design decision. |
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
