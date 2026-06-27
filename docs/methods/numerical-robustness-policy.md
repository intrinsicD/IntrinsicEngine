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

`Geometry::Curvature::ComputeCurvatureTensor` estimates the per-vertex 3×3
curvature tensor (Taubin, "Estimating the tensor of curvature of a surface from a
polyhedral approximation", ICCV 1995). For each 1-ring edge `(i,j)` it accumulates

```
M_i = Σ_j w_ij · κ_ij · T_ij T_ijᵀ,   Σ_j w_ij = 1
κ_ij = 2 nᵢ·(x_j − x_i) / ‖x_j − x_i‖²       (directional curvature)
T_ij = normalize((I − nᵢnᵢᵀ)(x_j − x_i))     (tangent direction)
w_ij ∝ area of the faces incident to edge (i,j)
```

`M_i` is decomposed with the shared `Geometry::PCA::SymmetricEigen3`. The
eigenvector aligned with the vertex normal is discarded; the two tangent
eigenvalues `λ_a, λ_b` are read off `M_i` via the Rayleigh quotient (the tensor is
not positive semi-definite, so the solver's non-negative eigenvalue clamp cannot
be used) and the principal curvatures are recovered as `κ₁ = 3λ_a − λ_b`,
`κ₂ = 3λ_b − λ_a`, each aligned to the direction it came from.

Fail-closed policy (GEOM-005/GEOM-007), never emitting NaN/Inf and never firing an
assert:

- Flat 1-ring (tensor numerically zero), boundary (open) 1-ring, and zero-area
  1-ring → write the zero-vector sentinel direction and keep the scalar
  (H/K-derived) principal curvatures.
- Edges parallel to the normal or with zero length are skipped from the
  accumulation.
- Empty meshes and meshes with no faces → `nullopt`.
