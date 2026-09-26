# Laplacian eigenbasis

**View → Laplacian Eigenbasis** (also under Mesh/Graph/PointCloud → Processing)
computes the k smallest eigenpairs of a sample-graph Laplacian and publishes each
eigenvector as a float property `<prefix><j>` (default `eigen_0`, `eigen_1`, …).
The panel shows the eigenvalues as a bar chart and a show button for the selected
eigenvector. This is IntrinsicEngine's counterpart to Framework24's
eigendecomposition viewer.

## Formulation

The operator is `A = D - W` over the shared sample graphs of
[property smoothing](property-smoothing.md) and [harmonic fields](harmonic-field.md):
kNN weights on any domain, or uniform and nonnegative (clamped) cotangent weights on
mesh vertices. The mass `M` is the DEC lumped vertex area (mesh vertices) or unit.
`Geometry.Sparse::SolveSymmetricGeneralizedEigen` solves `A z = lambda M z` by
shift-invert block subspace iteration over one `SparseLDLT` factorization (see the
linear algebra policy in [the geometry architecture](../architecture/geometry.md)).
Eigenvalues are ascending, eigenvectors M-orthonormal with the largest-magnitude
entry positive; each pair must reach the normwise backward error tolerance, and
otherwise nothing is published. Connected components each contribute a zero
eigenvalue; clamped cotangent weights make this a graph operator rather than signed
FEM, as in the smoothing and harmonic-field operations.

## Engine integration

| Surface | Contract |
| --- | --- |
| Least-structured input | Sample positions of any canonical domain plus weights, mass choice and k. |
| Entity/domain sources | All eight canonical domains via the shared property-graph capture; mesh weights and lumped mass need mesh vertices. |
| Runtime owner | `Runtime.MeshFieldOperations.Eigenbasis.cpp`. |
| Config/agent | `sandbox.laplacian_eigenbasis` (domain, positions, weight, neighbors, spatial_sigma, lumped_mass, count 1–256, output_prefix, max_iterations, tolerance). |
| UI | View → Laplacian Eigenbasis with spectrum bar chart and show button (`Runtime.EditorPropertyWidgets::DrawEditorSpectrumBarWidget`). |
| Publication | k float properties in one guarded undo/redo transaction; outputs of another storage type are rejected. |
| Verification | `Test.SparseEigensolver.cpp`, `Test.LaplacianEigenbasisOperations.cpp`, and the ImGui action in `Test.SandboxProcessingPanels.cpp`. |

## Limitations

- CPU only and synchronous; cost grows with k (block size about 2k) and the
  factorization fill. In the unoptimized `ci` build a 10,000-vertex grid took about
  5 s for k = 10 and 25 s for k = 30 (about 20 iterations); this is a debug-build
  observation, not a performance claim.
- Only the smallest eigenpairs; no spectrum slicing or interior eigenvalues.
- Spectral filtering, HKS/WKS descriptors and cross fields are consumers still to be
  built on this solver (METHOD-006, METHOD-024).
