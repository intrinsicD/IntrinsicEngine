# Frog part-segmentation diagnostic

Exploratory serial CPU evidence at `9f917c6b2040e363052a4a2901081c463691195b`,
Clang 23, unsanitized Debug `ci`. Source was clean before this record.
No engine code or defaults changed. Claim C57 records only the bounded replay.

The operator reports satisfactory sculpt parts and two unsatisfactory frog
parts using defaults, and confirms the desired parts include legs, body, and
head. These are user observations, not annotated segmentation ground truth.

## Replay

The public OBJ importer accepted all 19,106 frog triangles (9,555 vertices).
`ComputeCurvature`, `DetectFeatureEvidence`, and `SegmentFeatureAlignedPatches`
were called directly. Automatic mode uses the runtime's default values;
Fixed 6 also matches the existing sculpt regression profile. Each row ran once.

| Input / mixture | Patch cost | Turn weight | Regions | Largest region, faces |
| --- | ---: | ---: | ---: | ---: |
| Frog / Automatic | 0.5 | 0.001 | 5 | 19019 |
| Frog / Automatic | 0.05 | 0.001 | 13 | 18918 |
| Frog / Automatic | 0.0025 | 0.001 | 19 | 18729 |
| Frog / Fixed 6 | 0.5 | 0.001 | 5 | 19046 |
| Frog / Fixed 6 | 0.05 | 0.001 | 11 | 18936 |
| Frog / Fixed 6 | 0.0025 | 0.001 | 18 | 18747 |
| Frog / Fixed 6, ablation | 0.5 | 0 | 4 | 15094 |
| Frog / Fixed 6, ablation | 0.05 | 0 | 18 | 3340 |
| Sculpt / Automatic | 0.5 | 0.001 | 5 | 3348 |
| Sculpt / Fixed 6 | 0.5 | 0.001 | 8 | 3348 |

Frog detection returned 4 source hard edges and 3,010 retained soft edges;
sculpt returned 384 and 808. The fixed-six sculpt control reproduces the
existing regression's eight regions and 620 final boundary edges.
Frog's exact reported two-region UI result was not reproduced: persisted
config, live asset normalization/topology, and app revision were not inspected.

Reducing patch cost alone leaves at least 98% of frog faces in one region.
The matched Fixed-6, cost-0.05 turn ablation changes largest-region occupancy
from 18,936 to 3,340 faces. It isolates a strong dependency on the current turn
term, but does not establish a replacement objective or anatomical quality.
No candidate was selected, and no quality gate was retuned.

The current energy charges a per-patch cost, regional curvature-model error,
boundary length and discrete geodesic turn, and credits soft-feature support.
Merging and one-face refinement do not introduce a new split after a region
has been absorbed. The intended object-part criterion is absent. METHOD-039's
known seed-location refutation remains in force.

Boundary-role diagnostics need care: `HardFeature` is emitted for every edge
between a region pair whose merge is hard-blocked. Thus the default frog row
reports 55 hard-role boundary edges despite only 4 source hard-feature facts.
Direct mask intersection gives 4 hard, 15 soft-supported and 36 unsupported
edges among these 55. Do not use the role count as detector precision or as
proof that every edge lies on a hard crease. This was not repaired here.

## Evidence and reproduction

[Raw results and exact probe sources](../diagnostics/curvature_frog_2026-09-06/results.json)
include source/input hashes, all fourteen parameter rows and the two C++
programs used. The external frog OBJ is not copied into the repository.

To replay, configure/build the `ci` geometry tests, extract `probe_cpp` or
`no_turn_probe_cpp` from the JSON to a temporary `.cpp`, and reuse the compiler
command and module map for `Test.CurvatureTensor.cpp` in
`build/ci/compile_commands.json`, replacing only the source and output object.
From `build/ci`, link that object with `lib/libIntrinsicGeometry.a`,
`lib/libExtrinsicCore.a`, the preset's Debug `libxatlas.a` and `libglm.a`,
`-ldl -pthread`. Invoke the binary with `INPUT.obj EXISTING_OUTPUT_DIRECTORY`.
Both programs write JSON lines to stdout and face labels/edge masks to the
output directory. The ablation changes only `BoundaryTurnWeight` to zero and
uses Fixed 6 with patch costs 0.5 and 0.05.

## Interpretation awaiting operator decision

A part-oriented extension should test concave boundary salience, completion
of incomplete separating contours, and regional shape/thickness evidence.
Extremal curvature alone cannot distinguish an anatomical attachment from an
internal decorative ridge. A broader optimizer could improve local decisions,
but would still need an objective appropriate to the intended parts.

- [Zhuang et al. 2017](https://link.springer.com/content/pdf/10.1007/s41095-016-0071-3.pdf)
  jointly select/connect features through correlation clustering; their
  limitations section explicitly distinguishes organic parts from patches.
- [Lee et al. 2005](https://cg.postech.ac.kr/papers/mesh_scissoring.pdf)
  complete concavity-derived open contours into loops and test part salience.
- [Shapira et al. 2008](https://iww.inria.fr/sed-sophia/files/2016/07/Consistent-mesh-skeletonisation.pdf)
  use shape diameter to provide volumetric part evidence and refine boundaries
  with local concavity cues.

These are candidate directions, not adopted algorithms or promises for frog.
