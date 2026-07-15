# Boundary First Flattening

## Citation

- **Title:** Boundary First Flattening
- **Authors:** Rohan Sawhney and Keenan Crane
- **Venue / Year:** ACM Transactions on Graphics 37(1), Article 5, 2017
- **DOI:** 10.1145/3132705
- **URL:** https://doi.org/10.1145/3132705
- **Preprint:** https://arxiv.org/abs/1704.06873

## Core claim

Boundary First Flattening constructs a conformal map by solving first for
compatible boundary data through a Dirichlet-to-Neumann formulation and then
extending the resulting planar boundary over the interior. This formulation
supports direct boundary-shape control while retaining a linear conformal
solve.

This package records a bounded subset of the paper. It does not claim complete
feature parity with the publication or its reference application.

## Mathematical formulation

Let `L` be the cotangent Laplacian of a connected triangle disk, partitioned
into interior and boundary degrees of freedom. Eliminating the interior
produces a boundary Dirichlet-to-Neumann operator that relates boundary log
scale factors to compatible boundary curvature data. The boundary data is
integrated into a best-fit closed planar curve. The automatic and target-length
modes then use the paper's holomorphic extension; the target-angle mode uses
harmonic extension so prescribed sharp corners are retained.

The repository exposes three boundary modes:

1. **`AutomaticConformal`.** Construct compatible free-boundary scale data
   from the input geometry; no target array is supplied by the caller.
2. **`TargetLengths`.** One positive finite target is supplied per boundary
   edge, in UV length units. The targets induce boundary scale factors. Closure
   and discretization can prevent exact realization, so the contract is an
   approximate fit with an explicit length/closure residual.
3. **`TargetAngles`.** One finite exterior turning angle is supplied
   per boundary vertex, in radians. The prescribed total must equal `2*pi`
   within the documented tolerance; inconsistent data fails closed rather than
   being silently projected.

All modes retain the shared conformal-quality diagnostics. Scale, translation,
and rotation normalization must be deterministic so equivalent runs have a
stable output convention.

## Inputs and outputs

- **Inputs:** finite connected manifold triangle mesh; one boundary loop;
  Euler characteristic one; selected boundary mode and its mode-specific
  target array; finite solver/validation tolerances.
- **Outputs:** one finite UV per stored vertex on success, shared
  `ParameterizationDiagnostics`, boundary length/angle residuals, closure
  residual, and explicit solver status. Failure carries no UV payload.

## Degenerate and edge cases

- Empty, non-triangle, disconnected, non-manifold, closed, multiple-boundary,
  or positive-genus inputs fail before assembly.
- Non-finite positions or parameters, non-positive target lengths, mismatched
  target-array cardinality, and inconsistent target-angle totals fail closed.
- Singular factorization, non-finite intermediate/output values, a boundary
  that cannot be closed within tolerance, or diagnostics with no usable faces
  report failure without retaining partial UVs.
- Cone singularities do not repair topology in this contract. Interior cones,
  automatic cone placement, seam generation, and cutting are excluded.
- BFF is used here as a conformal objective; this contract does not guarantee
  area preservation.

## Implementation notes

- `METHOD-023` owns the deterministic `cpu_reference` implementation on the
  shared `Geometry.Parameterization` strategy surface.
- The implementation uses geometry-owned cotangent assembly,
  `Geometry.Sparse::SparseLDLT`, boundary-loop extraction, and shared
  parameterization diagnostics. Eigen types must not cross the public API.
- `RUNTIME-176` owns config serialization, stable strategy-token conversion,
  mesh-property writeback, undo, and the UI-facing view model; no runtime or ECS
  dependency belongs in the geometry method.
- Correctness tests and the manifest-backed PR-fast smoke are present. No
  comparative performance claim is asserted by this package.
