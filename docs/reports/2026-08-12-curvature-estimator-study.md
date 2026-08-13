# Curvature estimator parity and method study — 2026-08-12

> Historical PMP study: this report characterizes the previously selected PMP
> formulation and remains useful for estimator comparison and robustness
> limitations. BUG-156 was subsequently reopened to satisfy the reporter's
> actual interoperability target: Framework24 `CurvatureTaubin`, whose area,
> support, boundary, sign/pairing, and smoothing conventions differ from PMP.
> The current local, non-claim-eligible direct-parity record is
> [`ara/evidence/tables/curvature_framework24_parity_2026-08-12.md`](../../ara/evidence/tables/curvature_framework24_parity_2026-08-12.md).

## Historical verdict

The repaired Intrinsic principal-curvature implementation reproduces the
selected PMP tensor formulation on ordinary, well-conditioned triangle meshes.
Across a deterministic 96-model sample from the local OBJ corpus, the 62 models
without a rejected triangle had worst relative L2 errors of `4.55e-7` for
minimum curvature and `2.23e-7` for maximum curvature. The median full-field
errors over all 80 comparable models were `4.15e-8` and `3.31e-8`.

The remaining large full-field differences are intentional and preferable to
literal PMP parity: they occur around exactly degenerate or severely
ill-conditioned triangles, where the reference path can reuse undefined local
Laplace entries or normalize a zero face normal. Intrinsic now rejects that
support, excludes it from the reusable cotan smoother, and publishes finite
zero sentinels with explicit diagnostics. More than four adjacency rings away
from rejected triangles, the worst relative L2 differences over 2,520,863
vertices were `3.48e-6` and `3.25e-6`.

PMP's estimator is therefore a useful interoperability baseline, but it is not
the universally best quality/performance choice for every mesh. The literature
explicitly finds that no tested estimator is reliable in all circumstances.
Keep the current PMP-compatible path as the fast clean-mesh baseline; evaluate
Rusinkiewicz's finite-difference estimator as the next low-cost alternative,
and use interpolated corrected curvature measures when robustness to noise,
coarse tessellation, or a controllable physical support radius matters.

## Experiment identity

- Dataset root: `/home/alex/Dropbox/Work/Datasets/obj`.
- Discovered OBJ files: 1,989 after ignoring AppleDouble `._*` files.
- Selection: 10 named controls plus a deterministic SHA-256-ordered,
  size-stratified sample, capped at 96 files and 50 MB per source file.
- Comparison input: each source was normalized independently to the same
  position-only, fan-triangulated OBJ before either implementation ran.
- Intrinsic probe: Clang 23, Release, SHA-256
  `4da760e6b467d505ff76c44b9b1b978747c19bcf1ab0d7d5b1480bb8bc5bdfd7`.
- PMP probe: Clang 23, Release, local PMP revision
  `2121e154321949a5d4c3654ffa110634fa33913c`, probe SHA-256
  `1f08b12c2f060039d3dc2a2d42eb63eac3c5cf77377fe25a68f42a2876152cde`.
- Full corpus result SHA-256:
  `d5044acd9a6cbd97c6a9ad2a61e57a2ec3562f577c0963af54af433a3e222872`.
- Evidence class: local-development diagnostic. The source tree was dirty, the
  dataset is machine-local, and timing was not controlled sufficiently for a
  claim-eligible benchmark.

The retained harness and exact reproduction commands are in
[`tools/diagnostics/curvature/`](../../tools/diagnostics/curvature/README.md).
It records full fields, not only aggregate numbers, and never modifies the
source OBJ files.

## Cross-model results

| Outcome | Models | Interpretation |
| --- | ---: | --- |
| Compared | 80 | Same realized topology and finite scalar outputs from both probes |
| Normalization failed | 13 | Files contained no usable OBJ position/face payload for this triangle-mesh experiment |
| PMP timeout | 2 | PMP exceeded 120 seconds; Intrinsic finished both normalized inputs in 4.16 and 1.68 seconds wall time |
| Invalid result | 1 | PMP emitted non-finite scalar curvature on `OC12_7.obj`; Intrinsic remained finite |

The 80 comparable models contain 2,530,726 vertices. Their aggregate result is:

| Population / aggregation | Models / vertices | Relative L2 `kmin` | Relative L2 `kmax` |
| --- | ---: | ---: | ---: |
| All fields, median model | 80 / 2,530,726 | `4.15e-8` | `3.31e-8` |
| No rejected triangles | 62 models | `4.55e-7` | `2.23e-7` |
| More than four rings from rejected triangles | 2,520,863 vertices | `3.48e-6` | `3.25e-6` |
| All fields, worst model | 80 models | `9.62e-1` | `9.40e-1` |

