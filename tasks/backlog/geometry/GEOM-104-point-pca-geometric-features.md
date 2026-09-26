---
id: GEOM-104
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-104 — Per-point PCA geometric features

## Goal
Compute the standard covariance-eigenvalue features per point — linearity,
planarity, sphericity (scattering), anisotropy, omnivariance, eigenentropy, sum of
eigenvalues, surface variation (change of curvature), verticality — and publish
them as scalar properties. Nothing in IntrinsicEngine covers them today.

## Context
- Demantké, Mallet, David & Vallet, *Dimensionality based scale selection in 3D lidar point clouds*, ISPRS 2011;
  Weinmann, Jutzi, Hinz & Mallet, *Semantic point cloud interpretation based on optimal neighborhoods, relevant features and efficient classifiers*, ISPRS J. 2015.
- Framework24 `bcg_matrix_pca_geometric_features.h` as a checklist.
- Reuse `Geometry.PCA`, the shared kNN/radius neighborhoods and the property-graph sample capture.

## Acceptance criteria
- [ ] Kernel with analytic tests (line, plane, isotropic blob give the expected dominant feature; degenerate/collinear neighborhoods fail or clamp explicitly).
- [ ] Optional eigenentropy-optimal neighborhood size (Weinmann 2015) as a separate, tested option.
- [ ] Editor operation publishing the selected features as float properties on any point domain; panel with show buttons; contract and ImGui tests.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | vec3 samples plus neighborhood parameters. |
| Compatible entity sources | All canonical domains via sample capture. |
| RuntimeModule | Point-analysis operations. |
| Config/agent | `sandbox.pca_features`. |
| UI | View → PCA Features and domain redirects. |
| Publication | Named float properties, one transaction. |
| End-to-end tests | Kernel analytic tests, contract and ImGui tests. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PcaFeatures' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
