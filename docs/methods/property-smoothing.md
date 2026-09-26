# Property smoothing

`Geometry.Smoothing` filters scalar or vector signals on a nonnegative weighted
graph. Runtime binds `float`, `double`, `vec2`, `vec3`, or `vec4` properties on
mesh vertices, edges, halfedges and faces; graph nodes, edges and halfedges; and
point-cloud points. Values need no prescribed name or provenance. Vector
channels share the neighborhood and bilateral range weight; vectors are not
renormalized.

For mesh fairing, open **View → Smooth Property**, select `v:position` as the
input and neighborhood positions, then choose **Overwrite input property**.
Choose **Implicit (backward Euler)**, **Nonnegative mesh cotangent** weights,
and **Lumped mesh area (implicit)** for area-aware diffusion. Enable **Pin mesh
boundary** to hold boundary vertices fixed. The same choices apply to any other
floating mesh-vertex field without moving positions.

## Formulation

For symmetric weights `W`, let `D` contain row sums. The selectable Laplacians
are the combinatorial `L = D-W` and random-walk `L = I-D^-1 W`; isolated rows
have zero Laplacian. Implicit smoothing also supports `L = M^-1(D-W)`
with positive lumped masses. All filters preserve isolated rows and optional
fixed rows. Runtime can pin mesh boundary vertices for any filter.

- **Averaging:** simultaneous `x <- x - lambda L x`. Combinatorial explicit
  steps divide lambda by the maximum weighted degree for stability. Lambda 1
  with random-walk normalization gives the neighbor average.
- **Spectral heat:** `x <- exp(-t L) x`, applying gain `exp(-t eigenvalue)`.
  Evaluation uses the nonnegative series
  `exp(-a) sum_k a^k/k! (I-L/rate)^k`, with `rate=1` for random walk or the
  maximum degree for combinatorial. Time is split so `a <= 1`; each split
  retains terms 0 through 18 and normalizes their coefficient sum. It does
  not compute or truncate an eigenbasis. The discarded Poisson mass for a
  split is bounded by the tail of this series; floating-point roundoff is
  additional. Time is expressed in the selected operator's units.
- **Implicit backward Euler:** solve `(M + dt (D-W)) x_new = M x_old` for
  each channel and iteration. `M` is the identity for combinatorial, weighted
  degree for random walk, or the DEC lumped vertex area for the mesh-area option.
  Geometry callers can supply arbitrary positive row masses. The system
  matrix is assembled once per run: fixed rows become identity rows and their
  coupling moves into the free right-hand side (reduced-system Dirichlet
  elimination), so the matrix stays symmetric positive definite. The default
  **direct** solver factors it once with `Geometry::Sparse::SparseLLT`
  (sparse Cholesky) and reuses the factor for every channel and iteration;
  it has no convergence tolerance. The **conjugate gradient** solver runs
  Jacobi-preconditioned `Geometry::Sparse::SolveCG` on the same matrix and is
  the parity reference. If the factorization fails, the direct setting falls
  back to CG and reports it in the diagnostic. Factorization cost dominates
  on large meshes with small time steps, where CG converges in few
  iterations; large steps or many iterations favor the direct solver.
  CG nonconvergence, nonfinite results, or output conversion failure publish
  nothing. Time step is positive and independent of explicit lambda; solver,
  CG tolerance and CG maximum iterations are serialized settings.
- **Taubin:** alternate positive lambda and negative mu updates on the fixed
  graph. Both combinatorial steps use the same maximum-degree scaling.
  These parameters define the filter polynomial; arbitrary parameters are
  not a promise of volume preservation or monotone attenuation.
- **Bilateral:** multiply spatial/topological weights by
  `exp(-||x_i-x_j||^2/(2 range_sigma^2))` each iteration. Random-walk averaging
  includes unit self weight, retaining isolated or range-separated samples.
  Combinatorial updates use the original maximum-degree bound. Range sigma
  is in property units, with the Euclidean norm over all vector channels.