The corpus contained 129 exactly degenerate and 376 ill-conditioned triangles
under the dimensionless quality rule
`twice_area / maximum_edge_length_squared <= 3.5e-4`. The floor is the
conservatively rounded square root of machine epsilon for the engine's public
float-position storage; it bounds the roundoff amplification of inverse-area
terms without depending on model units. The worst full-field
model, `EA01m.obj`, contained 206 such triangles: full errors were `0.962` and
`0.940`, but fell to `5.26e-8` and `4.12e-8` on its quality-uncontaminated
vertices. The same pattern holds for the other outliers. This localizes the
difference to rejected support and its three explicit smoothing rings rather
than to topology, the eigensolver, or a global linear solve.

Intrinsic's published directions cannot be compared directly because this PMP
API publishes only scalar curvature selections. On 2,379,268 Intrinsic vertices
that published directions, however, the worst model-level RMS errors were `1.71e-8`
from unit length, `2.04e-8` from mutual orthogonality, and `1.69e-8` from the
realized geometric tangent plane.

## Scale differential

The original implementation used absolute epsilon gates for areas, cotangent
denominators, and tensor support. That made geometry at scale `1e-6` collapse
to zero and left material errors at scale `1e-3`. The repair uses homogeneous
tests and the dimensionless triangle-quality criterion above. Re-running the
same 29,070 vertices from bunny, armadillo, and fandisk produced:

| Uniform position scale | Worst relative L2 `kmin` | Worst relative L2 `kmax` |
| ---: | ---: | ---: |
| `1e-6` | `4.09e-8` | `3.22e-8` |
| `1e-3` | `4.29e-8` | `3.32e-8` |
| `1e3` | `4.25e-8` | `3.27e-8` |
| `1e6` | `2.57e-6` | `1.22e-6` |

The small loss at `1e6` is consistent with the public float position storage;
the estimator itself accumulates in double and retains finite output.

## Root cause and implemented correction

There is no global curvature solver in the compared PMP path. It constructs a
two-ring edge-dihedral tensor, performs a local symmetric 3x3 eigendecomposition,
interpolates boundary values, and applies three explicit sparse-matrix updates
of `curv += L * curv`. The earlier Intrinsic port instead used a tangent 2x2
projection, computed boundary values directly, and replaced each value with a
full cotan neighbour average. Those are algorithm-port differences, not solver
differences.

The repair now:

- matches the PMP signed 3x3 eigensystem, boundary interpolation, and three
  damped `0.5 old + 0.5 neighbour_average` updates;
- preserves authored OBJ normal seams on `h:normal` instead of splitting
  authoritative geometric topology;
- exposes that damped cotan operation for `float`, `double`, and canonical
  `glm::vec2`, `glm::vec3`, and `glm::vec4` vertex properties;
- accepts an optional active-vertex mask so any scalar/vector caller can
  exclude invalid support without allowing it to diffuse into valid rows;
- uses scale-independent geometric predicates; and
- rejects degenerate/ill-conditioned faces and reports their counts and minimum
  observed quality rather than reproducing PMP's undefined/non-finite behavior.

PMP's degenerate-input weakness is concrete in the referenced revision:
triangle normals normalize a zero cross product without a guard, while the
triangle Laplace helper writes its output only when Heron's area is positive.
On a zero/NaN area, a caller can therefore retain stale matrix entries. Literal
numeric parity on those inputs is not a meaningful correctness target.

The pinned reference anchors are
`src/pmp/algorithms/curvature.cpp:135-356` (local tensor, boundary interpolation,
and explicit smoothing), `src/pmp/algorithms/normals.cpp:21-24` (unguarded
triangle normalization), and `src/pmp/algorithms/laplace.cpp:156-190`
(conditionally written triangle matrix). PMP's remeshing source labels the
tensor option Cohen-Steiner; no solve occurs in the curvature tensor path.

## Literature comparison

| Estimator family | Values and directions | Cost/strength | Important limitation | Recommended role |
| --- | --- | --- | --- | --- |
| Current PMP-compatible edge-dihedral tensor / normal cycle | Principal values; Intrinsic also retains eigenvectors | Local, linear-storage, fast on clean connected meshes; normal-cycle theory gives an efficient tensor and convergence bounds for restricted Delaunay samples | Sensitive to degenerate faces, irregular sampling, noise, and the fixed two-ring/three-smoothing scale; local PMP API does not publish directions | Keep as the clean-mesh interoperability baseline |
| Meyer et al. mixed FEM/FV operators | Robust discrete mean and Gaussian scalars; no coherent direction field by themselves | Simple, widely used cotan/angle-defect operators with accuracy guarantees under mild smoothness | Recovering `kmin/kmax` from `H` and `K` amplifies inconsistent estimates and does not recover directions | Keep as independent scalar invariants and analytic cross-checks |
| Taubin 1995 tensor | Principal values and directions | Linear time/space, closed-form local 3x3 tensor | Its vertex-neighbour integral formulation is not the same algorithm as PMP's hinge quadrature; local and scale-limited | Candidate baseline, but no reason to replace the now-matched path without benchmark evidence |
| Rusinkiewicz 2004 finite differences | Principal values, directions, and curvature derivatives | Efficient per-face estimate plus vertex averaging; reported comparable accuracy with significantly fewer outliers on irregular meshes | Still a local differential estimate and degrades under substantial noise; needs its own degeneracy policy | Best next low-cost CPU reference candidate for interactive irregular meshes |
| Cazals/Pouget jet fitting | Values, directions, and higher derivatives | Works on point samples or meshes; polynomial degree/neighbourhood make the accuracy model explicit and SVD exposes conditioning | More expensive, neighbourhood/degree dependent, and can be ill-conditioned | Use for point clouds or high-order differential properties |
| Integral invariants | Principal values and directions | Ball-neighbourhood PCA is numerically robust and naturally multiscale | Radius selection is semantic, and neighbourhood integration costs more | Use where stable physical-scale features matter |
| Lachaud et al. interpolated corrected measures | Mean, Gaussian, principal values and directions | Stable with respect to position/normal perturbations; accepts an expansion-ball radius and corrected normals; available in CGAL | More computation, relies on trustworthy or separately estimated normals, and adds a radius control surface | Preferred robust/noisy/coarse-mesh backend candidate |

