# Edge-Aware Point Set Resampling paper intake

## Citation

- **Title:** Edge-Aware Point Set Resampling
- **Authors:** Hui Huang, Shihao Wu, Minglun Gong, Daniel Cohen-Or, Uri
  Ascher, and Hao Zhang
- **Venue / Year:** ACM Transactions on Graphics 32(1), Article 9, 2013
- **DOI:** 10.1145/2421636.2421645
- **Original paper:** <https://doi.org/10.1145/2421636.2421645>
- **Author-listed paper/code entry:**
  <https://www.cs.sfu.ca/~haoz/papers_list.html>
- **Author-hosted manuscript:**
  <https://www.math.tau.ac.il/~dcor/articles/2012/Edge-Aware.pdf>

## Frozen claim boundary

This package defines a deterministic serial CPU reference for the original
two-phase EAR algorithm: resample away from features by alternating bilateral
normal refinement and anisotropic LOP, then progressively insert oriented
samples toward features with the paper's clearance, priority, bilateral
projection-distance, and normal-selection rules. It also exposes the first
phase as an anisotropic fixed-count WLOP mode.

The correctness claim is limited to checked-in analytic dihedral/plane
fixtures. It is not a global state-of-the-art, scanner-data, reconstruction,
runtime, optimized, GPU, or performance claim.

## Literature lineage and improvement review