- **Variational fit:** minimize
  `sum_edges w rho_s(|u_a-u_b|) + lambda sum_rows m rho_d(|u_i-f_i|)` subject
  to fixed rows and an optional per-channel tolerance `|u_ic-f_ic| <= r_i`.
  Norms are Euclidean over channels. Each penalty is quadratic `r^2`, Huber
  (`r^2` up to delta, then `2 delta r - delta^2`) or L1 smoothed below delta
  (`r`, and `r^2/(2 delta) + delta/2` below). L1 smoothness is total variation
  (edge-preserving); Huber or L1 data terms are robust to outliers. Row masses
  `m` follow the Laplacian choice: weighted degree (random walk; 1 for isolated
  rows), 1 (combinatorial) or lumped mesh area. With both penalties quadratic
  and no tolerance this is exactly one implicit step with `dt = 1/lambda`.
  The data weight is either fixed or chosen by the discrepancy principle: a
  log-space bisection within eight decades of `sum w / sum m` finds the weight
  whose mass-weighted RMS residual equals the noise level (to `1e-4` relative);
  a target outside that range clamps the weight and is reported. The tolerance
  is one radius for every row or a float/double radius property (radius 0 pins
  a row). The kernel is `Geometry::HarmonicField::FitProperty`: non-quadratic
  penalties use iteratively reweighted least squares (edge weight
  `w rho'(r)/(2r)`, the quadratic majorizer, so the energy never increases),
  and tolerances a primal-dual active set per channel whose active rows become
  hard rows at their bound. Every step is one sparse Cholesky
  `HarmonicField::Solve`. Reweighting stops when no value changes by more than
  the relative tolerance times the input range; exceeding the iteration limit,
  an unsettled active set or a failed factorization publishes nothing. Results
  report the weight, RMS residual, reweighting iterations and active bounds.

