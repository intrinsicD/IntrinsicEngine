**Corrections first.** A closed circumferential cylinder with open ends has Euler characteristic zero and two boundary loops. It is an annulus, not a disk. A disk-only LSCM pipeline must either make one longitudinal cut, which yields an exactly isometric rectangle, or emit at least two disk charts. Five uncut cube faces are an open box. Its four bottom corners each have three faces meeting with angle sum 270 degrees, so each carries a 90 degree deficit. Total interior curvature is 2 pi. No single isometric chart exists, so the correct expectation is a cross-shaped net with cuts or several charts, not one flat chart.

**xatlas annulus chart.** Do not disqualify it. Classify the chart by Euler characteristic and boundary loop count. If it is genus zero with two loops, check UV injectivity separately: all signed UV triangle areas share one sign and no overlap exists. If injective, it is a valid chart for texturing and its distortion counts. Report whole-mesh frog xatlas distortion with that chart included, and note that your method is restricted to disks by choice, which is a limitation of yours rather than a defect of theirs.

**Smallest useful next iteration.** I would not start with the vectorized disk test. It changes no output and does not test the hypothesis. Do these three cheap things in one pass, with parameters frozen:

1. **Profile the merge phase** by attributing time to disk audit versus union solves versus bookkeeping. Nearly half of union attempts were rejected on distortion after a full solve. If solves dominate, the win is a cheap pre-solve rejection filter, such as integrated absolute angle deficit inside the union relative to its area, not a faster disk test. If the audit dominates, vectorize it and verify identical accept and reject decisions on both meshes.
2. **Run the held-out set** with the frog-tuned settings untouched: bunny, fandisk, cylinder, sphere, synthetic fold, plus the uniform 4x subdivision of frog and sculpt. Record chart count, max and mean stretch, seam over root area, and solve count.
3. **Sweep the max stretch bound** at three values, one matching the xatlas sculpt maximum near 1.25. A single operating point cannot support any Pareto claim. Sculpt at present is fewer charts with more stretch, which is a trade, not a win.

Defer the 24-seed cohort until the profile exists. Fewer seeds produce larger initial patches, which fail the disk or distortion test more often and trigger more splits, so lower merge work is not guaranteed.

**Metric and control risks.**

- **Metric eigenvalues are sign-blind.** The 2x2 first fundamental form of a folded triangle is identical to its unfolded twin. Validity must include a signed area check per chart. State explicitly that "all maps valid" includes this.
- **Area normalization scope.** If stretch is normalized per chart, texel density differences between charts vanish from the metric. Report the ratio of largest to smallest chart scale factor.
- **Seam measurement source.** Measure seam length from mesh edges whose two faces land in different charts, for both methods. Do not measure UV boundary length, which packing and padding alter.
- **Feature control is within noise on frog.** Blind gave four more charts, slightly lower mean stretch, and near-identical seam. The feature term currently buys chart count, not distortion. Two meshes cannot separate that from luck. The growth-only ablation is worth running once the held-out set exists, not before.
- **Timing.** Keep reporting solve count and total degrees of freedom. Python against Debug Clang is not a comparison.
- **Packing.** Report atlas utilization as a separate line with an explicit "not comparable" note.

**Genuinely good result.** The bound holds on every held-out mesh with frozen settings. Cylinder yields one cut or two charts, sphere yields at least two, and the fold is not split across a smooth flank. Chart count and seam over root area on the 4x subdivided meshes stay within a few percent of the originals, showing resolution invariance of the same PL geometry. Solve count grows roughly linearly with face count. At a matched max stretch, chart count and seam sit within about 1.5x of xatlas.

**Premature stopping** looks like declaring a win from two meshes at one bound, with a feature effect inside noise, no fold check, no baseline distortion on frog, and a packer that cannot be compared.
