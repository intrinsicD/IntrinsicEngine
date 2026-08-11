# Numerical Robustness Policy

Method implementations must define stable behavior for non-ideal inputs.

## Required handling

- Degenerate geometry (zero-area faces, duplicate vertices, non-manifold edges).
- Ill-conditioned solves and failed convergence.
- Empty/undersized input domains.
- Precision mode assumptions and tolerances.

## Diagnostics requirements

Results must report:

- Convergence or failure reason.
- Iteration counts or stopping criteria where applicable.
- Applied fallback path.
- Quality/error metrics used by tests and benchmarks.

## Worked example: curvature-tensor estimation (Taubin)

`Geometry::Curvature::ComputeCurvatureTensor` uses the edge-dihedral estimator
retained under the framework24 "Taubin" compatibility name. For every finite
interior edge it precomputes

```
M_e = beta_e (|e| / 2) t_e t_eᵀ
```

Here `beta_e` is the signed dihedral between the edge's two oriented face
normals and `t_e` is the unit edge tangent. Each vertex sums the contributions
incident to itself and its one-ring neighbours, normalizes by their mixed area,
and restricts the signed tensor to the finite oriented vertex tangent plane.
A closed-form 2×2 decomposition avoids a normal/zero-principal ambiguity; because
the hinge measures bend across the edge, each eigenvalue is paired with the
complementary eigenvector. Three simultaneous nonnegative-cotan passes smooth
the two principal values without making results depend on traversal order.

The sign is converted to the module's established positive-outward-convex
convention. Mean and Gaussian curvature in `ComputeCurvature` are then derived
from the same smoothed principal values, never from a second estimator. The
standalone Meyer mean/Gaussian functions remain separate explicit operators.
The compatibility name does not imply that this is the vertex-neighbour
directional-curvature quadrature in Taubin's ICCV 1995 paper.

Fail-closed policy, never emitting NaN/Inf and never firing an assert:

- Boundary edges have no dihedral contribution; a boundary vertex remains
  estimable when its two-ring support contains valid interior hinges.
- Deleted, isolated, flat, zero-area, degenerate, or non-finite support writes
  finite zero principal values and zero-vector directions.
- Zero-length edges and invalid/non-finite face normals are skipped.
- Empty meshes and meshes with no faces → `nullopt`.