- Lipman et al., [Parameterization-Free Projection for Geometry
  Reconstruction](https://www.wisdom.weizmann.ac.il/~ylipman/lop/lop.htm)
  (SIGGRAPH 2007), supplies the L1 attraction/repulsion form.
- Huang et al., [Consolidation of Unorganized Point Clouds for Surface
  Reconstruction](https://www.cs.ubc.ca/~ascher/papers/hlzac.pdf) (SIGGRAPH
  Asia 2009), adds WLOP density compensation and is the isotropic contrast
  oracle already implemented by METHOD-016.
- Huang et al., [Edge-Aware Point Set
  Resampling](https://doi.org/10.1145/2421636.2421645) (2013), is the governing
  reference. Its exact signed normal-similarity, tangent-plane attraction,
  alternating refinement, and progressive insertion equations are adopted.
- Cheng et al., [Efficient L0 Resampling of Point
  Sets](https://doi.org/10.1016/j.cagd.2019.101790) (CAGD 2019), targets severe
  noise/outliers with a different sparse objective plus half-sampling and
  interleaved regularization. It motivates explicit severe-noise limitations
  and a future comparator, but is not substituted into EAR.
- Qi, Hu, and Guo, [Feature Preserving and Uniformity-controllable Point Cloud
  Simplification on Graph](https://arxiv.org/abs/1812.11383) (2018/2019), makes
  the feature-versus-uniformity trade-off explicit. The reference therefore
  reports both edge retention and point-spacing uniformity; it does not adopt
  the graph-spectral optimizer.
- Yu et al., [EC-Net: an Edge-aware Point Set Consolidation
  Network](https://openaccess.thecvf.com/content_ECCV_2018/papers/Lequan_Yu_EC-Net_an_Edge-aware_ECCV_2018_paper.pdf)
  (ECCV 2018), learns point-to-surface and point-to-edge objectives from
  annotated meshes. Its training data/model dependency is outside a
  deterministic geometry reference.
- Lv, Lin, and Zhao, [Intrinsic and Isotropic Resampling for 3D Point
  Clouds](https://doi.org/10.1109/TPAMI.2022.3185644) (TPAMI 2022/2023), adds
  efficient intrinsic neighborhoods and globally/adaptively isotropic count
  control. It informs the uniformity/count diagnostics but remains a separate
  algorithm.
- Wei et al., [iPUNet: Iterative Cross Field Guided Point Cloud
  Upsampling](https://arxiv.org/abs/2310.09092) (2023), uses learned cross
  fields and local parameterized surfaces for arbitrary-ratio feature-aware
  upsampling. That is a useful later improvement axis, not part of this CPU
  oracle.

The adoption rule is therefore explicit: implement original EAR equations;
use later work to select adversarial cases and complementary metrics; keep
different optimization objectives or learned priors behind separately named
future methods.

## Mathematical formulation

Let `S = {(p_i,n_i)}` be oriented samples, with finite unit normals and
positive world-unit support `h`. Neighborhoods contain points with
`||p_i-p_j|| < h`. The original spatial and signed normal weights are

```text
theta(r) = exp(-r^2 / h^2)
psi(n_i,n_j) = exp(-((1-dot(n_i,n_j)) / (1-cos(sigma_n)))^2)
```

with `sigma_n = 15 degrees` by default. One bilateral refinement step is

```text
n_i' = normalize(sum_j theta(||p_i-p_j||) psi(n_i,n_j) n_j
                 / sum_j theta(||p_i-p_j||) psi(n_i,n_j)).
```

The anisotropic attraction used while resampling away from edges is

```text
phi(n_i,p_i-q_j) = exp(-(dot(n_i,p_i-q_j)^2) / h^2),
```

again restricted to the declared spatial neighborhood. The positional update
is the existing LOP/WLOP fixed-point expression with `phi` replacing the
isotropic attraction weight; source-density correction and shared repulsion
remain unchanged. Normal refinement and anisotropic projection alternate for
three rounds by default.

For progressive insertion, candidate base `b=(p_i+p_j)/2` uses orthogonal
clearance from an oriented sample `s_l`:

```text
D(b,s_l) = ||(b-p_l) - dot(n_l,b-p_l)n_l||
C(b) = min_{l in the local h-neighborhood} D(b,s_l)
P(s_i) = max_j (2-dot(n_i,n_j))^rho C((p_i+p_j)/2), rho=5.
```

The globally greatest priority wins; ties resolve by source indices. For the
winning endpoint normals, choose the one with smaller absolute bilateral
projection distance, refine it once with the frozen directional weights, and
compute

```text
d(b,n) = sum_l dot(n,b-p_l) theta(||b-p_l||) psi(n,n_l)
         / sum_l theta(||b-p_l||) psi(n,n_l)
p_new = b + d(b,n)n.
```

Insertion repeats until `TargetPointCount`. All sums and candidate scans use
fixed index order.

## Inputs, outputs, and normal policy

- `Cloud` input consumes a valid built-in `p:normal` property without mutation.
- A position-plus-normal span overload supplies authored normals without an ECS
  or property dependency.
- `AuthoredOrEstimate` invokes deterministic `Geometry.PointCloud.Normals`
  when authored normals are absent; `RequireAuthored` returns
  `NormalsRequired`.
- The result carries positions plus refined/inserted unit normals for
  anisotropic/EAR strategies. Isotropic LOP/WLOP/CLOP retain their existing
  position-only contract.

## Frozen controls and failure behavior

- `h > 0`, `0 <= mu < 0.5`, `0 < sigma_n < pi`, finite non-negative `rho`,
  positive refinement/iteration limits, and a target within the explicit
  output resource guard.
- Non-finite or degenerate authored normals fail closed; estimation failure,
  empty neighborhoods, zero normal/projection denominators, non-finite
  iterates, or an impossible insertion also return explicit status with no
  successful payload.
- EAR may upsample; the other reference strategies retain downsample-only
  target semantics.
- Inputs and authored normals are immutable. Fixed input, parameters, and seed
  produce bitwise-identical serial output.

## Original-paper limitations retained

The 2013 paper reports sensitivity to fixed `h`/`sigma_n`, rough open
boundaries, large holes, close sheets, severe noise/under-sampling, and bad
initial normal orientation; outputs may over-smooth or over-sharpen. These are
documented limitations and fail-closed boundaries, not hidden tuning claims.

## METHOD-019 optimization review

The original EAR construction is local: its refinement, clearance, bilateral
projection distance, and candidate-pair tests all use the declared support;
the paper additionally narrows inserted-normal selection to the winning
endpoints. METHOD-019 applies KD-tree bounds to those exact scans, sorts every
returned point set by source index, and preserves the global priority maximum
and original tie rule. It does not approximate, truncate, or incrementally
stale the priority computation.

Later L0, graph-uniformity, intrinsic/isotropic, EC-Net, and cross-field work
changes the objective, neighborhood meaning, or learned prior. Those papers
inform limitations and future comparisons, but not this candidate backend.
The exact parity and performance decision is preregistered in
[`METHOD-019-protocol.md`](../locally_optimal_projection/reports/METHOD-019-protocol.md).
