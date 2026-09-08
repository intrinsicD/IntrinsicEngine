**Recommendation: one experiment, "feature-component atoms plus boundary relaxation."** Replace the 64 farthest-point seeds with connected curvature components, keep the existing LSCM-validated merge, and add a bounded post-merge boundary relaxation pass. Compare it against the current growth-only arm under budgets taken from the published 1.35 examples.

**Separate the two distortion questions first.** The native xatlas numbers and the five-chart sculpt result are different things. On sculpt, xatlas reaches lower maximum stretch than the merge pipeline only by spending far more charts. The five-region sculpt decomposition comes from the soft normal-turn growth cost, not from the earlier thickness or anatomical labels, which the report already links as failed and which should not be reused as atoms. The high native frog maximum stretch belongs to one native chart and says nothing about the segmentation. To make this explicit, add a matched-mean sweep as a control: adjust the stretch limit until area-weighted mean stretch equals the xatlas value on each mesh, then compare chart count, seam length and occupancy at that point. That replaces the current operating-point comparison with one the report can defend.

**Experiment design.**

1. Atoms. Threshold the integrated normal-turn edge cost on the dual graph and take connected components of low-turn faces as initial atoms. Components larger than the current patch scale are split by farthest-point seeding inside the component, so smooth meshes still get usable atoms without any shape-name rules. Tiny components below a face-count floor are absorbed into their lowest-turn neighbor before any solve.
2. Constrained merges. Keep the existing seam-removed ranking and LSCM acceptance. Add two constraints: the union's boundary-length-to-area ratio may not exceed a fixed multiple of the disk-optimal ratio, and merges that would create a chart wider than a fraction of the atlas side are deferred. Both are compactness rules aimed at packing, not at distortion.
3. Boundary relaxation. After merging, iterate over faces in a one-ring band along each seam. Move a face to the neighboring chart when it shortens seam length or reduces boundary turning, accept only if both affected charts remain disks, the two re-solved LSCM maps stay under the stretch and anisotropy limits, and no foldover appears. Cap iterations and stop when no move is accepted. Only the two touched charts are re-solved, which bounds cost.
4. Packing. Unchanged xatlas UV-only packing, same resolution and padding, same post-pack audit.

**Budgets and falsifiable gates.** All numbers reference the published 1.35 examples and the growth-only ablation arm as baseline.

| Gate | Pass condition |
| --- | --- |
| Validity | Post-pack audit passes identically: disks, oriented triangles, no intersections at the current tolerance |
| Stretch | Maximum below 1.35, area-weighted mean within 0.01 of baseline, 95th percentile not above baseline, anisotropy below 2 |
| Charts | Count within one of the baseline at the same limit |
| Seams | Normalized seam length reduced by at least 10 percent on frog, sculpt and fandisk |
| Jaggedness | Seam turning count per unit normalized length reduced by at least 25 percent on the same meshes |
| Packing | Continuous UV occupancy up by at least 5 points on frog and sculpt with unchanged packer settings |
| Seed dependence | Face-label agreement across three seed permutations exceeds the farthest-point baseline agreement |
| Timing | Reference cost within two times the baseline on the same single-sample harness |

Any single failed gate rejects the arm. Bunny remains a validity and count check only, never a quality-win claim, given the native validity failure.

**Controls.** The fifteen generated controls must produce the same chart structure as today: single near-isometric charts for folds and cut cylinders, multiple disks for annuli, two to four charts for spheres. The same-surface fourfold subdivision check must keep maximum stretch under the limit before and after packing. Run four ablations so the gain can be attributed: atoms only, relaxation only, compactness constraints only, and all three. Report the curvature threshold sweep across at least three values, because a result that holds at one threshold is a tuning artifact.

**Tradeoffs to acknowledge.**

- Curvature components carry a new threshold. On smooth meshes they reduce to farthest-point seeding inside one large component, so the gain will concentrate on featured meshes like sculpt and fandisk.
- Compactness constraints can raise chart count or mean stretch. The chart and stretch gates exist to make that cost visible rather than hidden.
- Boundary relaxation is greedy and local. It straightens seams but cannot relocate a badly placed cut, so it should not be advertised as a global optimizer. The global direct-growth comparison stays open.
- Straighter seams do not guarantee better packing. Long thin charts pack worse than jagged compact ones, which is why the occupancy gate is independent of the seam gate.
- Costs are still reference diagnostic timings, not a speedup claim. The Release and native matched cohort remains prerequisite to any production statement.

If the arm clears every gate on frog, sculpt and fandisk while the controls stay unchanged, the report can retire the jagged-boundary caveat and promote feature-component initialization to the retained configuration. If it clears the seam gate but fails packing, the finding is that boundary quality and packing need separate treatment, which is itself a useful negative result to record.
