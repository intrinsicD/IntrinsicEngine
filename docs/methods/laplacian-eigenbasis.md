# Spectral modes

**View → Spectral Modes** (also under Mesh/Graph/PointCloud → Processing) computes
the k smallest eigenpairs of a modal operator and publishes each mode as a property
`<prefix><j>` (default `eigen_0`, `eigen_1`, …). The panel shows the eigenvalues as
a bar chart and a show button for the selected mode, and can additionally publish a
multi-scale modal signature and a modal distance to a source row. This is
IntrinsicEngine's counterpart to Framework24's eigendecomposition viewer, extended
with the operators of Hildebrandt, Schulz, von Tycowicz and Polthier, "Modal shape
analysis beyond Laplacian" (CAGD 29(5), 2012).

## Operators

| Operator | Domains | Matrix | Modes |
| --- | --- | --- | --- |
| Graph Laplacian | all eight | `A = D - W` over the shared sample graphs of [property smoothing](property-smoothing.md) and [harmonic fields](harmonic-field.md): kNN weights on any domain, or uniform and nonnegative (clamped) cotangent weights on mesh vertices | float |
| Modified Dirichlet energy `E_D^N` | mesh vertices | `A_ij = <N_i, N_j> S_ij` (paper eq. 12): exact cotan stiffness `S`, area-weighted unit vertex normals | float |
| Thin-shell vibration | mesh vertices | rest-state Hessian of the discrete-shells energy (Grinspun et al. 2003), `sum_i w_i grad f_i grad f_i^T` (paper Lemma 1) over dihedral angles (`w = k_F 3|e|^2/A_e`), edge lengths (`w = k_L/|e|`) and triangle areas (`w = k_A/A`) | vec3 displacement |

`E_D^N(u)` is the Dirichlet energy of the normal variation `u N`; it equals the
Laplacian on planar meshes, and its constant function no longer lies in the kernel
on curved surfaces (the extra term approximates `½∫u²(κ1² + κ2²)`, paper eq. 11), so
low modes vanish near high curvature. The thin-shell Hessian is 3#V × 3#V and has the
six rigid motions in its nullspace; its modes are the linearized vibration modes of
the shell. Kernels live in `Geometry.ModalAnalysis`; the mass `M` is the lumped vertex
area (mesh operators: barycentric thirds as in the paper, replicated per coordinate
for the shell; graph Laplacian: the DEC lumped area) or unit.

`Geometry.Sparse::SolveSymmetricGeneralizedEigen` solves `A z = lambda M z` by
shift-invert block subspace iteration over one `SparseLDLT` factorization (see the
linear algebra policy in [the geometry architecture](../architecture/geometry.md)).
Eigenvalues are ascending, modes M-orthonormal with the largest-magnitude entry
positive; each pair must reach the normwise backward error tolerance, and otherwise
nothing is published. For the graph Laplacian, connected components each contribute
a zero eigenvalue; clamped cotangent weights make it a graph operator rather than
signed FEM. Mesh operators run over non-isolated vertices; isolated vertices publish
zero.

## Signatures and distances

With the first `skip_modes` pairs removed (6 rigid motions for the shell, typically
0 or 1 otherwise):

- signature `S_t(v) = sum_j exp(-lambda_j t) |Phi_j(v)|^2` (paper eqs. 27, 30): the
  vibration signature for the shell, the feature signature for `E_D^N`, and the heat
  kernel signature of Sun et al. (2009) for the cotan Laplacian;
- distance `delta(v, s) = (∫ ((S_t(v) - S_t(s)) / sum_k exp(-lambda_k t))^2 d log t)^½`
  over `[t_min, t_max]` (paper eq. 31), midpoint rule on `distance_samples` uniform
  log-scale intervals.

The scale range is Sun et al.'s `[4 ln 10 / lambda_last, 4 ln 10 / lambda_first]`
over the used positive eigenvalues; the signature is evaluated at
`t = t_min^(1-s) t_max^s` for `signature_scale = s`. Result diagnostics report the
range and `t`.

## Engine integration

| Surface | Contract |
| --- | --- |
| Least-structured input | Sample positions of any canonical domain plus operator, weights, mass choice and k; mesh operators need triangle-mesh vertices. |
| Entity/domain sources | All eight canonical domains for the graph Laplacian via the shared property-graph capture; mesh operators, mesh weights and lumped mass need mesh vertices and vertex positions. |
| Runtime owner | `Runtime.MeshFieldOperations.Eigenbasis.cpp`. |
| Config/agent | `sandbox.laplacian_eigenbasis` (operator, domain, positions, weight, neighbors, spatial_sigma, lumped_mass, count 1–256, output_prefix, max_iterations, tolerance, shell_flexural/length/area, skip_modes, signature_output, signature_scale, distance_source, distance_output, distance_samples). Payloads without the newer fields keep the graph-Laplacian defaults. |
| UI | View → Spectral Modes with operator selection, shell weights, a signature/distance section, spectrum bar chart and show buttons (`Runtime.EditorPropertyWidgets::DrawEditorSpectrumBarWidget`). |
| Publication | k float or vec3 mode properties plus optional float signature and distance in one guarded undo/redo transaction; outputs of another storage type or duplicate names are rejected. |
| Verification | `Test.SparseEigensolver.cpp`, `Test.ModalAnalysis.cpp` (finite-difference gradients and Hessian, planar and sphere identities, rigid nullspace, signature invariants), `Test.LaplacianEigenbasisOperations.cpp`, and the ImGui actions in `Test.SandboxProcessingPanels.cpp`. |

## Limitations

- CPU only and synchronous; cost grows with k (block size about 2k) and the
  factorization fill; the shell system has three times the unknowns. In the
  unoptimized `ci` build a 10,000-vertex grid took about 5 s for k = 10 and 25 s for
  k = 30 with the graph Laplacian; this is a debug-build observation, not a
  performance claim.
- Only the smallest eigenpairs; no spectrum slicing or interior eigenvalues.
- Vibration modes are shown as vec3 colors; an animated displacement preview along a
  mode (paper Fig. 4) is not implemented.
- The shell term weights have different units (`1/L^2`, `1/L`, `1`), so their
  balance depends on the mesh scale, as in the paper.
- The distance source is a row index; there is no viewport picking for it.
- Wave kernel signatures, spectral filtering and cross fields remain consumers still
  to be built on this solver (METHOD-006, METHOD-024).
