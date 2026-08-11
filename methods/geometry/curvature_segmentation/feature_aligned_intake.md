# METHOD-038 Feature-Aligned Segmentation Intake

Status: **Slice A preregistration draft**. This document fixes the scientific
vocabulary, candidate equations, units, comparison rules, and rejection
conditions used to build the screening runner. It does not select or expose a
production `cpu_reference_v2`. The machine-readable protocol becomes frozen
only after its deterministic fixtures and runner are committed and sealed with
`tools/agents/experiment_custody.py freeze-protocol`.

## Contract boundary

METHOD-038 retains METHOD-037's signed-curvature Gaussian mixture as the region
data model. It may replace the input-triangulation Potts boundary inference,
but it must not replace the GMM, mutate topology, materialize cuts, use a GPU,
or change the Fixed/Automatic runtime default during Slice A.

Two different uses of curvature must remain separate:

- Extrinsic normal/curvature evidence may detect folds, ridges, valleys, and
  surface-type transitions. An intrinsic metric cannot distinguish a flat
  sheet from an isometric fold.
- Boundary fairness may use surface arc length and geodesic curvature `k_g`.
  It must not penalize normal curvature `k_n`, because that would change under
  an isometric bend even when the curve is intrinsically unchanged.

The target is remeshing stability after projection to a common embedded
reference surface. Raw face labels, region IDs, edge IDs, and edge-mask overlap
are not comparable across triangulations.

## Primary-source disposition

