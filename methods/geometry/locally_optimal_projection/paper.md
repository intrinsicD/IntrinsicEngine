# Paper Intake — Locally Optimal Projection (LOP / WLOP)

> Produced by a multi-source deep-research pass (fan-out web search, source
> fetch, cross-source corroboration) over the primary papers, follow-up
> papers, and reference implementations; every equation below was confirmed
> against at least two independent sources. See the claims table at the end.

## Citation

Primary (default variant, WLOP):

- **Title:** Consolidation of Unorganized Point Clouds for Surface Reconstruction
- **Authors:** Hui Huang, Dan Li, Hao Zhang, Uri Ascher, Daniel Cohen-Or
- **Venue / Year:** ACM Transactions on Graphics 28(5) (SIGGRAPH Asia), 2009
- **DOI:** 10.1145/1618452.1618522
- **URL:** https://dl.acm.org/doi/10.1145/1618452.1618522

Origin (variant B, plain LOP):

- **Title:** Parameterization-free Projection for Geometry Reconstruction
- **Authors:** Yaron Lipman, Daniel Cohen-Or, David Levin, Hillel Tal-Ezer
- **Venue / Year:** ACM Transactions on Graphics 26(3) (SIGGRAPH), 2007
- **DOI:** 10.1145/1276377.1276405
- **URL:** https://dl.acm.org/doi/10.1145/1276377.1276405

Secondary (limitations and restatements only): EAR (Huang et al., ACM TOG
32(1), 2013 — edge-aware resampling, out of scope), CLOP (Preiner et al.,
ACM TOG 33(4), 2014 — continuous reformulation whose Sec. 3 restates the
discrete LOP update used here).

## Core claim

Given an unorganized, noisy, outlier-ridden point set `P = {p_j}` (no
normals, no connectivity), a projected point set `X = {x_i}` with
`n = |X| << m = |P|` converges under a fixed-point iteration to a
consolidated set that (a) lies close to the underlying surface via a
localized L1 median (robust to noise and outliers) and (b) is evenly
distributed via a repulsion term. WLOP adds input- and projected-set
density weights so highly non-uniform input no longer biases the result,
and replaces LOP's fast-decaying repulsion function with a linear one for
smoother convergence.

## Mathematical formulation

Localization kernel (both papers, Eq. confirmed by CLOP Sec. 3 and an
independent MATLAB implementation):

    theta(r) = exp(-r^2 / (h/4)^2)

with support radius `h`; at `r = h` the weight is `exp(-16)`, so
neighborhoods are truncated at radius `h`.

