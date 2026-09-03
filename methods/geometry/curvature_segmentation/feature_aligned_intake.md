# METHOD-038 Feature-Aligned Segmentation Intake

Status: **completed METHOD-038 evidence intake**. This document records the
scientific vocabulary, candidate equations, units, comparison rules, rejection
conditions, and analytic controls used by the screening runner. METHOD-038
retired without selecting or exposing a production `cpu_reference_v2`.
METHOD-039 owns the smaller practical feature-network patch formulation; its
implementation must consume rather than reinterpret the sealed controls here.

## Contract boundary

METHOD-038 retained METHOD-037's signed-curvature Gaussian mixture as the
comparison data model. It did not replace the input-triangulation Potts
boundary inference, replace the GMM, mutate topology, materialize cuts, use a
GPU, or change the Fixed/Automatic runtime default.

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

## METHOD-039 selected practical contract (Slice A freeze)

This section freezes the formulation that METHOD-039 will implement. It is an
engineering synthesis of the cited integral-invariant, ridge/valley,
watershed/region-growing, and region-adjacency merging literature; it is not a
novelty claim. At this checkpoint it is an executable contract and oracle
catalog only. The production method remains `cpu_reference_v1`, and no runtime
selector, config migration, or UI behavior changes in Slice A.

### Immutable inherited evidence boundary

METHOD-039 consumes, but must not rewrite or reinterpret, these completed
METHOD-038 records:

- [`tasks/done/METHOD-038-feature-aligned-remeshing-stable-curvature-segmentation.md`](../../../tasks/done/METHOD-038-feature-aligned-remeshing-stable-curvature-segmentation.md)
  is the retired task contract;
- [`tasks/evidence/METHOD-038/experiment/protocol.yaml`](../../../tasks/evidence/METHOD-038/experiment/protocol.yaml)
  fixes the final experiment protocol;
- [`tasks/evidence/METHOD-038/experiment/runs/scratch-011/bundle.yaml`](../../../tasks/evidence/METHOD-038/experiment/runs/scratch-011/bundle.yaml)
  and [`audit.json`](../../../tasks/evidence/METHOD-038/experiment/runs/scratch-011/audit.json)
  are the accepted portable non-claim replay and independent audit; and
- ARA claims C40--C43 in
  [`ara/logic/claims.md`](../../../ara/logic/claims.md) bound the narrow
  Automatic-selection refutations and fixture/oracle support.

Those records validate only the named v1 and generated-control facts. They do
not validate the detector, patch construction, stability, runtime adoption, or
performance of the METHOD-039 formulation below.

### Reviewed integral and extremality equations

Pottmann et al.'s equation (27) is a reviewed comparator, not the selected
open-surface estimator. For the covariance of the *volume*
`N_b = B_r(p) intersect D`, its tangent eigenvalues have the asymptotic form

```text
M_b,1 = 2*pi*r^5/15 - pi*(3*kappa_1 + kappa_2)*r^6/48 + O(r^7),
M_b,2 = 2*pi*r^5/15 - pi*(kappa_1 + 3*kappa_2)*r^6/48 + O(r^7),

kappa_b,1 = 6*(M_b,2 - 3*M_b,1)/(pi*r^6) + 8/(5*r),
kappa_b,2 = 6*(M_b,1 - 3*M_b,2)/(pi*r^6) + 8/(5*r).
```

This construction presumes a solid domain and ball--solid intersection. A
centroid quadrature over an open triangle patch is not equation (27), so
METHOD-039 does not use that name for its practical compact surface-patch
averages. It consumes the existing or supplied signed principal-curvature
field and makes the physical averaging radius explicit.

The Hildebrandt--Polthier--Wardetzky comparator retains their directional
extremality quantity

```text
e_i(p) = 1/area(star(p))
         * sum_{T incident p} area(T) * <grad(kappa_i)|_T, t_i(p)>.
```

A maximum-curvature ridge satisfies `e_max = 0`, a negative directional
derivative of `e_max` along `t_max`, and
`abs(kappa_max) > abs(kappa_min)`; the valley conditions reverse the
directional sign for `e_min`. This remains a fixture comparator because it
requires a stable principal-direction field and higher derivatives. It is not
a hidden second production detector.

### Units, neighborhoods, and selected soft-feature response

