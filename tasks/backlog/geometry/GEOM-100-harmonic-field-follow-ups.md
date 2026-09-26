---
id: GEOM-100
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested backlog of follow-ups to the harmonic-field CPU reference; each slice owes its own tests and any benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources]
---
# GEOM-100 — Harmonic-field follow-ups and solver consolidation

## Goal

Extend `Geometry.HarmonicField` to the use cases its first slice leaves open, and
move the repository's duplicated constrained-Laplacian solves onto it.

## Current state and scope

The [harmonic-field method](../../../docs/methods/harmonic-field.md) solves
harmonic, biharmonic and triharmonic fields with hard/soft constraints, Poisson
sources and grounded pure-Neumann components, and publishes random-walker labels
and per-label weights on every element domain. An inventory on 2026-09-26 found the same
Dirichlet elimination hand-written in `Geometry.Parameterization.Harmonic.cpp`,
`Geometry.Parameterization.Bff.cpp`, `Geometry.HalfedgeMesh.Parameterization.cpp`
(LSCM pins) and `Geometry::Sparse::SolveCGShiftedFixed`, which has no
production caller since the unified property smoothing landed.

Independent slices:

1. **Displacement-form deformation.** Solve for displacements from rest
   positions to handle targets and add them back, so editing keeps surface detail.
2. **Gradient-divergence helper.** Build the Poisson source `div X` from a face
   vector field (gradient-domain editing, heat-method distances) on meshes.
3. **Complex / connection Laplacian and eigen solves.** Direction and cross-field
   design and spectral embeddings need complex weights and an eigensolver.
4. **Face dual graph.** Face-adjacency weights for face-domain fields and
   segmentation instead of kNN on face centers.
5. **Consolidation.** Route harmonic/Tutte parameterization and the BFF Dirichlet
   solve through the shared kernel or one shared elimination helper; decide
   whether `SolveCGShiftedFixed` stays.
6. **Bounded weights.** Inequality constraints (bounded biharmonic weights) need
   a QP solver; decide scope first.
7. **Hole-fill fairing.** `HalfedgeMesh::Repair` adds no interior vertices yet;
   refinement plus biharmonic fairing of the patch positions.
8. **Volumetric and edge domains.** Tetrahedral Laplacians (harmonic coordinates
   in cages) and edge 1-form (Hodge) Laplacians need their own operator sources.
9. **Signed cotangent weights.** Replace clamping by an intrinsic Delaunay
   Laplacian rather than admitting negative weights into the SPD assumption.
10. **GPU.** Bind through RUNTIME-269/GEOM-089 after CPU parity rules are set.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a signal with constrained rows and optional source on a nonnegative weighted graph; slice 4 adds face adjacency, slices 3 and 8 new operators. |
| Compatible entity sources | All eight canonical domains through the shared sample graph; face-dual weights apply to mesh faces only. |
| RuntimeModule | `Runtime.MeshFieldOperations.HarmonicField.cpp` with the shared `PropertyGraph` helpers. |
| Config/agent | Extend `sandbox.harmonic_field` through its single validator; new fields keep the null-for-unused convention. |
| UI | View → Harmonic Field and the domain Processing redirects. |
| Publication | Same-domain named outputs in one guarded transaction, including the per-label weights; slice 1 adds a displacement-to-position publication. |
| End-to-end tests | Extend `Test.HarmonicField.cpp` and `Test.HarmonicFieldOperations.cpp` per slice; consolidation keeps the existing parameterization and BFF tests unchanged. |

## Acceptance criteria

- [ ] Each implemented slice has analytic or dense-oracle tests and updates the method doc's use-case table.
- [ ] Consolidation leaves parameterization, BFF and LSCM results unchanged within their existing test tolerances.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R "HarmonicField|Parameterization|Bff" --timeout 60
```
