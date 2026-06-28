# A Heat Method for Generalized Signed Distance

## Citation

- **Title:** A Heat Method for Generalized Signed Distance
- **Authors:** Nicole Feng and Keenan Crane
- **Venue / Year:** ACM Transactions on Graphics 43(4), SIGGRAPH 2024
- **DOI:** 10.1145/3658220
- **URL:** https://nzfeng.github.io/research/SignedHeatMethod/

## Core Claim

The signed heat method approximates generalized signed distance directly on a
domain even when the source geometry is noisy, incomplete, or not a perfect
boundary. It replaces explicit repair with a PDE pipeline: diffuse oriented
source normals, normalize the diffused vector field, then recover the scalar
whose gradient best matches that unit field.

## Mathematical Formulation

For a source boundary `Omega` embedded in a domain mesh `M`, the paper describes
three main steps:

1. Diffuse source normals for a short time `t`, using one backward-Euler heat
   step.
2. Normalize the diffused vector field to approximate the gradient direction of
   an unknown signed distance function.
3. Solve a Poisson problem so `grad(phi)` best matches the normalized field in
   least-squares sense.

The IntrinsicEngine CPU reference in Slice 1 follows this contract with existing
vertex DEC operators: `L` is the weak cotan Laplacian, `M` is the lumped vertex
mass, `(M + tL)` diffuses the oriented boundary-normal impulse, and a
regularized Poisson solve recovers the per-vertex scalar. The implementation
documents this as a deterministic vertex-based approximation rather than the
paper's full edge-based Crouzeix-Raviart connection discretization.

## Inputs And Outputs

- Inputs: a finite halfedge triangle mesh, an oriented span of halfedges forming
  the source curve, and `SignedHeatParams`.
- Outputs: per-vertex signed distance, per-vertex source markers, and
  diagnostics covering solver status, boundary degeneracy, factorization state,
  time step, and output range.

## Degenerate/Edge Cases

- Empty meshes, empty source curves, non-finite parameters, meshes without
  valid triangle faces, or source curves that do not touch live vertices fail
  closed with explicit diagnostics.
- Open or duplicate source curves are allowed when the linear solves remain
  well-posed; the result status is `DegenerateBoundaryInput` and the output is
  finite when successful.
- Non-finite matrix or result values return `NonFiniteResult` and do not claim
  success.

## Implementation Notes

- CPU reference only; no optimized CPU, GPU, point-cloud, or volumetric backend.
- Uses `Geometry.Sparse::SparseLDLT` for the heat and Poisson systems.
- The Poisson null space is handled by mass regularization and a final weighted
  shift so the source curve has mean zero value.
- Current correctness coverage uses a deterministic flat-grid square boundary
  baseline (`quality_error_l2 < 0.40`) plus orientation, degenerate-boundary,
  invalid-input, and determinism regressions.
- The public C++ module exposes no Eigen types.