The key primary sources are:

- [Cohen-Steiner and Morvan, *Restricted Delaunay Triangulations and Normal
  Cycle*](https://courses.cms.caltech.edu/cs286c/papers/restricted/cohen_normalcycle.pdf),
  which derives an efficient polyhedral curvature tensor and bounds its error
  for restricted Delaunay approximations.
- [Meyer et al., *Discrete Differential-Geometry Operators for Triangulated
  2-Manifolds*](https://www.geometry.caltech.edu/pubs/DMSB_III.pdf), the mixed
  FEM/FV scalar operator reference.
- [Taubin, *Estimating the Tensor of Curvature of a Surface from a Polyhedral
  Approximation*](https://research.ibm.com/publications/estimating-the-tensor-of-curvature-of-a-surface-from-a-polyhedral-approximation),
  which describes the linear-time/space integral tensor estimator.
- [Rusinkiewicz, *Estimating Curvatures and Their Derivatives on Triangle
  Meshes*](https://gfx.cs.princeton.edu/pubs/Rusinkiewicz_2004_ECA/index.php),
  which reports an efficient finite-difference method with fewer outliers and
  comparable accuracy to prior methods.
- [Váša et al., *Mesh Statistics for Robust Curvature
  Estimation*](https://onlinelibrary.wiley.com/doi/10.1111/cgf.12982), whose
  large comparative experiment finds no estimator reliable in every tested
  circumstance and motivates estimator selection from mesh statistics.
- [Pottmann et al., *Principal Curvatures from the Integral Invariant
  Viewpoint*](https://www.geometrie.tuwien.ac.at/geom/ig/publications/oldpub/2007/pwylh_principal_07/pwylh_principal_07.html),
  which uses ball-neighbourhood PCA for robust multiscale curvature.
- [Lachaud et al., *Interpolated Corrected Curvature Measures for Polygonal
  Surfaces*](https://perso.liris.cnrs.fr/david.coeurjolly/publication/lachaud-2020/),
  and the corresponding [CGAL corrected-curvature
  API](https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__corrected__curvatures__grp.html).
- [CGAL's Cazals/Pouget jet-fitting reference](https://doc.cgal.org/latest/Jet_fitting_3/group__PkgJetFitting3Ref.html),
  which exposes principal values/directions and higher derivatives for point
  samples through polynomial fitting and SVD.

## Performance interpretation

A matched Clang-23 `-O3` probe with five in-process timed repetitions produced:

| Model | Vertices | Intrinsic | PMP | Intrinsic / PMP |
| --- | ---: | ---: | ---: | ---: |
| `inputmodels/armadillo.obj` | 21,582 | 55.0 ms | 82.8 ms | `0.665` |
| `inputmodels/brain100k.obj` | 49,888 | 135.7 ms | 564.7 ms | `0.240` |
| `SHREC14/Synthetic/Data/203.obj` | 60,169 | 140.1 ms | 804.4 ms | `0.174` |
| `armadillo.obj` | 172,974 | 437.3 ms | 6,435.4 ms | `0.068` |

The median ratio was `0.207`. This establishes only that the repaired Intrinsic
implementation is not paying an obvious performance penalty relative to this
local PMP build. It is not a general performance claim: the corpus is small,
cache state and CPU frequency were uncontrolled, no confidence intervals were
collected, and no competing estimator was implemented in the same engine.

## Decision and follow-up evidence needed

Do not replace the default solely from literature reputation. A backend change
should first compare at least the current path, Rusinkiewicz, and corrected
curvature measures on analytic shapes with known truth, remeshing-density and
triangle-quality sweeps, controlled normal/position noise, boundaries, sharp
features, scale transforms, direction angular error away from umbilics, and
matched CPU time/memory. Until that evidence exists, the current repaired path
is the smallest deterministic implementation that preserves PMP compatibility
and fails safely outside its trustworthy geometric regime.