| Source | Equation or mechanism retained for screening | Disposition |
| --- | --- | --- |
| [Lavoué, Dupont, and Baskurt 2005](https://doi.org/10.1016/j.cad.2004.09.001) | Near-constant curvature regions, sharp-edge thresholding, normal-cycle curvature evidence, and curvature-direction boundary rectification | Lineage and CAD baseline; its classifier is not reproduced and does not replace the repository GMM. |
| [Lai, Zhou, Hu, and Martin 2006](https://doi.org/10.1145/1128888.1128891) | Feature-space embedding, feature-sensitive remeshing hierarchy, integral-invariant descriptors, coarse-to-fine clustering, and feature-sensitive boundary smoothing | Candidate B and the principal acceleration precedent. Its all-pairs geodesic distance construction is treated as a cost risk, not copied by default. |
| [Cohen-Steiner and Morvan 2003](https://doi.org/10.1145/777792.777839) | Normal-cycle anisotropic curvature measure with convergence under restricted-Delaunay sampling and regularity assumptions | Curvature-estimator baseline. Its theorem is not generalized to arbitrary input meshes. |
| [Pottmann et al. 2007](https://doi.org/10.1016/j.cagd.2007.07.004) | Ball/sphere integral invariants and principal curvatures at a physical kernel radius | Preferred smooth-feature descriptor family for A/B because radius is an explicit scale and the construction is noise-robust. |
| [Lai et al. 2007](https://doi.org/10.1109/TVCG.2007.19) | Multi-scale feature-sensitive metric, compactness and minimum-radius responses, morphology, hysteresis, and ridge/valley/prong/bridge/tunnel classification | Persistence/taxonomy baseline. Sharp CAD edges still require explicit dihedral handling. |
| [Hildebrandt, Polthier, and Wardetzky 2005](https://doi.org/10.2312/SGP/SGP05/085-090) | Smoothed higher-order extremality fields | Ridge/valley comparison only; rejected as the default unless it survives noise and remeshing screens. |
| [Bonneel et al. 2018](https://doi.org/10.1111/cgf.13549) | Surface Mumford-Shah/Ambrosio-Tortorelli edge-set formulation | Candidate C quality baseline; no latency presumption. |
| [Yan et al. 2012](https://doi.org/10.1016/j.cad.2012.04.005) | Variational quadric proxy segmentation | Candidate D on CAD fixtures only; semantic mismatch is expected on free-form curvature regimes. |
| [Golovinskiy and Funkhouser 2008](https://doi.org/10.1145/1409060.1409098) | Consensus boundary probability over randomized cuts | Candidate E offline stability oracle unless a deterministic bounded approximation is demonstrated. |
| [Hildebrandt, Polthier, and Wardetzky 2006](https://doi.org/10.1007/s10711-006-9109-5) | Sampling assumptions needed for convergence of polyhedral geometric quantities | Limits any convergence statement to declared regular, nondegenerate refinement families. |

## Screening equations and units

### Mandatory crease facts

For an interior edge with usable adjacent unit normals `n0,n1`, define

```text
theta_e = acos(clamp(dot(n0,n1), -1, 1)) * 180/pi,
h_e     = 1[theta_e > theta_0].
```

`h_e` is consumed from `Geometry.HalfedgeMesh.Features`, including its
fail-closed invalid-normal/topology behavior and configurable boundary policy.
The comparison is strictly greater: `theta_e == theta_0` is non-feature. The
initial screening threshold is `theta_0 = 45 degrees`; that is an experiment
parameter, not a new runtime default.

### Feature-sensitive metric and physical scale

Lai's feature embedding is

```text
Phi_w(x) = (x, w n(x)) in R^6,
M_w      = I + w^2 III,
lambda_i^2 = 1 + w^2 kappa_i^2.
```

`w` has length units so `w*kappa_i` is dimensionless. Under a uniform model
scale `x' = a x`, both `w` and every neighborhood radius `r` scale by `a`.
Every fixture records its reference bounding-box diagonal `D`; screening uses
`r/D` and `w/D` in reports while kernels receive world-unit values. The first
screening grid is preregistered as

```text
r/D in {0.005, 0.01, 0.02, 0.04}; w = r.
```

This grid may be rejected as a whole, but individual radii must not be selected
from confirmation results.

For a feature-sensitive geodesic circle with source-surface area `A`, length
`L`, and minimum source-space radius `d_min`, retain the dimensionless Lai
responses

```text
f_com = A / L^2,
rho_min = d_min / r = 1 / sqrt(1 + w^2 kappa_max^2).
```

Candidate A tests scale persistence of bounded response vectors derived from
`rho_min`, `f_com`, signed curvature change, and surface-type change. The exact
tight/loose thresholds are learned on the screening split only, then frozen as
numeric config before confirmation. A response that exists at only one radius
is not persistent. Explicit `h_e` creases bypass this smooth-feature threshold
and remain mandatory barriers.

Pottmann's ball-neighborhood covariance estimator is the preferred curvature
oracle for the screen. If `M_b,1` and `M_b,2` are its two tangent covariance
eigenvalues, the implementation must cite and reproduce their equation (27)
including the `O(r^7)` truncation rather than substituting the current
per-vertex estimator under the same name. Until that reproduction is tested,
the existing curvature field is only a comparison input.

### Feature taxonomy

Feature diagnostics distinguish:

- hard crease/boundary facts from `GEOM-071`;
- smooth ridge and valley evidence, whose sign requires a consistent surface
  orientation;
- surface-type transitions in signed `(k1,k2)` space;
- weak-gap hysteresis continuations;
- endpoints and junctions; and
- compact prong versus elongated ridge/valley/bridge/tunnel responses.

The taxonomy is diagnostic. It must not force every optional feature response
to split a statistical region.

### Boundary objective

For a continuous boundary network `Gamma` on the reference surface, the
candidate family is compared using

```text
E(l,Gamma) = sum_f D_f(l_f)
           + lambda_L * length(Gamma)
           + lambda_g * integral_Gamma k_g(s)^2 ds
           - lambda_F * integral_Gamma F(s) ds,
```

subject to mandatory high-dihedral barriers and topological closure. `D_f` is
the existing GMM posterior unary. `F` is an inspectable feature likelihood.
There is no `k_n` term. Candidate A may restrict `Gamma` to a deterministic
feature-cell/narrow-band graph for the killing experiment; any source-edge mask
is a visualization projection, not the continuous comparison object.

## Paired-remeshing evidence design

Every analytic surface owns a continuous reference parameterization or exact
closest-point/barycentric map. A pair is generated from the same reference by
at least two of: uniform refinement, alternate diagonals, seeded legal edge
flips, anisotropic tessellation, nonuniform sampling, and feature-aligned versus
feature-crossing triangles. Invalid, inverted, degenerate, or non-manifold
samples fail fixture construction rather than entering a metric.

The screening split contains plane, sphere, cylinder, saddle, `30/45/60`
degree plane-plane folds, a smooth blend, tangent plane-cylinder transition,
one ridge/valley pair, one junction, one open boundary, and disconnected
components. The `45` degree fold is the strict-threshold equality control. The
confirmation split must use different analytic parameters and seeds and remains
unread until the formulation, numeric thresholds, and implementation digest are
frozen.

Projected comparisons use:

- area-weighted, label-permutation-invariant Rand agreement and variation of
  information for regions;
- symmetric surface-geodesic Hausdorff distance, tolerance-band
  precision/recall, total length, spur count, and junction error for boundaries;
- `integral k_g^2 ds` for fairness, reported separately from feature recovery;
- deterministic payload parity and stage timings; and
- peak working-set estimate measured by the same runner for every comparator.

Distance and length are reported both in world units and divided by `D`.
Raw region IDs and unprojected edge-mask overlap are forbidden metrics.

### Frozen cheapest-screen split (checkpoint 3)

The first executable A-D killing split is narrower than the eventual corpus
and is frozen before any candidate implementation. Coordinates use the
reference bounding-box diagonal `D` for normalized distances. Each pair keeps
the same parametric vertices and connectivity while changing only the listed
diagonal phase; fold pairs rigidly rotate the positive-`x` half about the
`x=0` crease and therefore preserve every intrinsic edge length.

| Control | Screening parameters (readable now) | Confirmation parameters (held out) | Killing fact |
| --- | --- | --- | --- |
| Plane | `[-1,1] x [0,1]`, `24 x 48` cells, diagonal phases `0/1`, seed `1701` | `[-1.3,0.9] x [0,1.2]`, `31 x 62`, phases `0/1`, seed `9109` | no interior feature or segmentation boundary |
| Cylinder | open cylinder `R=1`, `L=2`, `32 x 64`, angular phase `0` versus `1/128` turn, seed `1709` | `R=1.7`, `L=2.6`, `37 x 74`, phase `1/148`, seed `9127` | no tessellation-induced ring boundary |
| Plane-plane folds | the plane grid rigidly folded by `30/45/60` degrees, `theta_0=45` degrees, seed `1723` | folds `20/45/70` degrees on the held-out plane grid, seed `9133` | `theta > theta_0` is mandatory, equality is not; projected crease is `x=0` |
| Smooth transition | `q(x)=0.5(1+tanh(x/0.08))` on `[-1,1] x [0,1]`, reference curve `x=0`, seed `1741` | width `0.11` on `[-1.3,0.9] x [0,1.2]`, seed `9151` | recover the continuous transition without treating it as a hard dihedral |
| Isometric-bend control | flat sheet versus the `60`-degree fold with identical parametric connectivity, reference boundary along the crease, seed `1753` | flat sheet versus the `70`-degree held-out fold, seed `9173` | intrinsic-only evidence and `k_g` fairness are unchanged; extrinsic crease evidence may change |

Screening may read only the left column. Confirmation parameters stay unused
until one candidate implementation, its numeric thresholds, and its source
digest are frozen. The later full corpus still owes sphere, cone/frustum,
saddle, tangent plane-cylinder, cylinder-sphere, ridge/valley, nearby-feature,
junction, open-boundary, and disconnected-component families; this table does
not silently declare those rows complete.

Candidate A runs every control directly. Candidate B must run the same controls
after coarse-to-fine transfer and is killed by any lost mandatory fold or
coarse-level plane/cylinder artifact. Candidate C remains a quality comparator
but is killed as a production option if the bounded cost screen cannot support
the later acceleration gate. Candidate D must pass plane, cylinder, and fold
controls and is killed as the general default if its quadric proxies cannot
meet the smooth-transition curve gate. No candidate is selected merely because
the fixture/oracle lane itself passes.

## Candidate killing order

1. **Contract controls.** Reject a candidate that crosses a valid mandatory
   `theta > theta_0` crease, marks equality as a crease, changes topology,
   replaces the GMM, introduces `k_n` fairness, or changes results under a
   global orientation reversal beyond label permutation and ridge/valley sign.
2. **Cheapest analytic screen.** Run A-D on plane/diagonal, cylinder,
   `30/45/60` folds, smooth transition, and isometric-bend controls. Reject any
   candidate that creates tessellation boundaries on the plane/cylinder or
   cannot represent the known continuous fold/transition curve.
3. **Remeshing screen.** Require non-increasing projected boundary error under
   the declared regular refinements and stable bounded error under seeded flips
   and anisotropy. No convergence claim is made outside those families.
4. **Cost screen.** Compare matched single-thread reference work and report all
   stages. C, D, and E remain baselines unless their observed cost is compatible
   with the task's later `0.50x` median acceleration target; paper complexity
   alone cannot pass this gate.
5. **Selection.** A is the smallest first implementation. B is attempted only
   if A fails stability or the 100k/1M cost projection. A negative screen keeps
   METHOD-037 available and leaves GEOM-076 blocked.

## Profiling contract

`CurvatureSegmentationDiagnostics::Timings` reports wall-clock milliseconds for
curvature estimation, face aggregation/normalization, all GMM candidate fits,
unary construction, dual-graph construction, spatial optimization,
connectivity/cleanup/publication, and total time. Each candidate also reports
its `FitMilliseconds` beside its deterministic EM iteration count. Individual
EM-iteration callbacks are deliberately not added to `Geometry.GaussianMixture`:
candidate duration divided by the recorded iteration count is sufficient for
the first bottleneck screen and avoids creating a new fitter seam without a
second consumer.

Timings are observational diagnostics, not deterministic result fields. Quality
and parity tests compare labels, boundaries, energies, counts, statuses, and
configured seeds; they only require timing values to be finite and
nonnegative.

The legacy smoke's `quality_error_l2` schema slot is explicitly measured in
`misclassified-face-fraction`; it is not a Euclidean L2 norm. New METHOD-038
protocols use semantically named raw columns and bind any compatibility mapping
to that legacy slot.

The opt-in `IntrinsicCurvatureSegmentationProfile` runner declares paired
uniform/nonuniform supplied-curvature cohorts at 10k, 100k, and 1M live faces
for both Fixed and Automatic selection. Its `fixtures` cohort adds a 10k
analytic unit-sphere diagonal pair in cold `ComputeAndSegment` and reusable
precomputed-curvature lanes, plus a planar transition-grid pair measured
against the exact continuous line `x=0.5`. The planar symmetric surface-
Hausdorff result is an upper bound: the predicted-to-reference direction is
exact on each straight segment, and the sampled reference-to-predicted
direction adds half the 4097-sample spacing using the distance function's
1-Lipschitz property. All limits remain harness-health or bounded fixture
contracts. One planar pair is not the preregistered analytic corpus, and it
does not establish refinement convergence, candidate A-D quality, or an
acceleration gate.
