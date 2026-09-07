Corrected verdict: the energy is well-formed as specified. It is scale and translation invariant, bounded below, and rotation dependent by documented design. The remaining open question is whether the proposal generator or the energy explains the sampling failure, and the frozen v4 candidate does not solve the frog case.

Withdrawn items:

- **Scaling blocker withdrawn.** I assessed the cost without the graph assembly source. Edge lengths, areas, and half-lengths are all pre-divided by the diagonal, so the curvature term is dimensionless and the tested invariance holds.
- **"Must floor" withdrawn.** Signed refinement costs are the mechanism that produces the intended contrast. With finite partitions, a fixed positive beta, hard constraints, and a minimum child area, the energy is bounded below. Negative values are not evidence of ill-posedness, and no global-minimum or min-cut claim is being made.
- **Sampling mechanism withdrawn.** Flipping the diagonal leaves the coarse-2 and dense-1 results unchanged, so mixed flips do not explain the failure. Oblique normal curvature, geodesic-ball misalignment, and quantile aliasing all remain live hypotheses.

Retained criticisms:

- **Rotation dependence is a real limitation.** My test found it, and the attribution to the axis-aligned bounding box diagonal in the length term is consistent with the reported delta. The curvature-integral term cancels the diagonal, so the length term alone drives the change. Documenting rather than claiming invariance is the correct posture. Seed tie-breaking can also change under rotation, so no algorithmic invariance claim should appear anywhere.
- **Signed boundary curvature is not semantic.** It rewards concave creases regardless of whether they separate a part. That is why the concavity-aware proposal alone did not find a neck on the frog.
- **The minimum-area kink near 0.5 is an unresolved confound.** No asymmetric control was run, so the sampling results support no robustness claim.

Frog status: v4 accepts zero splits and preserves the five METHOD-039 regions with the main region near 99.73 percent of area. This is not a solution and shows no relative gate benefit.

Cheapest discriminating control next: evaluate the energy of the coordinate-equator partition and the best partition over all quantiles on the synthetic neck, without running the proposal search. If the neck partition scores lower than the accepted result, the proposal generator is the obstacle. If it does not, the energy itself is the obstacle. This is a fixed-label evaluation only, so it needs no new machinery.

Separate obstacle to record: the concavity-aware proposal failed on its own. An inside-triangle curve proposal and a thickness comparator are unimplemented follow-ups. A signed distance field would supply thickness information but does not guarantee part segmentation, and no such guarantee should be claimed.
