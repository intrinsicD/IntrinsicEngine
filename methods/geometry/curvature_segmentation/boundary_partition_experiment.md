# Boundary and regional partition experiment

Status: overnight engineering candidates, 2026-09-06; no accepted replacement.
The user authorized implementation, varied local-mesh testing, iterative fixes,
and bounded Claude collaboration. The prior review remains the motivation.

## Candidates and fixed controls

`boundary_multicut_isotropic_v1` used `sum ell (1 - 4 F) cut`. The inherited
moderate false-feature fixture rejected that weight before any dataset run.
Its source and failed log are preserved in the task evidence.

`boundary_multicut_isotropic_v2` uses alpha=1.5. The tiny exact oracle and the
17 inherited supplied-evidence fixtures remain unchanged. The heuristic uses
sparse additive contraction, bounded boundary-front KL moves, and positive-
capacity minimum-cut split proposals accepted on the original signed energy.
A gap-completion control exposed the need for split proposals. A frog work-limit
run exposed whole-region KL scans; boundary-front initialization and a 64-move
non-improving budget replaced those scans without raising the total move cap.

The pure boundary objective remains a comparator. The first varied cohort and
matching views motivate a separately named regional candidate: do not attribute
its outputs to a better optimizer of the pure multicut energy.

## Regional candidate: curvature_region_scale_v1

Freeze before its first integrated mesh execution:

- Physical radius ratio `r=0.02`, feature weight `alpha=1.5`.
- Face weight `A_f/D^2`, with D the mesh bounding-box diagonal.
- Face descriptor is the mean of its vertices' two values
  `(atan(r D k_max), atan(r D k_min))`. It is bounded, dimensionless, and
  invariant to uniform geometry/curvature scaling. Orientation reversal swaps
  and negates the coordinates and therefore preserves squared distances.
- Region model is the area-weighted descriptor mean. Its fitting cost is the
  area-weighted sum of squared distances, accumulated with weighted variance
  updates. The model coefficient is one.
- Boundary cost is `r ell (1 - alpha F_eff)`, where `ell=length/D`.
- Region-count cost is `pi r^2` per **connected** region. This is an explicit
  scale-coupled complexity prior, not the former runtime beta=0.5.
- Hard edges remain exact must-cut constraints. Near a hard crease only,
  `F_eff = F (1 - exp(-(d/r)^2))`, where d is the normalized shortest path
  through face centers and edge midpoints to a hard edge. With no hard edge,
  F_eff=F. This changes the candidate's cost, not the source detector output.
  It tests whether soft parallel strips duplicate an already mandatory crease.

The total energy is regional variance plus region-count cost plus signed
boundary cost. It is **not pairwise additive multicut** and is not a reproduction
of the full Mumford-Shah or Zhuang algorithms. Exact enumeration on at most eight
nodes evaluates the complete energy. Larger instances use region-model merge
increments and bounded binary split proposals with fixed trial model means;
every proposal is evaluated after accounting for connected output pieces.
Pure-boundary KL remains available only when regional costs are absent, since
changing a region mean invalidates all of its cached per-node model gains.

The direction draws on the previously reviewed piecewise-field and prescribed-
feature-scale literature. The precise descriptor, combined energy, and numeric
controls here are repository-specific engineering choices, not literature claims.

## Limits and evidence protocol

Both candidates use 8 outer sweeps, 2,000,000 attempted moves, 20,000,000 flow arc
visits, 4 split trials per region, and relative energy tolerance 1e-12. Terminal
trials include a far-apart descriptor pair when a regional model is enabled.
Graph storage is sparse; all max-flow traversals are iterative. Work-limit
failures return diagnostics without a partial partition. Larger results are
heuristic; success means convergence of the bounded move generator, not global
optimality or initialization-independent optimality.
Sparse storage does not imply linear running time: regional-model contractions
may refresh all neighbors of a repeatedly merged high-degree region, giving
quadratic work in that case. Move and flow counters bound their named operations,
not every arithmetic or adjacency operation. The external cohort process also
has a 300-second wall-time limit. The final `curves` profile disables regional
fitting; no large-mesh performance guarantee follows from these local runs.

