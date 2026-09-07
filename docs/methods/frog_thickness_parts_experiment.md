# Frog thickness-parts experiment — METHOD-043

**The four-round experiment did not achieve sculpt-quality frog parts.**
Thickness supplies useful lobe-versus-waist information on the declared neck
controls, but the tested persistent-maxima selector creates protrusion patches,
not a reliable whole-limb decomposition. The production method and defaults
remain unchanged. No fifth iteration was run.

Codex implemented and executed the diagnostics; real Claude CLI sessions
provided formulation, failure analysis and final evidence review. The
[record](../../ara/evidence/diagnostics/method043/record.json),
[round ledger](../../ara/evidence/diagnostics/method043/rounds.json) and
[claims C66–C68](../../ara/logic/claims.md) retain successes and refutations.
These are dirty-source, non-publication-eligible CPU observations, not an
accepted engine backend or performance result.

## What changed our understanding

The [earlier comparison](frog_parts_experiment.md) explains why sculpt is easier:
its hard-feature separators already delimit five patches. Frog's few hard
edges do not provide corresponding whole-part separators. METHOD-039 local
patches, METHOD-040 curve coverage/area cleanup, METHOD-041 extremum curves,
and METHOD-042's explicit-seam oracle and failed geodesic neck sweeps remain
distinct experiments. More curvature coverage alone has not established good
frog parts; METHOD-042 also separated unfavorable competitor energy from
missing favorable proposals.

This continuation tested a genuinely different input: rays through the shape's
interior. It did **not** replace the previous objective with another curvature
threshold or assume a requested number of frog parts.

| Round | Frozen experiment | Observed result |
| --- | --- | --- |
| 1 | Independent shape-diameter measurement | All 18 synthetic fields supported; both lobe medians exceed the waist in each neck cell. Frog has 11 unsupported faces despite no missed rays. |
| 2 | Persistent maxima; relative persistence ≥ .25, peak core area ≥ .02 | Negative controls stay one region, but necks contain residual caps/fragments: symmetric 5/101/5, asymmetric 3/3/3, bent 4/4/4. Prediction refuted. |
| 3 | Explicit small-field completion and final connected-area cleanup | All nine neck cells become two regions; all nine negative cells remain one. Native frog is still visually patch-like; sculpt regresses. |
| 4 | Frozen selector, withheld taper/bulge fixtures | Taper stays one region; bulge produces three, not the predicted two, in all five variants. Neither thickness-only result identifies the attachment. No further tuning. |

The first three rounds use sphere, shallow groove, ridge, symmetric neck,
asymmetric neck and bent asymmetric neck, each at 32×24, 64×48 and flipped
32×24 triangulations. Part-count agreement is not boundary-position convergence:
Round 3 neck seams reach up to .04846 times square-root surface area away from
the reference equator in axial position, and move with sampling.

## Native comparison

| Mesh | METHOD-039 local | METHOD-040 curves | Round 3 thickness + baseline intersection |
| --- | ---: | ---: | ---: |
| Frog | 5; largest 99.73% area | 15; largest 26.08% | 11; largest 35.38% |
| Sculpt | 5 | 5 | 24 |

Frog has seven substantive thickness regions before intersection, plus four
preserved tiny baseline islands. In two fixed views, the result separates a
head cap, feet and thigh-like patches, but also a central back patch and a
torso region that joins areas one might want separated. Boundaries remain
jagged. This is a visual assessment, without reference anatomical labels;
region counts do not rank the three methods' semantic quality.

[Frog comparison](../../ara/evidence/diagnostics/method043/frog-comparison.png) ·
[Sculpt comparison](../../ara/evidence/diagnostics/method043/sculpt-comparison.png).
All columns use the same source geometry and cameras. Colors are region IDs.

No post-hoc "sculpt succeeds, so return its baseline" guard was implemented.
Such a held-in no-regression policy was discussed and rejected as a substitute
for evidence about general part decomposition.

## Exact diagnostic formulation

