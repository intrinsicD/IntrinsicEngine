# Continuous LOP paper intake

## Citation

- **Title:** Continuous Projection for Fast L1 Reconstruction
- **Authors:** Reinhold Preiner, Oliver Mattausch, Murat Arikan, Renato
  Pajarola, and Michael Wimmer
- **Venue / Year:** ACM TOG 33(4), SIGGRAPH 2014
- **DOI:** 10.1145/2601097.2601172
- **URL:** <https://www.cg.tuwien.ac.at/research/publications/2014/preiner2014clop/>

## Frozen claim boundary

This reference replaces the discrete WLOP attraction sum with the paper's
closed-form integral over a fitted Gaussian mixture. Work diagnostics count
mixture-component contributions; they are not a speed claim. The reference
uses the existing deterministic ordinary-EM implementation, not the paper's
hierarchical EM accelerator, and makes no scanner-data, runtime, optimized, or
GPU generalization.

## Literature lineage and extension review

The implementation was checked against the primary LOP-family lineage rather
than treating CLOP in isolation:

- Lipman et al., [Parameterization-Free Projection for Geometry
  Reconstruction](https://www.wisdom.weizmann.ac.il/~ylipman/lop/lop.htm)
  (SIGGRAPH 2007), introduced the normal-free localized L1 projection and
  repulsion structure. This remains the semantic base of the shared strategy.
- Huang et al., [Consolidation of Unorganized Point Clouds for Surface
  Reconstruction](https://www.cs.ubc.ca/~ascher/papers/hlzac.pdf) (SIGGRAPH
  Asia 2009), added WLOP density correction and a separate robust normal
  pipeline. METHOD-016 implements the density-corrected positional reference;
  METHOD-018, not CLOP, owns normal-aware behavior.
- Liao, Xiao, and Jin, [Efficient Feature-preserving Local Projection Operator
  for Geometry Reconstruction](https://diglib.eg.org/bitstream/handle/10.2312/EG2011.short.013-016/013-016.pdf)
  (Eurographics 2011), combined bilateral normal weighting with stochastic KDE
  sampling. Its directional weighting informs the METHOD-018 comparison, but
  its randomized approximation is excluded from this deterministic reference.
- Preiner et al. (2014), the governing CLOP paper above, replaced the discrete
  attraction sum with an analytic integral over a Gaussian mixture and used a
  hierarchical EM construction for acceleration. The reference adopts the
  analytic Gaussian-product equation while deliberately reusing the existing
  seeded ordinary `FitEM` seam.
- Stotko, Weinmann, and Klein, [Incomplete Gamma Kernels: Generalizing Locally
  Optimal Projection Operators](https://doi.org/10.1109/TPAMI.2024.3349967)
  (TPAMI 2024), relates LOP to mean shift and derives improved WLOP density
  weighting and a more accurate CLOP kernel approximation. Those are valuable
  follow-up variants, but substituting either would invalidate the frozen
  original-paper equation oracle; they require a separately named strategy and
  parity evidence.

This review fixes the adoption boundary: original CLOP equations are the
reference, deterministic existing mixture fitting is the repository-sized
substitution, and later accuracy/acceleration variants remain explicit future
work rather than hidden implementation drift.

## Mathematical formulation

The input density is a normalized Gaussian mixture
`f(x) = sum_s w_s N(x | mean_s, covariance_s)`. For a projected point `q`,
the L1 attraction weight `theta(||x-q||) / ||x-q||` is approximated by the
paper's three isotropic Gaussian terms:

```text
(a_k, sigma_k) = (11.453, 0.11772),
                 (29.886, 0.03287),
                 (97.761, 0.01010)
S_k = sigma_k^2 h^2 I
Lambda_sk = covariance_s + S_k
omega_sk = w_s a_k (sigma_k h)^3 |Lambda_sk|^-1/2
           exp(-0.5 (mean_s-q)^T Lambda_sk^-1 (mean_s-q))
mean_sk = q + S_k Lambda_sk^-1 (mean_s-q)
F1(q, M) = sum_sk omega_sk mean_sk / sum_sk omega_sk
```

The first L2 initialization uses the same Gaussian-product identity with the
exact radial theta covariance `S_theta = h^2 I / 32`. Subsequent iterations
combine `F1` with the existing LOP/WLOP repulsion term and shared
`Geometry.PointCloud.Kernels` radial/repulsion conventions. Covariances,
coordinates, `h`, displacement, and the covariance floor are in squared or
linear world units as appropriate; mixture weights are normalized by
`Geometry.GaussianMixture::FitEM`.

## Frozen controls and stopping

- `MixtureComponentCount` is positive and no greater than input count.
- `CovarianceFloor` is finite and positive in squared world units.
- EM uses seeded k-means++ initialization, a positive iteration cap, and a
  finite non-negative relative tolerance.
- Projection uses the shared positive `h`, `mu in [0, 0.5)`, deterministic
  subsampling, iteration cap, and maximum-displacement stopping rule.
- The three approximation coefficients above are constants from Figure 5 of
  the paper and are not tuned against the confirmation fixture.

## Inputs, outputs, and diagnostics

Input is a finite point set and typed `ClopStrategy`. Output is the same
pointer-free consolidation result used by LOP/WLOP, extended with mixture
component count, EM iterations/convergence, and analytic attraction
contribution count. Fixed input, parameters, and seed produce identical serial
output.

## Degenerate and failure behavior

Invalid component counts or EM controls fail before allocation-heavy method
work. A failed/non-finite mixture, non-positive determinant, non-finite
Gaussian product, empty analytic attraction, invalid shared controls, or
non-converged EM returns an explicit status with no projected positions.
Projection `NotConverged` retains the last finite iterate under the shared
contract.

## Translation notes

`Geometry.GaussianMixture::FitEM` owns all mixture fitting; CLOP adds no private
EM code or mixture service. Matrix-product assembly is file-local inside
`Geometry.PointCloud.Consolidation`. The reference evaluates components and
the three Gaussian terms in fixed order and remains serial.

## METHOD-019 optimization review

The original CLOP paper reports grid-based neighbor queries and also proposes
approximate repulsion shortcuts: a roughly half-support cutoff and reuse on
alternating iterations. Those shortcuts reduce work but change the evaluated
repulsion sequence, so METHOD-019 excludes them from reference parity. The
paper's hierarchical mixture construction is likewise excluded because this
repository's canonical mixture is the existing seeded ordinary-EM result.

The exact candidate instead caches each Gaussian-product covariance sum,
determinant, inverse, and scalar coefficient. A component may be omitted only
when a conservative covariance bound proves all three floating-point weights
underflow to zero; retained components and terms stay in their original index
order. The later incomplete-gamma estimator remains a separately named
accuracy variant. These choices and the negative-result rule are frozen in
[`METHOD-019-protocol.md`](../locally_optimal_projection/reports/METHOD-019-protocol.md).
