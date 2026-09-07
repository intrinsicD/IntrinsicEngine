# Frog parts experiment — METHOD-042

The implemented experiment does **not** yet produce a sculpt-quality frog
decomposition. It distinguishes two concrete obstacles: the existing objective
disfavors the authored competitors tested here, and a separate concavity/neck
comparator misses useful cuts when its proposal bank changes with sampling.
No production default, native algorithm or UI selector was changed.

This is an interactive Codex/Claude experiment, not an accepted new backend.
Codex implemented and executed the tools; Claude supplied design and source
critique. The [record](../../ara/evidence/diagnostics/method042/record.json)
retains all final control cells, initial failed configurations, native comparison
records, source/input/output hashes and exact commands. Large meshes and full
geometry/label exports remain local. All measurements are exploratory and
non-publication-eligible. Claims are scoped by [C63–C65](../../ara/logic/claims.md).

## What was tested

The prior [METHOD-040 comparison](../../methods/geometry/curvature_segmentation/boundary_partition_report.md)
remains the baseline. Sculpt's hard boundaries already delineate its patches;
frog requires smooth boundary decisions. The new seam oracle evaluates entire
connected face partitions, not isolated lines or a requested part count.

The native runner now writes feature confidence with double `max_digits10`.
Its previous six-digit sidecar was suitable for display but not exact energy
replay. This is the only C++ change; no detector/solver parameter changed.

For the fixed `curves` profile, the independent evaluator reconstructs

`E040(P) = sum_cut .04 (L_e / D) (1 - 4 F_eff,e^3) + pi*.04^2 K(P)`.

`D` is the axis-aligned bounding-box diagonal; `K` counts connected regions,
including disconnected pieces with the same supplied label. Hard edges are
exact must-cut constraints. Long-hard-curve attenuation is reconstructed, and
infeasible partitions have no finite reported energy. Model fitting is zero in
this profile. Native final labels include the native cleanup; the authored
competitors and local baseline receive **no additional cleanup**.

The [pilot specification](../../tools/diagnostics/curvature/frog_parts_pilot.json)
was fixed before evaluation. It applies three coordinate planes only to the
largest METHOD-039 local region, preserving the four tiny hard-constrained
regions. These labels are provisional competitors, not anatomical ground truth.

| Partition | Connected regions | E040 | Change from local labels under E040 |
| --- | ---: | ---: | ---: |
| Local baseline | 5 | 0.027218 | — |
| Upper transition | 6 | 0.093630 | +0.066412 |
| Left appendage | 7 | 0.081350 | +0.054132 |
| Right appendage | 6 | 0.085569 | +0.058351 |
| Combined planes | 9 | 0.206113 | +0.178895 |
| Native `curves` final | 15 | -0.980260 | -1.007478 |

All are hard-feasible. This rules out better search *alone* for these specific
competitors under E040, not for every useful frog decomposition. Energy values
from different objectives below are not compared with E040.

The single-plane source-edge seams are approximately 1.31, 1.24 and 1.40 times
the exact within-triangle contour length. Their Euclidean curve-union Hausdorff
distance bounds are respectively `[.00947,.01451] D`, `[.00840,.01358] D` and
`[.01003,.01575] D`. Endpoints/midpoints are sampled against complete segments;
the 1-Lipschitz distance function gives the conservative upper bound by adding
one quarter the longest source segment. Plane distance is also recorded but
is explicitly only a lower bound, not a substitute for surface-curve distance.
No geometric acceptance tolerance was declared, so this is approximation
evidence, not a representability pass/fail verdict.

## Independently named offline comparator

`neck_sweep_cpu_diagnostic_v1` is a deterministic, standard-library Python CPU
reference for this **diagnostic formulation**, not a selectable IntrinsicEngine
backend or a replica of a cited paper. It requires one closed, consistently
oriented, nondegenerate triangle surface with positive signed volume; it does
not repair input, test self-intersections or establish vertex-link manifoldness.
It writes detached source-face labels without changing the mesh.

The frozen v4 cost is

`EN(P) = sum_cut (L_e/D) [1 + 16*.02*(D*kappa_e)] + .005 K(P)`,