The reference is informed by Shapira, Shamir and Cohen-Or,
*Consistent Mesh Partitioning and Skeletonisation Using the Shape Diameter
Function* (2008), DOI
[10.1007/s00371-007-0197-5](https://doi.org/10.1007/s00371-007-0197-5),
especially its [measurement definition](https://iww.inria.fr/sed-sophia/files/2016/07/Consistent-mesh-skeletonisation.pdf).
SDF here means **shape diameter function**, not signed distance.
The [CGAL segmentation documentation](https://doc.cgal.org/latest/Surface_mesh_segmentation/index.html)
provides a second reference for the distinction between measuring the field
and deriving segments. This experiment is not a reproduction of either full
pipeline: bilateral filtering, Gaussian-mixture fitting and graph-cut
label optimization were **not implemented**.

`shape_diameter_parts.py` accepts one closed, consistently oriented,
positive-volume triangle surface. It checks edge incidence, orientation,
connectedness and nondegenerate faces, but not self-intersections or
vertex-link manifoldness. Internal distances use `sqrt(total surface area)`;
the source geometry is not modified. Outputs are detached face arrays.

At each face center it traces 30 deterministic equal-solid-angle Fibonacci
rays in a 120-degree inward cone, using a serial triangle BVH. The tangent
frame uses the longest directed face edge. The **first** positive surface hit
is found, then rejected unless source and hit normals have negative dot
product. At least six valid lengths are required. Lengths within one
population standard deviation of their median are combined with inverse
polar-angle weights. There is no guarantee this oblique average equals an
analytic diameter; an axis-ray diameter test is separate.

`select_thickness_parts.py` builds descending superlevel components, processes
equal-valued plateaus simultaneously, and applies the elder rule. A peak is
retained when `(peak - saddle)/peak ≥ .25` and its above-saddle core has at
least .02 total area; the global root is initially retained. Canceled peaks
follow parent links, then connected face regions are canonicalized. This
explains the Round 2 failure: core area does not bound exclusive drainage area.

`refine_thickness_parts.py` makes missing data explicit: nearest-valid
completion is permitted only below .005 missing area and within .02 normalized
face-centroid graph distance. Frog fills 11 faces, .026745% surface area, with
maximum distance .005064. The original field remains unchanged. The smallest
connected region below .02 area is repeatedly merged into its longest-shared-
boundary neighbor, including residual root fragments. Native baseline labels
are intersected **after** cleanup; consequently baseline islands may remain
below the cleanup threshold. Round 2's missing-input rejection is preserved.

## What the final withheld controls do—and do not—show

The new surface is a sphere portion ending at `x=.9`, joined continuously to
a tapered appendage `x=.9+1.7t`, radius `sqrt(.19)*(1-t)`. A second fixture
adds `.45*sin(pi*t)*exp(-((t-.55)/.16)^2)` to its radius. The attachment has an
intentional slope discontinuity, so this is not a smooth-anatomy benchmark.
The five variants are base, dense, diagonal flip, reversed vertex/face order,
and a two-axis rotation plus scale/translation of the base.

All faces satisfy the field support rule in all ten runs. Thickness alone
leaves taper unsplit and places bulge seams at `t=.46875… .625`, missing the
attachment at `t=0`. The exact two-region bulge prediction was refuted. Before
execution, Codex corrected Claude's proposed "at the bulge" prediction to
"between body and bulge"; neither phrase is retrospectively used as a pass.
The actual radius maximum is not equated with the Gaussian center.

Native METHOD-039/040 yield 2/2 regions on taper and 4/3 on bulge. Both taper
baselines cut at `t≈-6.53e-17`, i.e. the attachment to rounding precision.
Round 4 deliberately omits baseline intersection to isolate the thickness
cue; it does not test a violation of the full hybrid's hard-cut constraints.
See the [withheld comparison](../../ara/evidence/diagnostics/method043/withheld-comparison.png).

Reordered and rigid/scaled copies preserve every tested thickness boundary
edge, but normalized field values differ by up to .000438. Exact scalar
invariance is **not** established. Ray-frame selection and sample rejection
are candidate causes, not diagnosed mechanisms. Analytic fixed-ray covariance
tests do not prove full-field or general segmentation invariance.

## Review and stopping decision

Claude helped identify core-area versus exclusive-area cancellation and
recommended the final withheld test instead of smoothing the wrong regions.
Its reviews were checked, not accepted automatically: the paper's source/hit
normal comparison, the distinction between a derived sphere angle and the
chosen normal cutoff, and the unimplemented sculpt guard all required
correction. Prompts, responses and the [final review](../../ara/evidence/diagnostics/method043/claude-final.md)
are retained beside the record.

The method skill kept this CPU-reference-first and separate from production;
the results-audit skill narrowed successful synthetic counts to fixture
observations and retained both failed predictions. No runtime/config/UI
selector or selectable backend token was added.

After a new approved work budget, a useful next comparison is the published
SDF segmentation pipeline against this frozen persistent-maxima selector,
using the same controls. This tests a different regional model, not just
smoother boundaries. Inside-triangle curve alignment remains separately
unimplemented: it may improve boundary placement but cannot by itself validate
which regions should represent whole limbs. These are follow-ups owned by
[METHOD-043](../../tasks/active/METHOD-043-thickness-and-curve-parts-comparison.md),
not work silently added after Round 4.

## Reproduction and verification

Use fresh output directories; the tools refuse overwriting experiment results.
The field generation is deliberately slow serial Python. No dataset download
is part of CI. Exact original invocations, configurations, source/input/output
hashes and compact per-cell results are retained in the record. Full local
fields/labels are under `build/method043`; their local paths are not portable
proof. The original round-controller scripts are archived; the shared replay
driver below is label-checked against both original selector cohorts.

```bash
python3 tools/diagnostics/curvature/shape_diameter_controls.py --config tools/diagnostics/curvature/shape_diameter_round1.json --output build/method043/r1/controls
python3 tools/diagnostics/curvature/shape_diameter_parts.py --config tools/diagnostics/curvature/shape_diameter_round1.json --cohort build/method042-native/cohort.json --mesh frog --output build/method043/r1/frog
python3 tools/diagnostics/curvature/shape_diameter_parts.py --config tools/diagnostics/curvature/shape_diameter_round1.json --cohort build/method042-native/cohort.json --mesh sculpt --output build/method043/r1/sculpt
python3 tools/diagnostics/curvature/thickness_parts_controls.py --fields build/method043/r1/controls/cohort.json --config tools/diagnostics/curvature/thickness_parts_round2.json --output build/method043/r2/controls
python3 tools/diagnostics/curvature/thickness_parts_controls.py --fields build/method043/r1/controls/cohort.json --config tools/diagnostics/curvature/thickness_parts_round3.json --output build/method043/r3/controls
python3 tools/diagnostics/curvature/refine_thickness_parts.py --field build/method043/r1/frog/field.json --config tools/diagnostics/curvature/thickness_parts_round3.json --output build/method043/r3/frog
python3 tools/diagnostics/curvature/refine_thickness_parts.py --field build/method043/r1/sculpt/field.json --config tools/diagnostics/curvature/thickness_parts_round3.json --output build/method043/r3/sculpt
python3 tools/diagnostics/curvature/thickness_withheld_controls.py --config tools/diagnostics/curvature/thickness_parts_round4.json --output build/method043/r4/controls
```

The current `ci` configure/build uses Clang 23. CPU CTest selected 4308 tests:
4302 passed, six skipped, zero failed. The 18 new Python tests, 18 earlier
seam/neck tests and 24 viewer tests pass. Structural and benchmark-result
validation are recorded in [verification.json](../../ara/evidence/diagnostics/method043/verification.json).
No new sanitizer, GPU/Vulkan, runtime-integration or performance comparison
was executed. Native benchmark quality-error fields are not anatomical error
measurements and are not interpreted as such.