Spatial weights use the symmetric union of k-nearest neighborhoods: uniform,
Gaussian `exp(-distance^2/(2 spatial_sigma^2))`, or inverse distance. Inverse
distance uses `spatial_sigma*1e-12` as a positive distance floor for coincident
samples. The shared CPU `PointLBVH::Index` supplies deterministic neighbors,
excluding only the query ID; coincident distinct samples remain eligible.
Mutual neighbor pairs are merged once and edges are ordered by index pair.
Queries use the index's cubic Morton cells and near-first traversal described
in [spatial indices](../architecture/spatial-indices.md#construction-and-limits).
The index owns the compact live-position snapshot for one operation. This
geometry API accepts spans independently of ECS/runtime, so it uses a private
index rather than retaining an entity-cache lease. No GPU backend is selected.

Mesh vertex cotangent weights reuse `DEC::BuildLaplacian`, clamping negative
edge weights to zero; it is assembled only when cotangent weights are selected. This is a nonnegative graph operator, not signed FEM.
Implicit smoothing optionally pairs it with `DEC::BuildHodgeStar0` lumped areas.
Uniform mesh-edge weights retain one-ring topology independently of geometry;
the three kNN weights remain available on every domain. Topological weights,
lumped mesh areas and boundary pinning require mesh-vertex signals and positions.

`FilterProperty` owns all general diffusion filters. Mesh smoothing selects
`v:position` as both input and output; there are no separate mesh-only
uniform/cotangent/Taubin/implicit wrappers or mutating vertex-property wrappers.
Callers compact inactive rows and omit their incident edges before filtering.
Variational fitting is one filter entry rather than separate TV, robust or
bounded methods because all of them are this energy; IRLS and the active set are
its solvers, not user-facing methods. It lives in `Geometry.HarmonicField`
because it is built on the constrained solve, which already depends on
`Geometry.Smoothing`; runtime dispatches `VariationalFit` there.
The two-stage face-normal bilateral denoiser remains a distinct reconstruction
algorithm. Runtime reuses property resolution, deletion mapping, config codecs,
face-center construction and guarded editor history.

## Engine integration

| Surface | Contract |
| --- | --- |
| Least-structured input | A floating signal with 1–4 channels and a weighted undirected graph; spatial graph construction accepts vec3 samples. |
| Entity/domain sources | All eight canonical domains; explicit same-domain sample positions, or vertex/node positions for derived face centers and edge/halfedge midpoints. |
| Runtime owner | `Runtime.MeshFieldOperations.Smoothing.cpp`; uses canonical resolution, point/deletion capture, face-center construction, DEC and editor history. |
| Config/agent | `sandbox.property_smoothing`, registered in the sandbox config tree; serialization and preview/apply use the same validator as execution admission. Fit fields: `smoothness_penalty`, `data_penalty`, `fidelity`, `fit_weight`, `noise_level`, `penalty_delta`, `bound`, `bound_radius`, `bound_radii` (null unless bound; required on the input domain for per-row bounds), `max_fit_iterations`, `fit_tolerance`. |
| UI | View → Smooth Property, also reachable from Mesh/Graph/PointCloud → Processing. Input property, output name/storage, positions, filter, Laplacian, weights and parameters are configurable; **Variational fit** adds penalties, fixed weight or noise level, and the tolerance bound with its radius property. |
| Publication | Same domain and cardinality; only the named output changes. Existing deleted output slots remain bitwise untouched; new deleted output slots are zero. In-place writes, including positions, use guarded undo/redo. |
| Verification | `Test.PropertySmoothing.cpp` (including direct-vs-CG implicit parity), `Test.VariationalFit.cpp` (implicit-step equivalence, closed-form TV, outlier rejection, perturbation optimality for every penalty/bound pair, discrepancy target), `Test.PropertySmoothingOperations.cpp`, and the real ImGui actions in `Test.SandboxProcessingPanels.cpp`. The bound-radius property is a publication guard. |

The default output type follows the chosen input in the UI. Scalar output
storage can also be float or double. Conversion rejects nonfinite results or
float overflow before publication. Existing output storage must match the
configured kind. Structural topology and deletion properties cannot be outputs.

GPU follow-up owners are [GEOM-081](../../tasks/backlog/geometry/GEOM-081-vulkan-explicit-mesh-and-property-smoothing.md)
for explicit property filters and [GEOM-089](../../tasks/backlog/geometry/GEOM-089-vulkan-heat-methods-and-implicit-smoothing.md)
for heat/sparse execution; this integration offers only CPU execution.

The CPU operation runs synchronously when invoked. Neighborhoods are fixed
throughout a run, including when the output overwrites the positions. Lumped
areas are fixed too; this is fixed-operator diffusion, not geometry-recomputed
curvature flow. Reapply the operation to rebuild from the changed positions. Large
neighbor counts or many iterations can take substantial time. Limits are
1–1024 neighbors, 1–10000 iterations, positive sigmas, lambda in `(0,1]`, mu in
`[-1,0)`, heat time in `(0,1000]`, and `heat_time*rate <= 10000`. Preview checks
configuration and metadata; execution checks live values, derived topology,
solver convergence and representability before a single history transaction.

## Sources and selected variants

- [Desbrun et al., *Implicit Fairing of Irregular Meshes Using Diffusion and Curvature Flow*, 1999](https://doi.org/10.1145/311535.311576): backward-Euler diffusion; this implementation uses a fixed operator and nonnegative cotangent weights, not the complete curvature-flow algorithm.

- [Taubin, *A Signal Processing Approach to Fair Surface Design*, 1995](https://doi.org/10.1145/218380.218473): the two-pass polynomial filter, applied here to arbitrary property channels.
- [Tomasi and Manduchi, *Bilateral Filtering for Gray and Color Images*, 1998](https://doi.org/10.1109/ICCV.1998.710815): joint spatial/range weighting, adapted here to graph signals.
- [Hammond, Vandergheynst and Gribonval, *Wavelets on Graphs via Spectral Graph Theory*](https://arxiv.org/abs/0912.3848): spectral functions of a graph Laplacian and polynomial evaluation without diagonalization. This implementation selects the heat response and the positive power series above, not their wavelet bank or Chebyshev approximation.
- [Rudin, Osher and Fatemi, *Nonlinear Total Variation Based Noise Removal Algorithms*, 1992](https://doi.org/10.1016/0167-2789(92)90242-F): total-variation denoising; applied here on the sample graph with a delta-smoothed absolute value.
- Vogel and Oman, *Iterative Methods for Total Variation Denoising*, SIAM J. Sci. Comput. 1996, and Chan and Mulet, *On the Convergence of the Lagged Diffusivity Fixed Point Method in Total Variation Image Restoration*, SIAM J. Numer. Anal. 1999: the reweighting iteration used here and its linear convergence.
- Huber, *Robust Estimation of a Location Parameter*, 1964: the Huber penalty.
- Morozov, *On the Solution of Functional Equations by the Method of Regularization*, 1966: the discrepancy principle for choosing the data weight.
- Hintermüller, Ito and Kunisch, *The Primal-Dual Active Set Strategy as a Semismooth Newton Method*, SIAM J. Optim. 2002: the active-set iteration for the tolerance bounds.
- [Gadde, Narang and Ortega, *Bilateral Filter: Graph Spectral Interpretation and Extensions*](https://arxiv.org/abs/1303.2685): graph interpretation of bilateral weights. This implementation uses iterative bilateral filtering, not their complete family of spectral designs.

These are established formulations, not a claim of state-of-the-art superiority.
The bounded [cycle-signal benchmark](../../benchmarks/geometry/manifests/property_smoothing_smoke.yaml)
checks four linear filters against their analytic Fourier-mode gains and emits
runtime plus maximum error. It supplies a correctness workload, not a performance
comparison or GPU parity claim.

Variational-fit limitations: reweighting converges linearly and slows as delta
shrinks on plateaus; its reweighted edge and data weights grow like `1/delta`, so
very small deltas also lose Cholesky accuracy (the smoke benchmark's TV step
keeps `delta = 1e-9`). Each iteration refactors the system, and noise-level
fitting repeats the fit per bisection step. Tolerances are per channel boxes,
not Euclidean balls, for vector properties. The smoothness term is first order
(Dirichlet or TV), not biharmonic. A delta-free total-variation solver (ADMM or
primal-dual with one reused factorization) is tracked in
[GEOM-101](../../tasks/backlog/geometry/GEOM-101-delta-free-variational-fit-solver.md).