where `kappa_e = dot(n_g-n_f,c_g-c_f) / |c_g-c_f|^2`, with outward unit normals
and face centroids. Positive means convex in this convention. The v4 variant
uses raw per-edge values: `.02` scales curvature but performs **no smoothing**.
Signed costs are intentional and bounded on the finite partition space.
Making every edge positive would reject every proposed refinement, not fix it.
This is still a boundary-curvature energy; its only regional evidence is the
separate geometric proposal gate, whose benefit has not been established.

Eight deterministic farthest landmarks generate face-dual Dijkstra distance
fields. Area-quantile sublevel sets from 2% to 98% in 1% steps are scored by
`N=L/sqrt(min(A,A_complement))`, using normalized edge lengths and surface area.
Five-sample averaging defines profile minima. Relative prominence is
`(min(left_max,right_max)-minimum)/min(left_max,right_max)` in complete ten-step
windows on both sides. A minimum of .15 admits a two-quantile proposal band.
Exact energy selects legal refinements inside those bands, counting every
connected child and requiring each child to occupy at least 2% of total area.
Original boundaries are never merged; greedy refinement stops after no
negative change or twelve accepted splits.

The fields are a frozen global proposal bank, not harmonic functions or
part-local geodesics. Old part boundaries do not constrain their construction.
The profile's 50% normalization kink, whole-mesh windows, grid anisotropy,
coarse quantiles and minimum area all restrict the proposal family. Digits and
asymmetric/off-axis necks are not validated. The profile gate cannot be equated
with anatomical part salience.

## Retained negative results

The synthetic surfaces of revolution have explicit oriented poles and cosine
axial sampling. Neck radius is
`sqrt(1-u^2) [rho+(1-rho) sin(pi*u)^2]`, with `x=2u`.
`rho` is waist radius, **not** the ratio to maximum lobe radius. Even `rho=.9`
has a mild waist and is not classified as a convex control. Decorative cases
use a sphere with a 12% outward Gaussian belt or 5% inward Gaussian groove,
both with axial width parameter `.10`.

- v1 face-mean averaging and v2 transverse-weighted averaging both miss the
  severe coarse neck. v3 unfiltered energy favors its explicit equator, but
  smoothed profile minima select neighboring quantiles. v4 broadens only the
  proposal band and finds that equator. All three earlier attempts are retained.
- The final cohort has 45 geometry/sampling cells: nine shapes at 32×24,
  flipped 32×24, 64×48, flipped 64×48 and clustered 32×24 sampling. Sphere,
  ellipsoids of aspect 4/8, belt and groove remain unsplit in all five variants.
- Severe neck30 has region counts **2,2,1,1,1**; moderate neck50 has
  **1,1,2,2,1**. Neck70/90 stay unsplit. Sampling stability is therefore
  refuted, not rescued by the decorative-control results.
- On the dense severe neck, the explicit equator has `delta EN=-2.448812`,
  while the best tested minimum-area sweep *without* prominence gating has
  `delta EN=+0.163316`. Thus this proposal bank lacks a favorable candidate
  although a representable favorable seam exists. This does not isolate one
  cause among distance anisotropy, landmark placement and quantile aliasing.
- Six gate ablations are retained. Regional-only gives 10 neck, 13 groove and
  2 ridge regions; concavity-only gives 2, 1 and 1. There is no demonstrated
  advantage for the full regional gate on these controls.
- Frozen frog v4 adds **zero splits** and retains five local regions, the
  largest occupying **99.7304%** of area. It is not an improvement over the
  existing native `curves` visualization.

A proposed rotation-invariance test also failed: the AABB diagonal changes on
rotation. For fixed labels, EN's curvature integral cancels D, but its length
term does not. Tests now separately verify uniform scale/translation invariance
and the exact predicted rotation dependence. This records a limitation, not a
loosened numeric tolerance. No algorithm-level rotation invariance is claimed.

## Review and next decision

Claude's [source critique](../../ara/evidence/diagnostics/method042/claude-source-review.md)
contains findings later withdrawn in the
[corrected verdict](../../ara/evidence/diagnostics/method042/claude-audit-corrections.md):
lengths were already normalized, negative costs were intentional, and unchanged
diagonal-flip outcomes did not prove a flip-based failure mechanism. Codex ran
the rotation test; Claude's wording attributing that test to itself is not an
execution record. “SDF” in the proposed follow-up means **shape diameter
function**, not a signed-distance field.

