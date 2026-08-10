# Spatially Regularized Signed-Curvature Mesh Segmentation

METHOD-038's feature-aligned follow-up intake, selected source equations,
physical-scale convention, remeshing metrics, and candidate killing order are
recorded separately in
[`feature_aligned_intake.md`](feature_aligned_intake.md). That Slice A record
does not change the frozen METHOD-037 formulation below.

## Published lineage

The closest direct precedent is Lavoué, Dupont, and Baskurt's curvature-tensor
CAD-mesh segmentation: it quantizes curvature descriptors and grows spatially
coherent surface regions. The IntrinsicEngine method uses that same broad
geometric signal but is a new repository formulation, not a reproduction of
their classifier or region-growing equations.

- Guillaume Lavoué, Florent Dupont, and Atilla Baskurt, “A New CAD Mesh
  Segmentation Method, Based on Curvature Tensor Analysis,” *Computer-Aided
  Design* 37(10), 2005. DOI:
  [10.1016/j.cad.2004.09.001](https://doi.org/10.1016/j.cad.2004.09.001).
- David Cohen-Steiner, Pierre Alliez, and Mathieu Desbrun, “Variational Shape
  Approximation,” *ACM Transactions on Graphics* 23(3), 2004. DOI:
  [10.1145/1015706.1015817](https://doi.org/10.1145/1015706.1015817).
- Ariel Shamir, “A Survey on Mesh Segmentation Techniques,” *Computer Graphics
  Forum* 27(6), 2008. DOI:
  [10.1111/j.1467-8659.2007.01103.x](https://doi.org/10.1111/j.1467-8659.2007.01103.x).
- Arthur Dempster, Nan Laird, and Donald Rubin, “Maximum Likelihood from
  Incomplete Data via the EM Algorithm,” *Journal of the Royal Statistical
  Society B* 39(1), 1977. DOI:
  [10.1111/j.2517-6161.1977.tb01600.x](https://doi.org/10.1111/j.2517-6161.1977.tb01600.x).
- Gideon Schwarz, “Estimating the Dimension of a Model,” *The Annals of
  Statistics* 6(2), 1978. DOI:
  [10.1214/aos/1176344136](https://doi.org/10.1214/aos/1176344136).
- Julian Besag, “On the Statistical Analysis of Dirty Pictures,” *Journal of
  the Royal Statistical Society B* 48(3), 1986. DOI:
  [10.1111/j.2517-6161.1986.tb01412.x](https://doi.org/10.1111/j.2517-6161.1986.tb01412.x).

These sources motivate, respectively, curvature-based surface decomposition,
piecewise-simple surface objectives, mesh-segmentation design choices,
Gaussian-mixture fitting, bounded model selection, and local optimization of a
spatial labeling energy. None defines the exact combined objective below.

## Frozen repository formulation

For each live triangle face `f`, average the signed per-vertex principal
curvatures to obtain `c_f = (k1_f, k2_f)`. Each channel is independently
centered by its median and divided by `1.4826 MAD`; an RMS scale is used when
the MAD collapses, and unit scale is the final constant-channel fallback. Let
the normalized descriptor be `x_f`.

The existing deterministic `Geometry.GaussianMixture::FitEM` fits full
covariance Gaussian components to `(x_f.x, x_f.y, 0)`. The zero third coordinate
is solely an adapter to the existing 3D GMM carrier and is stabilized by its
configured covariance floor. It is not a spatial coordinate. If `r_fc` is the
posterior responsibility of component `c` for face `f`, the unary cost is

```text
D_f(c) = -log(max(r_fc, 1e-15)) - min_j[-log(max(r_fj, 1e-15))].
```

For adjacent faces `f` and `g`, define

```text
q_fg = ||x_f - x_g||^2
       + (acos(clamp(n_f dot n_g, -1, 1)) / pi)^2,
w_fg = max(1e-9, exp(-feature_sensitivity * q_fg)).
```

The label energy is the contrast-sensitive Potts objective

```text
E(l) = sum_f D_f(l_f)
       + spatial_weight * sum_(f,g) w_fg [l_f != l_g].
```

Thus smooth neighboring faces pay the full disagreement cost, while a signed
curvature jump or normal fold makes a cut cheaper. Statistical similarity is
computed only from signed curvature; topology enters only through this dual
graph.

The CPU reference initializes labels from minimum unary cost and applies
deterministic ICM sweeps, alternating forward and reverse face order. Each
update tests all fitted component labels and accepts a strictly lower local
energy, with the lowest numeric label breaking ties. It stops after a sweep
with no moves or the configured iteration bound. Connected components of equal
labels become separate, contiguous region IDs. Regions smaller than
`minimum_region_faces` are greedily relabeled to the adjacent component with
the smallest computed energy delta, then connectivity is rebuilt. An edge is a
published boundary exactly when its two incident live faces have different
final region IDs.

## Fixed and automatic component selection

Fixed mode fits exactly the requested feasible count `k`. Spatial optimization
may leave a fitted component with no winning face, so diagnostics distinguish
selected, active, and connected-region counts.

Automatic mode fits every feasible `k` in the configured inclusive range. For
each successful candidate it records likelihood, EM convergence, covariance
regularization, and the normalized RMS distance from each face to the mean of
its maximum-posterior component. If any candidate reaches the requested RMS fit
tolerance, selection is restricted to those candidates; otherwise all
successful candidates remain eligible and diagnostics report the fallback.
The selected candidate minimizes

```text
BIC_alpha(k) = -2 log L_k + alpha * (6k - 1) log n,
```

where `6k-1` is the parameter count of a full-covariance two-dimensional
mixture and `alpha` is the configured complexity weight. Smaller `k` wins exact
criterion ties. This is a bounded engineering selector, not evidence that BIC
recovers a perceptually or atlas-optimal number of regions.

## Inputs, outputs, and failure behavior

- Input must be a non-empty, non-submesh-view triangle mesh with finite
  positions, non-degenerate faces, and one finite signed `k1,k2` pair per
  stored vertex slot. `ComputeAndSegment` obtains those values from the existing
  geometry curvature estimator on a detached mesh snapshot.
- Output arrays are storage-slot aligned. Deleted face slots retain the invalid
  label and transparent color; deleted, boundary-of-mesh, and non-region-boundary
  edge slots retain zero and transparent color.
- Invalid counts/ranges/tolerances, non-finite data, unsupported faces, failed
  GMM fits, and failed posterior evaluation return an explicit status without a
  successful partition.
- Runtime publication preserves topology and unrelated properties. It writes
  semantic component, connected-region, boundary, and visualization properties
  through the existing undoable geometry-processing operation.
- Diagnostics include observational wall-clock stage timings and per-candidate
  GMM fit duration alongside deterministic iteration counts. These measurements
  do not participate in candidate selection, energy, labels, or parity.

## Complexity

For `n` faces, `m` interior dual edges, `k` components, `I_em` EM iterations,
and `I_s` ICM sweeps, fixed mode is dominated by the existing GMM fit plus
`O(I_s (n k + m))` spatial optimization and `O(n + m)` connected relabeling.
Automatic mode repeats the fit over its bounded component range. The simple
reference small-region cleanup deliberately favors auditability over asymptotic
optimality and can become superlinear when many tiny regions are repeatedly
rebuilt; large-mesh optimization is deferred until reference quality is
established.

## Parameter guidance and limitations

- Use Fixed mode when a downstream experiment needs a controlled hypothesis
  count. Use Automatic mode for exploratory inspection, and review every
  candidate diagnostic rather than treating the chosen count as ground truth.
- Increase `spatial_weight` to suppress isolated label changes. Increase
  `feature_sensitivity` to reduce smoothing across curvature jumps and folds.
  These controls interact; neither has a universal scale-independent value.
- Increase `minimum_region_faces` only when removing small connected islands is
  preferable to preserving them. Cleanup can erase genuine small features.
- Curvature signs require consistent orientation. Reversing every face changes
  the signed descriptors but should preserve the partition up to label
  permutation; locally inconsistent orientation can create artificial
  boundaries.
- Curvature estimates near open boundaries, sharp corners, irregular sampling,
  and noise can be biased. Robust normalization limits scale sensitivity but
  does not repair a poor estimator.
- ICM produces a deterministic local minimum only. Boundary output is an
  inspectable hypothesis and is not yet a UV seam, topology cut, or atlas chart
  guarantee. That evaluation and materialization are deferred to `GEOM-076`.