Let `D > 0` be the source bounding-box diagonal, `A_f` a face area, `c_f` its
centroid, and `m_e` an edge midpoint. The ordered, signed face curvatures are
the arithmetic means of their three slot-aligned vertex values. Define

```text
a_f       = A_f / D^2,
ell_e     = length(e) / D,
x_f(r)    = r * (kappa_1,f, kappa_2,f),
r_0 / D   = 0.02,
r_s / D   in {0.01, 0.02, 0.04}.
```

All following energies are dimensionless. For adjacent faces `f,g` across
edge `q`, the dual step length is

```text
delta_q = norm(c_f - m_q) + norm(c_g - m_q).
```

For a candidate interior edge `e=(f_0,f_1)`, remove `e` and all hard-feature
transitions from the dual graph and run bounded Dijkstra searches from `f_0`
and `f_1`. A face is assigned to the nearer side (equal distances break toward
the lower source-face slot) when that distance is less than `r_s`. With the
compact kernel

```text
psi(t)       = (1 - t^2)^2  for 0 <= t < 1, and 0 otherwise,
w_jg(r_s)    = A_g * psi(distance_j(g) / r_s),
mu_e,j(r_s)  = sum_g w_jg(r_s) * x_g(r_s) / sum_g w_jg(r_s),
z_e(r_s)     = r_s/2 * ((kappa_1,va,kappa_2,va)
                        + (kappa_1,vb,kappa_2,vb)),
u(y)          = y / norm(y) when norm(y) > 0, and (0,0) otherwise,
s_tau(y)     = 1 - exp(-max(y,0) / tau_response),
tau_response = 0.10,
```

the three inspectable responses are

```text
T_e,s = min(1, norm(u(mu_e,0) - u(mu_e,1))),
R_e,s = s_tau(max(0, min(z_e,1 - mu_e,0,1,
                           z_e,1 - mu_e,1,1))),
V_e,s = s_tau(max(0, min(mu_e,0,2 - z_e,2,
                           mu_e,1,2 - z_e,2))),
Q_e,s = max(T_e,s, R_e,s, V_e,s),
Q_e   = median_s Q_e,s.
```

`T` detects a change in signed-curvature *type* rather than treating every
large same-type curvature gradient as a feature line. The zero vector remains
the explicit flat sentinel. `R` and `V` test whether the edge value is
respectively above both one-sided `kappa_1` means or below both one-sided
`kappa_2` means. Taking the median is the exact two-of-three scale-persistence
rule. The per-scale values and winning signal are diagnostics. The normalized
direction difference and Euclidean norm are invariant under the orthogonal
map `(kappa_1,kappa_2) -> (-kappa_2,-kappa_1)` induced by global orientation
reversal; `R` and `V` swap names while `T`, `max(R,V)`, and final support remain
unchanged.

Slice B corrected the originally frozen magnitude-only `T` during executable
contract testing, before any integrated quality run or positive claim. The
magnitude-only form produced parallel curvature-gradient bands and missed the
zero-crossing line on the preregistered smooth transition. The direction form
above is the implemented contract; the rejected form is not retained as a
second backend or tunable option.

Soft-feature candidates are live, non-hard interior edges with two live
incident triangles; source-boundary and hard edges do not receive `Q_e`. For
non-maximum suppression, let `t_e` be the unoriented candidate-edge tangent.
In each incident triangle, form the in-face binormal
`b = normalize(n cross t_e)` and choose, from the other two edges that are also
soft-feature candidates, the edge whose midpoint has the largest absolute
projection `abs(dot(m_q-m_e,b))`. A side with no eligible alternative adds no
competitor. Add candidates sharing either endpoint whose midpoint offset is
more binormal than longitudinal with respect to the average incident-face
normal. These candidates form `N_perp(e)`. Retain `e` exactly when
`(Q_e,-slot(e))` is lexicographically no smaller than the same pair for every
member of `N_perp(e)`.

Two bounded topology rules keep the thinning stable on triangle diagonals.
A strong transition junction is a vertex incident to at least three strong
transition candidates with at least one tangent pair turning by more than
`60 degrees`. A strong branch incident to that vertex bypasses suppression
only when it has a continuation within `15 degrees` at its other endpoint.
After suppression, if all three edges of one triangle remain, reject the edge
with the lexicographically smallest `(Q_e, aligned-continuation-count,
-slot(e))`. This removes a one-cell triangular plateau without erasing the
continuing branches of an actual junction. All choices are slot-stable.

Hysteresis uses the frozen thresholds