WLOP fixed-point update for projected point `x_i` at iteration `k`:

    x_i^{k+1} =   sum_j p_j (alpha_ij / v_j) / sum_j (alpha_ij / v_j)
                + mu * sum_{i' != i} delta_ii' (w_i' beta_ii') / sum_{i' != i} (w_i' beta_ii')

with:

- attraction weights `alpha_ij = theta(||x_i^k - p_j||) / ||x_i^k - p_j||`
  (the localized L1-median / Weiszfeld weight);
- repulsion direction `delta_ii' = x_i^k - x_{i'}^k` and weights
  `beta_ii' = theta(||delta_ii'||) |eta'(||delta_ii'||)| / ||delta_ii'||`;
- **eta functions:** Lipman's original LOP uses `eta(r) = 1/(3 r^3)`
  (decays too fast at large r: oscillation near a solution, irregular
  distributions when n << m). WLOP replaces it with `eta(r) = -r`, i.e.
  `|eta'| = 1` and `beta = theta(r)/r`. This attribution is stated by the
  WLOP paper itself and independently by CLOP and EAR.
- WLOP density weights: input density `v_j = 1 + sum_{j' != j}
  theta(||p_j - p_j'||)` computed once (divides the attraction weight, so
  dense input regions attract less), and projected-set density
  `w_i = 1 + sum_{i' != i} theta(||x_i - x_i'||)` recomputed every
  iteration (multiplies the repulsion weight, so crowded projected regions
  repel more). Plain LOP is exactly this update with unit density weights.

First iteration (LOP Sec. 3, restated verbatim by CLOP): the initial step
is a plain theta-weighted local mean — **no** `1/r` factor and **no**
repulsion:

    x_i^{1} = sum_j p_j theta(||p_j - x_i^0||) / sum_j theta(||p_j - x_i^0||)

The Weiszfeld weights are singular where the projected set coincides with
input points (the usual initialization), so the L2 initializer both
regularizes the start and performs the bulk of the denoising.

Repulsion weight constraint: `mu in [0, 0.5)` — required for the paper's
convergence argument (strict diagonal dominance of the iteration matrix,
`||A^-1||_inf <= 1/(1 - 2 mu)`).

## Parameters and defaults (verified guidance)

| Parameter | Paper default | Reference-implementation practice |
| --- | --- | --- |
| Support radius `h` | `h = 4 sqrt(d_bb / m)` (WLOP paper; `d_bb` = bounding-box diagonal, `m` = input count) | CGAL `neighbor_radius`: 8x average spacing; "at least two rings of neighbors"; too small fails to regularize, too large shrinks into the surface interior |
| Repulsion `mu` | 0.45 in all WLOP experiments; LOP recommends 0.1-0.25 for accuracy, 0.3-0.45 for regular distribution | CGAL hard-codes 0.45 (not user-facing) |
| Iterations | ~100 (WLOP experiments); 10-50 typical in follow-ups | CGAL default 35, fixed count, no termination test |
| Target size | random subsample of the input (e.g. 5% in the WLOP Lena experiment) | CGAL `select_percentage` default 5% |
| Initialization | random subset of the input | CGAL: shuffle + take first k |
| Convergence monitoring | mean per-point displacement between iterates | (same; no automatic stop) |

## Inputs and outputs

- Input: positions only (`std::span<const glm::vec3>` or a
  `Geometry::PointCloud::Cloud`); no normals, no connectivity, no units
  assumed beyond `h` being in the input's length units.
- Output: projected positions (`n = TargetCount` or `|InitialIndices|`),
  the seed indices actually used, and a convergence report (per-iteration
  mean/max displacement, empty-neighborhood diagnostics).

## Degenerate/edge cases

- Empty/too-small input, non-finite positions, non-positive or non-finite
  `h`, `mu` outside `[0, 0.5)`, zero iterations, invalid target count or
  duplicate/out-of-range seed indices: fail closed with explicit statuses.
- A projected point whose support ball contains no input points keeps its
  position for that term (counted in diagnostics).
- Coincident pairs (`r < 1e-12`) are skipped in the singular `1/r` kernels.
- Isolated outliers farther than `h` from the surface exert no attraction;
  a seed placed on one legitimately stays there (documented limitation).

## Implementation notes (deviations and observations)

- **Kernel fidelity:** this implementation keeps the papers'
  `theta(r) = exp(-16 r^2 / h^2)`. CGAL deviates: its code uses
  `exp(-4 r^2 / h^2)` (a kernel twice as wide relative to `h`), which is
  why CGAL's `8x average spacing` radius guidance corresponds to roughly
  the same effective averaging width as the paper kernel at `6-10x`.
- **Variant token:** `Variant::Lop` is "WLOP with unit density weights"
  (per the WLOP paper's own equivalence statement) and keeps
  `eta(r) = -r`; it is not Lipman's original `1/(3 r^3)` repulsion.
- **CGAL's density default:** CGAL applies the input-density weight `v_j`
  only when `require_uniform_sampling` is on (off by default) but always
  applies `w_i`; this implementation's `Variant::Wlop` applies both, as in
  the paper.
- **Accuracy/regularity trade-off (measured):** on the repo's noisy-plane
  fixture the equilibrium off-surface residual grows with `mu`
  (`0.0082` mean at `mu = 0.3` vs `0.0126` at `mu = 0.45`), matching the
  LOP paper's recommendation to use small `mu` for accuracy; repulsion
  acts in full 3D and re-inflates transverse noise as `mu -> 0.5`.
- **Determinism:** single-threaded reference; seeded partial Fisher-Yates
  initialization (restating `Geometry.PointCloud.Utils` `RandomSubsample`
  in the index domain); KD-tree radius queries return index-sorted
  results; double-precision accumulation.

## Claims table (equation/default -> sources)

| Claim | Sources |
| --- | --- |
| `theta(r) = exp(-r^2/(h/4)^2)` | LOP paper; WLOP paper; CLOP Sec. 3; independent MATLAB impl |
| WLOP update (attraction `alpha/v_j`, repulsion `mu * sum(delta w beta)/sum(w beta)`) | WLOP paper Eq. 6; CLOP restatement; MATLAB impl |
| `alpha = theta/r`, `beta = theta |eta'| / r` | LOP paper; WLOP paper; CLOP |
| `eta = 1/(3r^3)` is Lipman 2007; `eta = -r` is Huang 2009 | WLOP paper; CLOP; EAR |
| First iteration = plain theta-weighted mean (no 1/r, no repulsion) | LOP paper Sec. 3; CLOP restatement |
| `mu in [0, 0.5)`; diagonal-dominance convergence bound | LOP paper; WLOP paper; EAR |
| `mu`: 0.1-0.25 accuracy vs 0.3-0.45 regularity; 0.45 used in practice | LOP paper recommendation; WLOP/EAR/CGAL defaults |
| `v_j = 1 + sum theta` once; `w_i = 1 + sum theta` per iteration | WLOP paper; CGAL source; MATLAB impl |
| `h` default `4 sqrt(d_bb/m)`; CGAL 8x average spacing, two-rings guidance | WLOP paper; CGAL manual + source |
| CGAL kernel is `exp(-4 r^2/h^2)` (deviation from paper) | CGAL source inspection |
| Iterations: ~100 (paper), 35 (CGAL), 10-50 (CLOP-cited practice) | WLOP paper; CGAL; CLOP |
| LOP failure modes: oscillation; reproduces input non-uniformity | WLOP paper (Lena experiment) |
| Isotropic theta smears sharp features (motivates EAR; out of scope) | EAR paper |