Do not promote or port v4 into an engine selector. The next comparison belongs
to [METHOD-043](../../tasks/active/METHOD-043-thickness-and-curve-parts-comparison.md):
test proposals aligned to explicit within-triangle curvature curves and an
independent thickness/enclosure signal, with asymmetric necks and matched
sampling controls. Neither is implemented here, and neither guarantees good
parts. The subsequent four-round [thickness continuation](frog_thickness_parts_experiment.md)
is now recorded separately; it remains unadopted. Existing METHOD-039/040 rejection tests and production METHOD-037 remain
unchanged; UI-053's extremum overlay is independent.

Primary sources reviewed for formulation selection:

- [Lee et al., mesh scissoring (2005)](https://cg.postech.ac.kr/papers/mesh_scissoring.pdf):
  contour completion and part salience motivate evaluating whole separators;
  this experiment does not implement their complete pipeline.
- [Shapira et al., consistent mesh partitioning and skeletonisation (2008)](https://iww.inria.fr/sed-sophia/files/2016/07/Consistent-mesh-skeletonisation.pdf):
  shape-diameter measurements offer a distinct interior-aware comparator.
- [CGAL surface mesh segmentation](https://doc.cgal.org/latest/Surface_mesh_segmentation/index.html):
  a maintained shape-diameter implementation reference; no CGAL dependency
  was installed or used here.
- [Zhuang et al., feature-aligned segmentation (2017)](https://link.springer.com/content/pdf/10.1007/s41095-016-0071-3.pdf):
  informs the boundary-representation comparison; neither METHOD-040 nor this
  diagnostic implements its full anisotropic formulation.

## Reproduction and verification

Use fresh output directories. Native input paths are local, not bundled assets.
The tools require only Python's standard library; scientific renderings are
optional local inspection artifacts, not generated ground truth.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCurvatureBoundaryMesh IntrinsicTests
python3 benchmarks/runners/curvature_boundary_cohort.py \
  --runner build/ci/bin/IntrinsicCurvatureBoundaryMesh \
  --dataset-root /home/alex/Dropbox/Work/Datasets/obj \
  --meshes sculpt frog --modes local curves --output build/parts-native
python3 tools/diagnostics/curvature/evaluate_part_seams.py \
  --cohort build/parts-native/cohort.json \
  --spec tools/diagnostics/curvature/frog_parts_pilot.json --output build/parts-oracle
python3 tools/diagnostics/curvature/neck_sweep_controls.py \
  --config tools/diagnostics/curvature/neck_sweep_v4.json \
  --sampling --ablations --output build/parts-controls
python3 tools/diagnostics/curvature/neck_sweep.py \
  --config tools/diagnostics/curvature/neck_sweep_v4.json \
  --cohort build/parts-native/cohort.json --mesh frog --output build/parts-frog
python3 tests/regression/tooling/Test.PartSeamOracle.py
python3 tests/regression/tooling/Test.NeckSweep.py
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

The two new tooling suites are included in `ci-docs` and require no external
dataset. The dense-neck regression preserves the frozen failed decision; it
does not present that decision as desired segmentation behavior. No optimized,
GPU/Vulkan or sanitizer execution is asserted for this Python diagnostic.

Executed locally: canonical `ci` configure and `IntrinsicTests` build with
Clang 23; the CPU selector had 4,302 passes, six capability/environment skips
and zero failures. Both new tooling suites passed (18 tests), as did the two
existing viewer suites (24 tests). Four native results were sealed per input
with distinct identities and passed strict schema-v2 validation; the source
cohort still contains its original hash-bound **raw** native payloads. Direct
validation of raw payloads and an initial shared run-ID seal were rejected;
the raw data were not edited to pass. Method/benchmark manifests and the
task/ARA/link/layout/docs-sync checks passed. Root hygiene retains its known
non-fatal local `.agents/` warning. See the
[verification record](../../ara/evidence/diagnostics/method042/verification.json).