```text
tau_low = 0.35, tau_high = 0.65, alpha_max = 60 degrees.
```

Every NMS survivor at or above `tau_high` is a strong edge. A weak survivor at
or above `tau_low` is retained only when reachable from a strong edge through
survivors sharing vertices whose consecutive unoriented tangents turn by at
most `alpha_max`. Strong branches seed themselves, so junctions are not erased
by the continuation test. A retained connected fragment shorter than `r_0` is
rejected unless it touches a source boundary, a hard edge, or a retained
degree-three-or-higher junction. The final supplied/computed soft confidence is

```text
F_e = Q_e for retained soft edges, and 0 otherwise.
```

Hard evidence remains the separate binary `H_e` from the strict shared
dihedral classifier. A boundary-policy hard mark on a source-boundary edge is
a feature diagnostic and potential line endpoint, not a two-face partition
transition. `F_e` is not length weighted by itself; its contribution to an
objective is length weighted. Diagnostics record `T/R/V`, the three scale
decisions, NMS winner/loser, hysteresis predecessor, rejected-fragment reason,
endpoint degree, and junction degree.

### Supplied-evidence preflight

The oracle lane supplies only two borrowed, slot-aligned spans: binary
`H_e` and finite `F_e in [0,1]`. It deliberately adds no feature-network
interface or owning adapter. Before any detector or patch work, the common
preflight rejects empty input, submesh views, non-triangle or degenerate faces,
invalid adjacency, non-finite positions or curvatures, unordered
`kappa_1 < kappa_2` inputs, edge/vertex slot-count mismatches, non-binary hard
values, and non-finite or out-of-range soft confidence. Failure returns an
explicit status and no partial result.

### Seed and simultaneous-growth equations

For a traversable face-dual transition across `e`, define

```text
g(f,g) = (delta_e / D)
         * (1 + alpha_kappa * norm(x_f(r_0) - x_g(r_0))
              + alpha_feature * F_e),
alpha_kappa = 1, alpha_feature = 4.
```

`g(f,g)=infinity` across an interior `H_e=1` transition. Both incident faces of
each interior hard edge are mandatory distinct seeds. Each hard-barrier-
connected component with no such seed starts from its lowest live face slot.
Deterministic farthest-point sampling adds the face with greatest distance to
the current seed set until that distance is at most

```text
rho_seed = 2*r_0/D = 0.04.
```

Distance ties use face slot. One multi-source Dijkstra pass then assigns every
face to the lexicographically smallest `(accumulated_cost, seed_slot,
face_slot)`. A hard-adjacent seed pair can never be coalesced during growth.
The resulting fronts are provisional and have no boundary authority.

### Frozen regional and boundary energy

The existing deterministic, unweighted GMM remains the global hypothesis
generator; Slice A does not claim sample-weight support in `FitEM`. Let
`p_fk` be its finite posterior responsibility for base-scale descriptor `x_f`,
after the existing robust descriptor normalization, and set

```text
u_fk      = -log(max(p_fk, epsilon_p)), epsilon_p = 1e-12,
C(R)      = min_k sum_{f in R} a_f * u_fk + beta_patch,
beta_patch = 0.0025.
```

Thus fitting is unchanged while the regional decision is area weighted. The
winning component, runner-up cost margin, area, descriptor mean/covariance,
and normalized residual are diagnostics; a component ID is not a patch ID.

For a boundary graph `Gamma`, let

```text
L(Gamma) = sum_{e in Gamma} ell_e,
S(Gamma) = sum_{e in Gamma} ell_e * F_e.
```

At a degree-two boundary vertex separating two regions, let `omega_0` and
`omega_1` be the sums of source-triangle corner angles in the two incident
surface sectors between the outgoing boundary edges. If their lengths are
`ell_1,ell_2`, define the embedding-independent discrete geodesic turn and
quadrature

```text
phi_v = min(abs(pi - omega_0), abs(pi - omega_1)),
g_v   = phi_v^2 / max((ell_1 + ell_2)/2, epsilon_length),
epsilon_length = 1e-12,
G(Gamma) = sum over degree-two boundary vertices g_v.
```

Endpoints and junctions are reported but contribute no ambiguous pairwise
turning term. There is no normal-curvature term. The finite boundary energy is

```text
B(Gamma) = lambda_length * L(Gamma)
           + lambda_turn * G(Gamma)
           - lambda_feature * S(Gamma),
lambda_length = 0.01,
lambda_turn = 0.001,
lambda_feature = 0.02.
```

