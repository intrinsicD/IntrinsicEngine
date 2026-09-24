# Final review: certified cell specialization for HKTex

I accept all four corrections for the record: the Gaussian is applied inside the power, frozen rest-coordinate appearance can survive shape edits, ties obey the colour tolerance, and the exact-ranking floor is reducible.

The first correction has a consequence for bounds. Since x = (H·G)^a, negative ringing lobes of H are affected by the power. If the code's pow maps a negative base to a positive value for an integer-valued a (a=30), ringing becomes positive bumps. Bounds must then use |H·G|^a, not the positive part. Confirm this in the code.

## 1. Base-colour certificate: correct, and it resolves the sparse-flat case

**Proof.** Over any choice of outer-KNN and top-α sets:
- |N_ch| ≤ the sum of the largest 30 values of B_s,ch = sup_T |α_s|·|c_s,ch|, taken over the certified candidate union from the culling proof.
- The denominator is at least 1.
- clamp is 1-Lipschitz per channel.

So |C_ch − clamp(c_base,ch)| ≤ ΣB. Use per-channel |c_s,ch| rather than ‖c_s‖, which is tighter.

**Computing sup|α|.**
- α is monotone in x with α(0) = 0, so sup|α| = max(|α(x_lo)|, |α(x_hi)|).
- An upper bound on x is (max over corners of |H| · exp(−l_s/2σ²))^a, under the domain rule above.
- Pad with outward rounding. If the code produces NaN (from pow's domain) anywhere on T, the cell is uncertifiable, and that is a reference bug worth reporting.

**Why this fits sparse-flat regions.** The 50 nearest kernels there are far away, so each B_s is tiny, and those cells become constants with no KNN, exp, or sigmoid at runtime. The floor I raised earlier disappears exactly where it was worst.

**Failure regimes.**
- **Sparse but not flat.** Broad, low-ς kernels create smooth gradients. ΣB stays large, and the constant fails. These need a polynomial P fitted under affine arithmetic or Taylor models. Naive intervals suffer from dependency: N and Σα are correlated, which inflates the width.
- **Large cells near a strong kernel** force deep subdivision.
- **Tolerance space.** ε must be stated in the space C is defined in, which is the code's output space, not a perceptual one.
- **Selection jumps.** A jump larger than 2ε cannot be covered by any cell that spans it. Split the cell, or leave it analytic.

## 2. Exact cells: reducing the floor

- **Fixed outer set.** Since q_s − q_r is affine per face, N₅₀ is fixed on T if every pair (s ∈ N, r ∉ N) has the same sign at T's three corners. No ranking is then needed at runtime.
- **Fixed top-α set.** α is not affine, so use intervals: if the lower bound of the 30th kernel exceeds the upper bound of the 31st, the selection is fixed.
- **Dropping kernels.** Kernels whose B_s fits the remaining ε budget can be moved into the error term.
- **Donors:**
  - Keeter, *Massively Parallel Rendering of Complex Closed-Form Implicit Surfaces* (SIGGRAPH 2020): per-region interval evaluation that prunes min/max branches and emits a specialized tape per tile. This maps almost one-to-one onto the plan.
  - Duff 1992 and Snyder 1992, interval analysis with recursive subdivision.
  - Affine arithmetic: Comba & Stolfi 1993.
  - Taylor models: Makino & Berz 2003 (verify).
  - Futamura's partial evaluation as the conceptual frame.

## 3. Filtering path

Your inequality holds: |∫w(P − C)| ≤ ε for any normalized, nonnegative w. It holds only if P is integrated over every intersected cell and face, not sampled at the centre.

Two refinements:
- **Coarse nodes need integrals, not sup-approximants.** Store certified moments ∫C·(low-order basis) per node, computed by interval quadrature. These exist even over top-K discontinuities and hot detail, so sup certification is needed only where a footprint *partially* covers a node.
- **Filter-mismatch term.** Both filters are normalized, so ∫(w_F − w_G)C = ∫(w_F − w_G)(C − ½). The mismatch therefore costs at most ½‖w_F − w_G‖₁. For discontinuities, add an area-error term: jump × footprint mass on the uncertified set.

**Structure:** exact analytic evaluation in fine mode, and coarse-only derived moment caches. This path is principled.

**Unknowns:**
- storage for moments, trees, and residual analytic candidate lists, which must be charged to bytes;
- traversal of footprints that span many faces, which needs a cross-face cluster hierarchy that per-face trees don't provide;
- compile cost of certified quadrature in hot regions;
- temporal stability at the analytic/cached switch.

## Recommendation

**Adopt as the plan of record, not yet as the default:**
- the exact reference compiler as the oracle;
- production tiers of certified constant/polynomial cells, specialized exact cells, and a coarse moment hierarchy.

**Gates:**
- the fraction of surface area certified on engine assets, and total bytes against vertex attributes and texel baselines;
- zero sup violations against dense oracle sampling;
- filtering error against the supersampled oracle.
