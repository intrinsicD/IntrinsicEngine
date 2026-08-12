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

## Worked example: PMP-compatible edge-dihedral curvature estimation

`Geometry::Curvature::ComputeCurvatureTensor` uses the edge-dihedral estimator
implemented by PMP's tensor branch. PMP associates that branch with the
Cohen-Steiner--Morvan normal-cycle formulation; it is not Taubin's 1995
vertex-neighbour directional-curvature quadrature. For every finite interior
edge it precomputes

```
M_e = beta_e (|e| / 2) t_e t_eᵀ
```

Here `beta_e` is the signed dihedral between the edge's two oriented face
normals and `t_e` is the unit edge tangent. Each vertex sums the contributions
incident to itself and its non-boundary one-ring neighbours and normalizes by
their mixed area. A local signed symmetric 3×3 Jacobi decomposition then follows
the PMP reference: the eigenvalue with smallest absolute magnitude is discarded
as the tensor-normal mode and the remaining ordered pair supplies the principal
scalars. Because the hinge measures bend across the edge, each scalar direction
uses the complementary tangent eigenvector. Direction publication additionally
uses the geometric normal to disambiguate cylindrical zero modes. Boundary
scalars are interpolated from supported non-boundary neighbours before three
simultaneous updates of `0.5 * old + 0.5 * nonnegative_cotan_average`.

The sign is converted to the module's established positive-outward-convex
convention. Mean and Gaussian curvature in `ComputeCurvature` are then derived
from the same smoothed principal values, never from a second estimator. The
standalone Meyer mean/Gaussian functions remain separate explicit operators.
Fail-closed policy, never emitting NaN/Inf and never firing an assert:

- Boundary edges have no dihedral contribution; boundary centers are not
  estimated directly and become supported only by interpolation from valid
  non-boundary neighbours.
- Triangle quality is the scale-independent ratio `2A/l_max^2`. The `3.5e-4`
  floor is the conservatively rounded square root of machine epsilon for the
  public float-position storage; below it, inverse-area terms amplify position
  roundoff beyond the same square-root-epsilon budget. Degenerate, non-finite,
  non-triangular, or quality-at/below that floor support invalidates
  every tensor center that consumes it. Those centers retain finite zero scalar
  and direction sentinels and are excluded from smoothing rows and neighbour
  support, so an unreliable spike cannot diffuse into the supported field.
- A valid flat neighbourhood is supported and remains exactly zero.
- Zero-length edges and invalid/non-finite face normals are skipped.
- Empty meshes and meshes with no faces → `nullopt`.

The damping operation is the public
`Geometry::Smoothing::CotanSmoothVertexProperty` contract, not a
curvature-private loop. It accepts vertex properties of `float`, `double`, and
canonical persisted `glm::vec2/vec3/vec4`, computes every iteration from a
separate read buffer, clamps cotan weights nonnegative, and validates parameters,
property/mask cardinality, geometry, and live values before mutating the
property. Its optional active-vertex mask excludes unreliable values from both
their own rows and every neighbour average.