For adjacent regions `R_i,R_j`, let `P'` be the partition after their union
and define the exact boundary credit

```text
B_ij(P) = B(Gamma(P)) - B(Gamma(P')),
Delta_merge = C(R_i union R_j) - C(R_i) - C(R_j) - B_ij(P).
```

This definition includes any turning-cost change at affected endpoints and
junctions; it is not merely the length of the deleted shared chain. A merge is
inadmissible if the union would internalize any `H_e=1` edge. Otherwise choose
the most negative finite `Delta_merge`; exact ties use the ordered pair of
stable region IDs. Recompute only the union and its neighboring adjacencies.
Stop when every admissible delta is nonnegative. This is the stated local
optimum; it is not a global multicut claim.

One-ring boundary refinement uses the same complete energy. In each of at most
eight stable face-slot sweeps, a boundary face may move to an adjacent region
only if both regions remain nonempty and connected, no hard edge is
internalized, and the exact energy delta is negative. Stop early after a sweep
with no accepted move.

Finally

```text
Gamma_e = 1[label(f_0) != label(f_1)],
role_e  = hard_feature            if Gamma_e and H_e = 1,
          soft_feature_supported  if Gamma_e and H_e = 0 and F_e > 0,
          curvature_closure       if Gamma_e and H_e = 0 and F_e = 0,
          none                    otherwise.
```

Every live face must have one connected final region. Interior boundary arcs
must close or meet a diagnosed junction; open arcs may terminate only on the
source boundary. Any non-finite posterior/energy, invalid connectivity update,
or unsatisfied hard constraint fails closed rather than publishing a partial
partition.

The reference stores only mesh-slot arrays, three scale-response arrays,
priority queues, region sufficient statistics, and sparse adjacency records:
`O(F+E)` storage and no all-pairs face matrix. The deliberately direct bounded
one-sided searches cost `O(3 E N_r log N_r)` in the worst case, where `N_r` is
the largest number of faces reached inside a frozen physical radius. This can
grow superlinearly under refinement; the 100k-face health gate must measure it
before adoption, and any optimization remains a later parity-bound task.

### Slice C local-reference verdict

The companion module
`Geometry.HalfedgeMesh.CurvatureSegmentation.Patches` implements the equations
above as an unadopted serial CPU candidate. It borrows the Slice B feature-evidence
view, reuses the existing deterministic Gaussian mixture, keeps the sealed v1
interface byte-stable, and returns slot-aligned provisional/final regions,
growth facts, region statistics, hard/soft/closure boundary roles, exact merge
and refinement deltas, local-optimum diagnostics, and observational timings.
Every accepted merge or one-ring move is checked against a complete energy
recomputation, and the implementation stores sparse surface-graph records
rather than a dense face-pair matrix.

The candidate matches all seventeen supplied-feature oracle fixtures. The
computed-evidence smooth-transition, ridge, valley, strict hard-fold, plane,
and cylinder controls also pass, as do the frozen seed-density, alternate-
diagonal, scale, bounded-noise, and orientation checks. These are bounded CPU
unit/contract results only; they are not runtime, production-quality,
performance, parameterization, or novelty evidence.

The preregistered one-dual-step perturbation of every automatic seed does not
pass: it terminates at a different exact local optimum and exceeds the frozen
area-weighted variation-of-information limit of `0.01`. The executable
`LocalRagOneStepSeedPerturbationRefutesFrozenStabilityGate` regression retains
this failure. In accordance with the global-optimization escalation gate, the
local candidate is not adopted, its weights and fixtures are not retuned, and
the production-adoption slice is not entered. The completion slice adds only
opt-in profiles that preserve the positive controls, negative oracle, and
bounded health diagnostics before retirement. ARA claim C45 records the
bounded refutation; `METHOD-040` owns a separately reviewed task-local multicut
formulation and must not call a pairwise surrogate the unchanged
regional/turning energy without a proof.

Post-retirement `BUG-163` does not revise that verdict. It exposes the local
candidate through an explicitly diagnostic runtime/config/UI token so the
operator can inspect the exact feature evidence, final boundary roles, and
part colors on `tests/data/sculpt.obj`. The bounded sculpt profile uses fixed
GMM count `6` and patch complexity cost `0.5`; its focused regression requires
3--12 connected parts, mandatory hard-feature boundaries, no unsupported
seed-front closure, and an identical boundary mask under its recorded one-ring
seed perturbation. This single-mesh result is neither the original corpus-wide
adoption gate nor evidence of semantic object-part quality, so METHOD-037
remains the default and METHOD-040 remains open.

