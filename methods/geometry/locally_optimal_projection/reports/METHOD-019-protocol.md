# METHOD-019 optimized-CPU preregistration

Frozen before optimized implementation. This record fixes the comparison
surface; a failed gate produces a reference-only strategy and negative result,
not a changed fixture or threshold.

## Literature-derived implementation boundary

The normative sources are Lipman et al. (LOP, 2007), Huang et al. (WLOP,
2009), Preiner et al. (CLOP, 2014), and Huang et al. (EAR, 2013), linked from
the three package `paper.md` records. They all formulate local-support work,
which permits exact spatial indexing without changing the objective.

The candidate may only change execution:

- reuse allocated KD-tree radius-query scratch while retaining sorted point
  indices and the reference accumulation order;
- reuse an iteration's exact projected-neighborhood lists for WLOP/EAR density
  and repulsion calculations;
- precompute CLOP Gaussian-product covariance sums, determinants, inverses,
  and coefficients, and prune a component only when a conservative bound
  proves every omitted floating-point contribution underflows to zero;
- restrict EAR pair, clearance, projection-distance, and normal-refinement
  scans to the same compact-support neighborhoods, sorted by source index,
  while preserving its global priority maximum and index tie-breaking.

The original CLOP paper's hierarchical EM, half-support repulsion cutoff, and
every-other-iteration repulsion reuse are excluded because they can change the
fitted mixture or iterate. FLOP/KLOP stochastic KDE resampling, the 2024
incomplete-gamma kernels, L0/graph objectives, intrinsic neighborhoods, and
learned edge priors are separately named algorithmic variants, not hidden
accelerations of these references.

## Frozen fixtures and matched budgets

The executable manifest
`benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml` is canonical.
Its built-in v1 suite contains four deterministic fixtures:

- LOP: a `32 x 32` plane on `[-1,1]^2` with
  `z = 0.018 sin(13x + 7y) + 0.006 cos(5x - 11y)`;
- WLOP: a `40 x 24` plane whose normalized x coordinate is warped by
  `u^1.7`, with the same deterministic height function, to exercise density
  correction;
- CLOP: a `30 x 24` uniform plane with the same deterministic height function
  and the manifest's fixed 24-component ordinary-EM controls;
- EAR: two seven-by-eight oriented dihedral panels at 60 degrees, with radial
  spacing `0.095`, crease spacing `0.125`, deterministic seven-level normal
  noise `0.0045 * code`, authored face normals, and eight insertions.

Reference and candidate receive identical positions, normals, strategy,
target, seed, iteration limits, tolerances, and resource guards. Timing covers
one complete synchronous consolidation call, including deterministic
initialization, index/model construction, and mixture fitting. The supported
CPU thread count is one; no scheduler or hidden parallel budget participates.

## Frozen correspondence, metrics, and decision rule

Output correspondence is the emitted vector index: both paths use the same
seeded initialization, fixed candidate ordering, global EAR priority rule, and
append order. State, output count, and normal-presence must match exactly.
For successful outputs, position RMS/L-infinity deltas must be at most
`1e-6`/`2e-6` world units; EAR normal RMS/L-infinity deltas use the same
bounds. Failure fixtures require the exact status and an empty payload.

After one untimed warmup pair, five measured pairs alternate order
`reference,optimized` then `optimized,reference`. Each paired ratio is
`optimized_ns / reference_ns`; the adoption statistic is the median of those
five ratios. Raw per-backend durations and all paired ratios remain in result
diagnostics. `runtime_ms` is the sum of the four candidate median runtimes;
`quality_error_l2` is the maximum per-strategy position/normal RMS delta.

A strategy is publicly supported only when all parity rules pass, actual
identity is `cpu_optimized`, no fallback occurs, and the median paired ratio is
`<= 0.80`. Each strategy is decided independently. An unsupported ordinary
request is rejected during preview and never rerun as the reference. The
comparison path may execute an unadopted candidate solely to retain its
negative evidence.

Screening unit fixtures and this confirmation/performance suite have different
dimensions, seeds, target counts, and noise definitions. No result may be used
to retune this v1 protocol; a changed protocol requires a new version and run
identity.
