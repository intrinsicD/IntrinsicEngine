**Review of `neck_sweep.py` and the frozen v4 results.** No tools used. I accept the three corrections: the rho 0.9 second derivative is 2π²(0.1)−0.9 ≈ 1.07 > 0, so it is a mild saddle waist, not convex; the √2 anisotropy bound has no basis on arbitrary face-dual meshes; and the shoulder-based prominence denominator is what the code does, so the earlier objection is withdrawn.

**Blockers (mathematical, within the diagnostic's own scope)**

1. **Negative edge costs are present in the reported v3 split.** The waist circumference at rho 0.3 is about 1.885 in mesh units, and no cut on that shape is shorter. The reported delta of −2.219 equals cut cost plus a positive region cost, so the accepted cut had negative total cost. The energy is therefore not a positive-weight cut functional in the regime that produced the headline result. Any cut passing through the negative band lowers energy, so "delta < 0" is not evidence of a minimal cut against concavity. Check every cell by computing 1 + concavity_weight × radius × minimum_raw_kappa_times_D. Where it is negative, the equatorial acceptance should be reported as a sign-flipped band effect. A per-edge floor such as a small positive multiple of length would make the functional well-posed, but that is a diagnostic-side change, not an engine change.

2. **The energy is not scale-invariant.** Curvature is normalized by the bounding diagonal, but edge length in the cut term is not. The same config therefore means different things on the unit-box synthetic shapes and on the frog. Cross-mesh comparisons of energies and of the region-cost trade-off are not valid until length is divided by D or region_cost is stated in mesh units per mesh.

3. **The profile minimum at fraction 0.5 is partly an artifact.** The score normalizes by sqrt(min(area, total − area)), which has a kink at one half. On a sphere the score decreases monotonically toward the equator, so a local minimum at 0.5 exists for any convex shape and its detection depends only on the prominence threshold. The synthetic necks are symmetric, so their true waist coincides with this kink. The claim that v4 "finds the exact equator" is confounded and does not demonstrate proposal robustness on an asymmetric neck such as the frog.

**Why sampling stability failed, mechanically**

The edge curvature is a finite difference of normals projected onto the centroid-to-centroid direction. On the default triangulation that direction is oblique in the u-theta chart, so each edge samples a normal curvature that mixes the negative meridional value with the positive azimuthal value near 1/rho. Flipping the diagonal changes the mix, which matches the [2,2,1,1,1] and [1,1,2,2,1] patterns. The v2 directional weighting cannot fix this because it weights other equally oblique samples. The v1 face mean averages both principal directions, so a neck with mean curvature positive is invisible by construction, which explains the rho 0.3 failure. Recovering the minimum principal curvature needs a per-face or per-vertex shape-operator fit, which is a diagnostic-side estimator change.

**Regional meaning of the signed edge energy**

The signed edge cost by itself has no regional meaning. It is a local normal-curvature sample times length, with the smoothing radius as the only scale. A thin wrinkle and a true neck produce the same per-edge negativity. Part-ness requires a thickness or enclosure notion, which is exactly what the unimplemented SDF thickness comparator would supply. The regional-only ablation producing 10, 13, and 2 regions on neck, groove, and ridge is consistent with the profile score carrying no concavity information either. Neither term alone encodes a part.

**Evidence overclaims to avoid**

- "Feasible but worse energy" on the frog oracle is descriptive only, under a functional with the scaling issue above.
- The v3 equatorial split is not a detector success given blocker 1.
- Frozen v4 remaining single-region on convex, belt, and groove shapes is a true negative, but it is threshold-dependent given blocker 3.

**Bounded conclusion**

The diagnostic is internally consistent as code: hard constraints are enforced, new labels are counted exactly, original labels are never merged, and the replay delta of 9e-16 confirms the baseline path. Its results do not support any regional-gate benefit, and sampling stability is refuted for the current edge estimator. The frozen frog run is descriptive and nothing is portable to C++, runtime, or UI. Next diagnostic-side work, in priority order: floor or otherwise bound the edge cost, normalize cut length by D, replace the oblique dual-edge estimator with a fitted shape operator, and use an asymmetric neck to test proposals away from the 0.5 kink. The SDF thickness comparator remains the only planned independent measure of part-ness.