### Slice A generated oracle catalog

[`Test.CurvaturePatchContract.cpp`](../../../tests/unit/geometry/Test.CurvaturePatchContract.cpp)
constructs the catalog without external assets. Graph controls use an `8 x 16`
cell grid on `[-1,1]^2`; the cylinder uses 8 axial by 24 periodic cells; the
sphere is the repository icosahedron. The catalog freezes these expected
partitions before either the detector or grow/merge implementation exists:

| Control | Variants | Supplied evidence | Expected patches/boundary |
| --- | ---: | --- | --- |
| Plane, open cylinder, unit sphere | 1 each | zero hard/soft | one patch, no interior boundary |
| Strict fold | `30/45/60` degrees, two diagonal phases each | hard only for strict `angle > 45` | one patch at 30/45; two at 60, with eight hard crease edges |
| Smooth signed transition | `tanh(x/0.20)` generalized-cylinder graph | soft `1.0` on `x=0` | two connected patches |
| Ridge / valley | signed Gaussian graph of width `0.20` | soft `0.9` on `x=0` | two connected patches with soft-supported boundary |
| Nearby feature pair | planar supplied descriptor transitions at `x=-0.25,+0.25` | soft `0.9` on both lines | three connected patches |
| Junction | planar crossed signed descriptors at `x=0,y=0` | soft `1.0` on both lines | four patches and one diagnosed junction |
| Open-boundary termination | planar signed transition at `x=0` | soft `0.8` | two patches; line endpoints lie on source boundary |
| Disconnected surfaces | two disjoint triangles | zero hard/soft | two patches solely by connectivity |
| Same-curvature false boundary | constant planar descriptor | soft `0.55` on `x=0` | one patch; the unsupported provisional split must disappear |

The Slice A tests validate slot counts, evidence ranges, strict fold equality,
connected expected patches, and boundary endpoint/junction topology. They do
not claim that the not-yet-implemented computed detector or patch solver
already reproduces the oracles. Later quality comparisons retain METHOD-038's
area-weighted variation-of-information limit `<= 0.01` and projected symmetric
boundary-distance limit `<= 0.02 D`; raw label IDs and cross-remesh edge-mask
overlap remain forbidden metrics.

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
| Smooth transition | graph surface `p(x,y)=(x,y,q(x))`, `q(x)=0.5(1+tanh(x/0.08))`, on `[-1,1] x [0,1]`, `24 x 48` cells with diagonal phases `0/1`, reference curve `x=0`, seed `1741` | width `0.11` on `[-1.3,0.9] x [0,1.2]`, `31 x 62` cells with diagonal phases `0/1`, seed `9151` | recover the continuous transition without treating it as a hard dihedral |
| Isometric-bend control | flat sheet versus the `60`-degree fold with identical parametric connectivity, reference boundary along the crease, seed `1753` | flat sheet versus the `70`-degree held-out fold, seed `9173` | intrinsic-only evidence and `k_g` fairness are unchanged; extrinsic crease evidence may change |

Screening may read only the left column. Confirmation parameters stay unused
until one candidate implementation, its numeric thresholds, and its source
digest are frozen. The later full corpus still owes sphere, cone/frustum,
saddle, tangent plane-cylinder, cylinder-sphere, ridge/valley, nearby-feature,
junction, open-boundary, and disconnected-component families; this table does
not silently declare those rows complete.

The bounded fold-control custody replay at source `f622cd0e` exercises only the
fold and flat/fold oracle rows above. On both diagonal phases, strict
`theta > 45 degrees` classification reported `0/0/24` crease edges at
`30/45/60` degrees with zero mask error; the maximum normalized flat/fold edge-
length delta was `0.000000038`. Constant supplied curvature retained one v1
component, one connected region, zero boundary edges, and the exact flat-control
payload in every case. This accepted scratch result (ARA C42) validates the
fixture lane, not candidate A-D, the broader screen, held-out confirmation, or
a remeshing/convergence claim.

### Frozen cylinder and smooth-transition controls (checkpoint 4)

