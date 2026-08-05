# LOP and WLOP point-cloud consolidation

## Citations

- **LOP:** Yaron Lipman, Daniel Cohen-Or, David Levin, and Hillel Tal-Ezer,
  “Parameterization-free Projection for Geometry Reconstruction,” ACM TOG
  26(3), SIGGRAPH 2007.
  <https://www.cs.tau.ac.il/~dcor/articles/2007/Parameterization-free-Projection.pdf>
- **WLOP:** Hui Huang, Dan Li, Hao Zhang, Uri Ascher, and Daniel Cohen-Or,
  “Consolidation of Unorganized Point Clouds for Surface Reconstruction,” ACM
  TOG 28(5), SIGGRAPH Asia 2009, DOI 10.1145/1618452.1618522.
  <https://www.cs.ubc.ca/~ascher/papers/hlzac.pdf>

## Method contract

Given finite source samples `P = {p_j}` and projected samples `X = {x_i}`, one
fixed-point iteration combines an attraction average and a repulsion offset:

```text
x_i' = sum_j(p_j alpha_ij) / sum_j(alpha_ij)
     + mu sum_{i' != i}((x_i - x_i') beta_ii') / sum_{i' != i}(beta_ii')
alpha_ij = theta(||x_i-p_j||) / max(||x_i-p_j||, epsilon) * v_j
beta_ii' = theta(||x_i-x_i'||) |eta'(...)|
           / max(||x_i-x_i'||, epsilon) * w_i'
```

The compact radial kernel is
`theta(r) = exp(-r^2 / (h/4)^2)` for `r < h`, zero otherwise. This reference
uses the stable WLOP repulsion convention `eta(r) = -r`, so `|eta'| = 1`, and
constrains `mu` to `[0, 0.5)`. The shared weight and density calculations come
from `Geometry.PointCloud.Kernels`.

Plain LOP sets `v_j = w_i = 1`. WLOP uses reciprocal source density for `v_j`
and direct projected density for `w_i`, both evaluated with the same compact
kernel. In the paper's notation both densities include their leading base term,
`1 + sum`; an isolated sample therefore has direct density `1` and reciprocal
density `1`, rather than an empty-neighborhood failure. Initial projected
samples come from the engine's seeded random
subsample, followed by the paper's theta-weighted L2 initializer. Fixed input,
parameters, and seed therefore produce a bitwise-identical serial result.

All coordinates and `h` are world-unit values. Stopping uses maximum per-point
world-unit displacement. To give coincident samples a finite scale-relative
interpretation, inverse-distance terms clamp distance to `epsilon = 0.01 h`.

## Inputs, outputs, and diagnostics

- Input: at least two finite positions, a typed LOP/WLOP strategy, positive
  finite `h`, valid `mu`, positive iteration limit, non-negative finite
  tolerance, optional downsample target, seed, and an optional input-count
  resource guard.
- Output: projected positions plus strategy/backend identity, convergence,
  iteration count, average/maximum displacement, and attraction, repulsion,
  density, and empty-neighborhood contribution counts.
- A hard failure publishes no positions. `NotConverged` deliberately retains
  the last finite iterate for preview and diagnosis.

## Degenerate and failure behavior

Empty/one-point clouds, invalid or garbage cloud storage, non-finite positions,
invalid controls, requests above the caller's input-count resource guard,
failed spatial indexing/querying, empty compact-support attraction
neighborhoods, density failures, and non-finite arithmetic return explicit
statuses. A missing attraction denominator remains an `EmptyNeighborhood`
failure, while zero non-self contributions to a WLOP density is the valid
paper-defined base case. Coincident samples remain finite. The routine never
mutates input.

## Provenance and translation notes

The implementation was derived from the two papers above. The task intake also
identified framework24's untested
`lib_bcg_framework/include/bcg_locally_optimal_projection.h` as comparison
material; no framework24 code was copied and it is not a build dependency.
The source revision used for that comparison was the repository state available
at task intake; the papers remain the normative algorithm source.

## METHOD-019 optimization review

The original LOP and WLOP papers restrict attraction and repulsion to local
support. METHOD-019 therefore treats sorted KD-tree neighborhood reuse as an
exact execution change: it retains point-index accumulation order, the same
theta/density/repulsion kernels, and the same seeded iterates. Huang et al.'s
density estimate and projected repulsion consume the same local projected
set, so caching that exact set is permitted; changing its support or weights
is not.

Liao, Xiao, and Jin's FLOP/KLOP work
(DOI [10.2312/EG2011/short/013-016](https://doi.org/10.2312/EG2011/short.013-016)
and [10.1016/j.cad.2013.02.003](https://doi.org/10.1016/j.cad.2013.02.003))
adds bilateral feature weighting and stochastic KDE sampling. Stotko,
Weinmann, and Klein's 2024 incomplete-gamma formulation changes WLOP density
and CLOP approximation accuracy. Both are useful named extensions, but neither
is an optimized implementation of the frozen LOP/WLOP oracle. The evaluated
candidate was consequently limited to exact neighborhood/scratch reuse under
the preregistered comparison in
[`reports/METHOD-019-protocol.md`](reports/METHOD-019-protocol.md); it preserved
parity but missed the fixed acceleration gate and was not adopted.