The known cases are sculpt and frog. The preselected comparison cohort is
fandisk, bunny10k, dolphin, elephant, bumpy_torus, genus3, office_chair, sphere,
cube, and saddle2. After first inspection, those cases are comparison data,
not unseen hold-outs for later revisions. Input hashes, source hashes, raw
outcomes, topology/load failures, and failed candidates are retained. Mesh files
remain local and unchanged. No external dataset is required by CPU unit tests.

Quality assessment combines matching-view plots, area distributions, feature
support and closure lengths, hard-constraint preservation, and the frozen
analytic controls. Region count alone is not a quality metric. Timings are local
engineering observations, with no accepted performance or general quality claim.

## Regional calibration update

Before any regional dataset execution, coefficient one failed the unchanged
ridge and valley supplied oracles; its test log is retained. Candidate
`curvature_region_scale_v2` fixes the fitting coefficient to 0.03, keeping the
radius, complexity prior, hard-crease attenuation, fixtures, and thresholds
unchanged. This is analytic-pilot calibration of a new objective, not a repair
of the previously rejected candidate or an adoption verdict.

## Contour contrast candidate

`boundary_feature_contrast_v1` is frozen before execution as a separate boundary
objective: `sum r ell (1 - 4 F_eff^3) cut + pi r^2 region_count`, r=0.02,
with the same exact hard constraints and hard-crease exclusion radius r.
It has no regional fitting term. The cubic contrast gives positive cost at the
unchanged weak false-feature control F=0.55, negative cost at the salient
F>=0.8 controls, and stronger rewards for high-confidence paths than the linear
alpha=1.5 candidate. F is an engineering confidence, not a calibrated probability.
The quality question is whether this selects longer completing contours without
retaining excessive small regions; all previously seen meshes are comparison
cases. It cannot satisfy the inherited unmarked-curvature split oracle when F=0
and therefore cannot receive METHOD-040's frozen positive adoption verdict.
That conflict is retained explicitly; it does not justify modifying the oracle.

## Explicit area cleanup candidate

`boundary_feature_area_cleanup_v1` keeps the contrast candidate's exact controls,
then agglomerates regions smaller than `pi r^2` in normalized area A/D^2.
At each step, among adjacent pairs with at least one undersized region, it picks
the greatest energy decrease (or least increase); hard-blocked pairs are never
eligible. Hard constraints propagate across merged region boundaries. It stops
when every remaining undersized region has no legal adjacent merge and reports
that count, rather than crossing a hard feature or deleting faces. All ties use
canonical region IDs. This is an explicit postprocessing policy, not minimization
of the preceding cut energy; optimized and final energies and forced merge counts
are separate, and any changed result relinquishes the exact-optimum flag.
The radius and area rule are frozen before the first execution of this candidate.

The cleanup family also has predeclared medium/coarse profiles with radius ratios
0.04 and 0.08, the other two scales already used by the detector. Boundary scale,
hard-feature exclusion radius, region cost pi r^2, and minimum area pi r^2 change
together; the detector and confidence contrast stay fixed. These profiles are
compared across the entire cohort, never selected separately per mesh. They are
not promised to produce nested partitions. The 0.02 profile remains the original
frozen candidate; comparison at another scale does not repair its failed oracle.

## Coverage of established hard curves

`boundary_feature_curve_coverage_v1` freezes the medium cleanup profile (r=0.04)
with one additional condition before its first mesh execution: a connected primal
hard-edge component seeds soft-feature attenuation only if its total normalized
length is at least r. Short isolated hard facts remain exact cut constraints, but
do not suppress surrounding soft evidence. The comparison tests whether the
radial attenuation in earlier candidates removed useful evidence around isolated
hard edges. All other controls and input evidence stay fixed. The region graph's
unmergeable-small count is conditional on that graph and the chosen merge stage;
it is not proof that every feasible segmentation must contain those small parts.
