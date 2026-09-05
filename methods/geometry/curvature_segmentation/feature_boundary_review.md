# Feature-aligned geometric patches: objective review

Date: 2026-09-06. Status: literature and objective review; proposed METHOD-040
experiment, not an implemented or accepted replacement. The operator requested
this review after the default diagnostic METHOD-039 result on `frog.obj` was
much less useful than on `sculpt.obj`.

## Intended result

The clarified target is connected, geometrically meaningful regions: retain
prominent feature paths, complete useful interrupted contours, and avoid a
collection of tiny fragments. Perfect anatomy, semantic labels, and a prescribed
number of parts are not requirements. Strong curvature alone does not determine
which lines form useful separating boundaries; selecting and completing those
lines is part of the partitioning problem.

This differs from defining a patch primarily by statistical coherence of its
signed-curvature descriptor. The earlier shortlist remains relevant; the next
experiment should change the objective before adding semantic machinery.

## What the current evidence establishes

The [frog/sculpt diagnostic](../../../ara/evidence/tables/curvature_frog_diagnostic_2026-09-06.md)
records single executions of the public CPU geometry path. The default frog run
returned five regions, with 19,019 of 19,106 faces in one region. It did not
reproduce the exact two-region live-app result. Reducing only the per-region
cost still left a dominant region. At fixed six mixture components and region
cost `0.05`, removing the turning term changed 11 regions, largest 18,936 faces,
to 18 regions, largest 3,340 faces. These counts establish sensitivity, not
visual quality or an acceptable replacement. The mixture-component count is
not the final region count.

The source exposes three distinct mechanisms:

1. **Curvature agreement and a region-count cost can defeat feature evidence.**
   `RegionCost` adds the best accumulated component likelihood to a fixed cost
   per region. If two adjacent regions share a minimizing component, merging
   them changes this regional cost by exactly `-beta`. Consider an isolated
   shared closed boundary, no hard edge, confidence one throughout, and turning
   disabled. Its length/support energy is `(0.01 - 0.02) L = -0.01 L`, where
   `L` is length divided by the mesh bounding-box diagonal. Removing the boundary
   gives `Delta E = -beta + 0.01 L`. With runtime `beta=0.5`, any such boundary
   with `L < 50` is cheaper to remove. This is a conditional algebraic example,
   not a measurement that every frog merge meets these assumptions. At the
   original reference `beta=0.0025`, the corresponding threshold is `L < 0.25`.
2. **Raw edge turns can measure tessellation zigzags instead of the intended
   smooth contour.** The current contribution is squared intrinsic turn divided
   by the mean adjacent normalized edge length. The analytical example below
   demonstrates this representation risk; changing only the optimizer cannot
   remove it.
3. **The local move set cannot recover arbitrary boundaries after merging.**
   Greedy region merges and single-face moves into existing neighboring regions
   do not provide a general region-splitting move. The already frozen seed
   perturbation failure remains a separate, executable rejection of this local
   solver.

These observations follow from
[`RegionCost`, `VertexTurnContribution`, and the merge/refinement implementation](../../../src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cpp).
They do not establish that the detector is sufficient: frog has many retained
soft fragments, but whether they mark useful contours still needs overlays.
Changing detector thresholds and partitioning simultaneously would obscure that
question. Boundary-role labels also have a recorded region-pair aggregation
problem; quality measurements must intersect final boundaries with the original
per-edge hard/soft evidence rather than trust those labels as detection counts.

### Analytical check of edge-turn representation

On a fixed planar unit-square support, let `D=sqrt(2)` and
`p_i=(i/n, 0.5+(i mod 2)/n)` for even `n` and `i=0..n`.
Each edge has normalized length `1/n`; each of the `n-1` interior turns is
`pi/2`. Thus `G = n(n-1)(pi/2)^2`, while the normalized Hausdorff distance to the
straight line is at most `1/(n sqrt(2))`.

| Segments | Distance bound | Weighted turn cost, `0.001 G` |
| --- | --- | --- |
| 8 | 0.08839 | 0.13817 |
| 32 | 0.02210 | 2.44766 |
| 128 | 0.00552 | 40.11007 |

The formula correctly penalizes the jagged curve: Hausdorff convergence alone
does not imply convergence of bending energy. This is a formula-level example,
not a C++ execution or a refutation of convergence for smooth curve
approximations. It motivates testing boundary representation at a physical
scale instead of assuming raw source edges provide a smooth contour.
The [formula record and reproduction snippet](../../../ara/evidence/diagnostics/curvature_frog_2026-09-06/objective_analysis.json)
preserve both analytical examples; ARA claim C58 bounds these formula-level
statements separately from the earlier CPU replay.

## Existing shortlist, reconsidered

