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

## Worked example: deterministic Framework24 curvature compatibility

`Geometry::Curvature::ComputeCurvatureTensor` is a deterministic numerical port
of Framework24 revision `6dd50a82`'s default
`CurvatureTaubin(mesh, 0, false, Policy::Sequential)`. The name is the
compatibility target; it does not reclassify the edge-dihedral tensor as
Taubin's 1995 vertex-neighbour directional-curvature quadrature. For every
finite interior edge it precomputes

```
M_e = beta_e (|e| / 2) t_e t_eᵀ
```

Here `beta_e` is Framework24's corrected signed dihedral between the edge's two
oriented face normals and `t_e` is the unit edge tangent. Each center sums its
own incident hinges and divides by its own corrected mixed Voronoi area. This is
the reference's one-ring support; open-boundary centers use the same direct
tensor evaluation as interior centers.

Eigen's symmetric self-adjoint solver discards the eigenvalue with smallest
absolute magnitude as the tensor-normal mode. The two remaining algebraically
ordered values pair directly with their tensor eigenvectors. Framework24's
corrected sign, Meyer acute/obtuse area coefficients, `[-19.1, 19.1]` cotan
clamp, and direct value/direction pairing remain part of the compatibility
contract. The default zero smoothing-step count publishes principal scalars
and directions directly. Mean and Gaussian curvature in `ComputeCurvature` are
derived from those principal values, never from a second estimator; the
standalone Meyer mean/Gaussian functions remain separate explicit operators.

Fail-closed policy, never emitting NaN/Inf and never firing an assert:

- Boundary edges have no dihedral contribution, but supported boundary centers
  are evaluated directly from their available one-ring tensor support.
- Triangle quality is the scale-independent ratio `2A/l_max^2`. The `3.5e-4`
  floor is the conservatively rounded square root of machine epsilon for the
  public float-position storage; below it, inverse-area terms amplify position
  roundoff beyond the same square-root-epsilon budget. Degenerate, non-finite,
  non-triangular, or quality-at/below that floor support invalidates
  every incident tensor center that consumes it. Those centers retain finite
  zero scalar and direction sentinels, so unreliable geometry cannot publish a
  non-finite field.
- A valid flat neighbourhood is supported and remains exactly zero.
- Zero-length edges and invalid/non-finite face normals are skipped.
- Empty meshes and meshes with no faces → `nullopt`.

The public `Geometry::Smoothing::CotanSmoothVertexProperty` operation is a
separate reusable contract and is not used by this curvature path. It accepts
vertex properties of `float`, `double`, and canonical persisted
`glm::vec2/vec3/vec4`, computes every damped iteration from a separate read
buffer, clamps cotan weights nonnegative, and validates parameters,
property/mask cardinality, geometry, and live values before mutation. Its
optional active-vertex mask excludes unreliable values from both their own rows
and every neighbour average.
