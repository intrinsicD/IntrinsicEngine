The proposal has the right scope. Two parts need replacing before it is a credible experiment: the objective degenerates, and single-face descent is the wrong solver for this objective.

**Objective degeneracy.** A strictly positive weighted seam length, minimized with chart count fixed, is minimized by shrinking one chart until the UV limits on its neighbor bite. Feature weights only bias where that shrinking stops. The result would be a distortion-driven border with feature terms as tie-breakers, and the ablation would not be able to tell those apart. Fix this by bounding where the seam may go rather than by adding area or turning terms. Restrict every move to a band of faces within graph distance k of the current shared seam of one chart pair. Faces outside the band keep their labels. The seam then cannot vanish, chart count stays fixed for free, and originals stay fixed because the band is clipped to the source region. Report k as a frozen parameter, and use the following weight per dual edge:

```
cost(e) = length(e) * (1 - beta * s(e)),   0 <= s(e) <= 1,  beta <= 0.9
```

The floor keeps noisy extrema from making a seam free, which answers the hard-lock concern directly.

**Solver.** With two labels and Potts costs, the band subproblem is a binary submodular labeling. A min cut solves it exactly in one shot. Source and sink terminals pin the band rim faces to their charts. This replaces best-first single-face descent and the pair/strip escalation with one bounded move type that cannot get stuck in single-face local minima. The scipy sparse maximum-flow routine is offline and needs integer capacities, so scale costs and round. After the cut, reject the candidate if either chart is disconnected, then run the existing open_region and parameterize_local admissibility on both charts. That reuses the current topology, orientation, simple-boundary and stretch 1.35/anisotropy 2 checks unchanged. Count internal cuts from open_region in the verified delta only, not in the min-cut cost. The min cut is the cheap discrete delta the proposal asked for. Accept only if the verified weighted seam cost of the pair, borders plus internal cuts, decreases.

**Schedule.** Iterate pairs sorted by shared seam length descending, one pass per round, at most three rounds, stop when no pair changes. Cache rejections keyed by both chart revision numbers as proposed. Pair count is in the tens, so the LSCM cost is bounded without further tricks.

**Evidence weights.** Curves are already face-bound in the trace data, so there is no need for a Euclidean projection and no risk of pulling evidence from a nearby disconnected sheet. Score an edge by the best curve segment within graph distance r of its two faces, using confidence times absolute cosine between segment tangent and edge direction, with linear falloff over r. Tie r to the recorded support scale, medium for frog and large for sculpt, and state that this choice came from the audit's display settings and is not tuned. Native soft confidence is a separate score on the same edges. Combine by maximum, not sum, so the two sources stay separable for the ablation.

**Frozen tests and ablation.** One experiment, five arms, one synthetic control:

- Protected atlas unchanged, as reference.
- Band min-cut with beta zero. This measures how much change comes from seam shortening alone.
- Native soft evidence only.
- Native soft plus curve score.
- Same as the previous arm with edge scores randomly permuted under a fixed seed. Alignment must not improve here, or the metric is measuring smoothing.
- Analytic control on a cylinder with a crease ring. Start with a seam offset from the crease by fewer than k faces and confirm it snaps to the crease, and stays put when beta is zero.

Report per arm: weighted and unweighted seam length, seam edges carrying nonzero soft evidence gained and lost against the protected atlas, mean curve alignment at the matched scale, max stretch and anisotropy, chart count, lost baseline edges, and packing utilization measured afterward. Acceptance is zero lost baseline edges, all charts admissible, and weighted seam cost non-increasing per accepted move. Do not constrain unweighted length. Let the table show it.

**Expected limits to state up front.** On frog the added borders are bisection lines that started as UV repairs, and most of the 64 removed soft-evidence edges lie away from any current seam. A band move can only reach features within k faces, so frog gains will be local, not a restoration. The merge priority remains feature-agnostic and that is out of scope for this turn. On sculpt there are few pairs and no merges, so movement will be small, which is the intended floor. Turning terms stay out unless the beta-zero arm shows the cuts zigzag despite length minimization.

That is the whole change: keep the immutable regions, fixed count and UV limits; replace the objective with a band-bounded weighted seam cost; replace descent with pairwise band min-cut plus the existing verification.