| Primary source | Relevant mechanism | Disposition for this goal |
| --- | --- | --- |
| [Zhuang et al., 2017, *Feature-Aligned Segmentation using Correlation Clustering*](https://link.springer.com/content/pdf/10.1007/s41095-016-0071-3.pdf) | Correlation clustering selects features and their completing boundaries together using signed cut costs; anisotropic length follows surface geometry. Smoothing follows partitioning. | Leading candidate. Its lack of guaranteed semantic parts does not exclude the clarified geometric goal. |
| [Keuper et al., 2015, *Efficient Decomposition of Image and Mesh Graphs by Lifted Multicuts*](https://openaccess.thecvf.com/content_iccv_2015/html/Keuper_Efficient_Decomposition_of_ICCV_2015_paper.html) | Feasible multicut heuristics support nonlocal partition decisions and optional long-range terms. | Solver lineage, with an exact tiny-graph oracle. A heuristic remains a heuristic; lifted edges need evidence before adding them. |
| [Bonneel et al., 2018, *Mumford-Shah Mesh Processing using the Ambrosio-Tortorelli Functional*](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13549) | Piecewise-smooth field approximation couples restoration and discontinuities. | Alternative if a suitable field is the main signal. More formulation and solver work; choosing a curvature field alone does not settle useful patch boundaries. |
| [Golovinskiy and Funkhouser, 2008, *Randomized Cuts for 3D Mesh Analysis*](https://gfx.cs.princeton.edu/pubs/Golovinskiy_2008_RCF/index.php) | Repeated randomized decompositions expose persistent cuts and segmentation uncertainty. | Useful comparison or stability evidence; consensus is not ground truth and adds repeated solves. |
| [Gehre, Lim, and Kobbelt, 2016, *Adapting Feature Curve Networks to a Prescribed Scale*](https://www.graphics.rwth-aachen.de/publication/03258/) | Feature networks are simplified according to a prescribed geometric scale. | Supports an explicit physical scale for meaningful detail; not a complete replacement partitioner. |
| [Gehre, Lim, and Kobbelt, 2018, *Feature Curve Co-Completion in Noisy Data*](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13337) | Recurring feature configurations guide completion and pruning. | Later detector/completion option where repeated patterns exist, not an assumed prerequisite for frog. |

Review depth: Zhuang's formulation and Randomized Cuts were read from full
primary PDFs; the other rows use primary abstracts/project descriptions and the
existing intake. This is a bounded shortlist review, not a systematic survey or
a claim that these are the latest methods. The frozen
[METHOD-038/039 intake](feature_aligned_intake.md) remains historical evidence.

## Proposed METHOD-040 experiment

Use a **new boundary-first objective**, explicitly distinct from METHOD-039's
regional GMM and turning energy:

```text
minimize E(x) = sum_e ell_e (a_e - alpha F_e) x_e
x_e = 1 exactly when the adjacent faces have different region labels
H_e = 1 implies x_e = 1
ell_e = source-edge length / D
```

Here `F_e` and `H_e` are the unchanged retained soft and hard evidence. Valid
partitions enforce cycle consistency; disconnected pieces receive distinct
region IDs. A strong feature can make a cut desirable, while unsupported
closing paths cost positive length. The partition therefore chooses which
fragments justify a completion instead of connecting every detected fragment.
All costs, tolerances, and stopping rules must be frozen before integrated
quality runs. Increasing `alpha` is not a promise of nested partitions or a
particular part count.

Start with full face-dual nodes and `a_e=1`. This isolates joint boundary
selection from GMM region fitting and turning penalties. Do not contract a
seed-grown region before solving: that would remove possible cuts from the
search space. Tiny exhaustive partitions provide the objective oracle; larger
meshes require a deterministic sparse heuristic with split/reassignment moves.
Repeatability and insensitivity to initialization are separate tests. No global
optimality claim follows merely from using a multicut objective.

Then compare an anisotropic `a_e` using the existing
[principal-direction fields](../../../src/geometry/Geometry.HalfedgeMesh.Curvature.cppm),
with dimensionless physical-scale curvature contrast, a strictly positive cost
floor, and deterministic isotropic fallback near umbilics or unsupported
estimates. Treat directions as sign-invariant line fields and pair them with
their corresponding principal values. Freeze this second variant separately;
do not silently insert its metric after inspecting failed outputs.

This is a documented adaptation of Zhuang: that paper uses mesh-vertex nodes
and within-triangle dual boundaries, whereas this first experiment preserves
face labels and source-edge boundaries. Its anisotropic watershed reduction
also restricts the feasible partitions. Neither reduction nor its approximate
solver proves initialization independence. If edge alignment remains the
limitation, test a richer boundary representation explicitly; do not claim the
paper was faithfully reproduced by the face-dual variant.

GMM labels remain available as diagnostics. The first candidate has no regional
GMM likelihood, per-region `beta`, or raw edge-turn energy. Later smoothing
would need its own physical-scale and hard-feature preservation contract.

### Comparison and stopping rules

- Keep the existing detector and all frozen METHOD-039 oracle/stability controls
  unchanged. A new objective must report any conflict with an inherited oracle,
  not relax its threshold after seeing the result.
- Add controlled equal-curvature regions separated by a salient closed contour,
  interrupted contours with competing short/long completions, unrelated nearby
  fragments, weak noise on homogeneous surfaces, and alternate triangulations
  including zigzag representations. Hard folds must remain cuts; a homogeneous
  plane or cylinder may correctly remain one region.
- Use sculpt as the existing positive inspection case and frog as a known
  counterexample. Select additional held-out meshes before parameter calibration;
  do not describe frog as held-out. Freeze parameters on analytic/pilot cases.
- Report supported boundary length, unsupported closure length, region area
  distribution, fragmentation, connectivity, and remeshing/scale stability.
  Include feature and final-boundary overlays at matching views. Neither more
  parts nor greater feature recall alone establishes quality: cutting everything
  can game both. Visual review checks usefulness without claiming semantic
  ground truth.
- Retain every failed outcome. Accept a candidate only with exact-small-graph
  correctness, the inherited stability gates, and evidence of useful boundaries
  on the comparison cohort. The current review makes no such acceptance claim.

Implementation, benchmark manifests, and any later adoption remain owned by
[METHOD-040](../../../tasks/backlog/methods/METHOD-040-global-multicut-curvature-patch-reference.md).
The present deliverable is this revised experiment and its bounded rationale.
