**Bottom line:** run the oracle test first, because for the frog its outcome is analytically determined by one number, and that number decides whether the failure is objective or solver. Then build the concavity-harmonic comparator as a bounded engineering adaptation.

## (a) Oracle and seam metric

The 040 energy is monotone under refinement except along strong-F edges. Splitting any region adds one region and one seam, so the energy change is

```
dE = 0.04 * sum_seam (l/D) * (1 - 4F^3) + pi*0.04^2
```

The seam term is negative only where F > 4^(-1/3) = 0.630. A candidate seam of normalized length L in low-F territory costs 0.04L against a region cost of 0.00503. Therefore no candidate seam whose length-weighted mean of (1 - 4F^3) is positive can ever be preferred by 040, regardless of solver. Report that mean, and the fraction of seam length with F > 0.63, as the primary oracle output. Also report the F histogram along the seam rather than the mean alone, since a bimodal seam can hide a rewarded sub-arc.

Construction critiques:

- **Apply masks only to faces of the local-baseline largest region**, keep the tiny hard-edge regions verbatim, then recount connected components after intersection. A halfspace may slice one intended region into several pieces. Count each as a region and do not merge them silently.
- **Hard-constraint check is nearly free** since only four hard edges exist and the tiny regions are preserved, but verify explicitly that no hard edge has both faces under the same candidate label.
- **Seam approximation error** should compare the label seam to the exact triangle-wise intersection contour of the mask function. Report two numbers: seam length divided by contour length, and mean normal-plane distance of seam edge midpoints from the contour. A length ratio well above one indicates staircase inflation that penalizes the candidate under 040 for a reason unrelated to geometry.
- **Commit mask parameters** in a file with a hash before any energy is evaluated. Call them provisional coordinate masks, never ground truth.
- **Sliver control:** report any candidate piece under the pi*0.04^2 area threshold separately. Do not drop it before evaluation.

## (b) Smallest comparator

Two-seed harmonic field with geometric scoring qualifies as a known engineering adaptation. Harmonic fields on a dual graph, concavity-weighted conductance, and Cheeger-style cut scoring are standard building blocks. It becomes paper reproduction only if you claim parity with a published method's results. Do not claim that.

Specification:

- **Concavity:** for adjacent faces i, j with outward normals, c_ij = angle between normals with sign of dot(n_i, centroid_j - centroid_i). Positive means concave. Validate on a sphere, where every edge must be convex, and on the synthetic neck.
- **Conductance:** w_ij = (l_ij / d_ij) * max(exp(-k * max(0, c_ij)), 1e-3). Declare k = 4 in advance.
- **Field:** solve Lu = 0 with u_s = 1, u_t = 0 via scipy sparse Cholesky or CG.
- **Seeds:** farthest-point sampling on the dual graph with geodesic distances, six seeds, all fifteen pairs.
- **Scoring:** sweep thresholds at u quantiles. Score each cut with purely geometric weights, h = L_seam / sqrt(min(A_1, A_2)). Scoring with concavity weights would be circular.
- **Stop rule:** accept the best cut if h < h_max and both sides exceed a_min. Recurse on parts, depth at most four. Declare h_max = 1.6 and a_min = 0.02 of total area before touching the frog. A sphere's best cut has h = sqrt(2*pi) = 2.51, so this rejects convex bodies with margin.
- **Negative control:** sphere, ellipsoid with a convex decorative ridge, and the two-lobe family with neck ratio swept from 0.9 down to 0.2. The comparator must produce zero splits on the first two and a split that appears once and remains monotone in neck ratio on the third.

Do not build the SDF path now. It costs more code and its pose-contact failures would confound the first comparison.

## (c) Killer tests and false claims

1. **Monotonicity test:** on the frog, no candidate seam achieves negative mean weight. If confirmed, the 040 conflict is provable for those candidates. If a negative-weight seam exists that the solver missed, that is a solver bug and blocks everything else.
2. **Triangulation invariance:** three densities and two tessellations of the two-lobe mesh. Cut location must shift less than half the neck width and h must vary under five percent.
3. **Ridge indifference:** the convex ridge must not attract the harmonic cut, while 040 must still respond to it through F. This isolates why the objectives differ.

Subtle false claims to avoid: attributing a cut to geometry when the seed pair fixed it, treating largest-region percentage as quality, comparing post-cleanup 040 counts against raw comparator counts, and reading noisy thresholds on nearly constant fields as region structure.

## (d) Gate

Implement the comparator now, in parallel. Its interpretation is gated on test one: only a positive mean seam weight on committed candidates licenses the claim that 040's objective, not the solver, excludes those seams.