Before their runner implementation, the next two screening rows are fixed as
one separate `surface_controls` lane. The open cylinder uses `32` axial by `64`
angular cells, periodic angular connectivity, radius `1`, length `2`, and the
two declared angular phases. Its supplied analytic principal curvatures are
constant `(1,0)`. The two phase meshes must have zero boundary-disabled
`GEOM-071` hard-feature edges, bbox-normalized radial and paired-edge-length
errors at most `1e-6`, and identical one-component/one-region/zero-boundary v1
payloads. This is a tessellation-ring negative control, not a curvature-
estimator validation.

The smooth row is the generalized-cylinder graph

```text
p(x,y) = (x, y, q(x)),
q(x)   = 0.5 * (1 + tanh(x / b)),
b      = 0.08,
k(x)   = q''(x) / (1 + q'(x)^2)^(3/2).
```

The supplied ordered principal-curvature pair is `(max(k,0), min(k,0))`.
Thus the continuous sign-change curve is exactly `x=0`, `z=0.5`, with no
crease discontinuity. Both `24 x 48` diagonal phases must yield zero hard-
feature edges; the fixed-two-component v1 comparison must retain two connected
regions, at most `0.02` label-permutation-invariant face error, and a boundary
network within `0.02 D` symmetric surface-distance upper bound of the exact
curve, with two endpoints and no junction. The runner records zero warmup and
one measured execution. These controls validate only fixture construction,
analytic supplied descriptors, reference-curve measurement, and the unchanged
v1 comparison lane. They do not execute A-D, select v2, read confirmation
parameters, or support convergence, performance, runtime/UI, or production
claims.

The accepted non-claim scratch-007 replay at exact source `251f2dab` passed all
seventeen frozen fixture gates. The cylinder pair recorded zero hard features,
exact v1 payload parity, at most `0.000000008` normalized radial error, and at
most `0.000000015` paired-edge delta. The smooth pair recorded zero hard
features, zero label and boundary-mask error, exactly two regions and 24
reference boundary edges, two endpoints, no junction, and a normalized sampled
symmetric-distance upper bound of `0.000049835`. ARA C43 limits this result to
fixture/oracle integrity; it does not select or evaluate candidate A-D.

Final retirement replay scratch-011 repeated the unchanged scientific rows
against METHOD-038's completed evidence-only task bytes. Its result, raw row,
portable bundle, and independent audit pass the same seventeen gates and keep
`claim_authorized: false`. Scratch-008 and scratch-010 remain immutable
history; scratch-009 records a replay-tooling failure caused by a missing host `jq` command. The
replacement protocol changed only post-processing to Python and did not change
any fixture, raw-result field, metric, threshold, or gate.

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
5. **Selection.** METHOD-038 made no selection. METHOD-039's smaller
   feature-first formulation failed its frozen seed-location gate and remains
   an unadopted comparator. METHOD-037 remains available, and METHOD-040 owns
   the next CPU-reference attempt before any later atlas-hint evaluation.

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

METHOD-039 adds two explicit runner modes without changing the default lane:

```bash
INTRINSIC_CURVATURE_PROFILE_COHORT=feature_patch_smoke \
  build/ci/bin/IntrinsicCurvaturePatchProfile <output-directory>
INTRINSIC_CURVATURE_PROFILE_COHORT=feature_patch_health \
  build/ci/bin/IntrinsicCurvaturePatchProfile <output-directory>
```

`feature_patch_smoke` writes three manifest-bound rows. `feature_smoke`
checks computed soft-feature support and continuous-boundary error on both
smooth-transition diagonals. `quality_smoke` feeds the same computed evidence
into the local patch candidate with the declared boundary-adjacent test-seed
override and checks the exact two-region reference. `seed_refutation` replays
the unchanged automatic seeds and their one-legal-dual-step perturbation; its
`passed` status means the preregistered failure (`VI > 0.01`) was reproduced.
It must never be presented as positive stability evidence.

`feature_patch_health` runs the unadopted local candidate twice on an exact
100,000-face homogeneous plane with zero supplied evidence. It requires one
complete region, no boundary, identical deterministic payloads, finite
nonnegative stage timings, and result storage bounded by a fixed multiple of
surface slots. The manifest's wall-time and peak-working-set limits are broad
harness-health guards, not a baseline, acceleration target, or performance
claim. All four manifests set `candidate_adopted: false`; none reads held-out
confirmation data or changes production selection, config, runtime, UI, or
property publication.
