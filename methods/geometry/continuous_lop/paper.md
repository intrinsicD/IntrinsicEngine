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
