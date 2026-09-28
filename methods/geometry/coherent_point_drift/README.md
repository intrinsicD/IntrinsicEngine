# Coherent Point Drift

Rigid (default), affine, nonrigid and Bayesian (BCPD, METHOD-050) Coherent Point Drift
(Myronenko & Song 2010; Hirose 2021) in `src/geometry/Geometry.Registration.CoherentPointDrift.*`:
one shared E-step, four M-steps, a `Solver` for step-by-step runs and an iteration
observer. The single-threaded reference stays canonical; optimized backends (METHOD-049,
`Geometry.Registration.CoherentPointDrift.EStep`) are opt-in. Formulation, bounds, units
and failure states: [paper.md](paper.md).

## ICP or CPD?

- ICP (`Geometry.Registration`) matches each source point to its nearest target point and
  is fast (spatial indices, O(N log M) per iteration), but hard correspondences make it
  sensitive to the initial pose, noise and outliers.
- CPD weighs every source/target pair (soft correspondences), models outliers with `w`,
  shrinks its matching radius (sigma) as it converges, and also fits affine and smooth
  nonrigid motion. The reference costs O(N*M) per iteration and O(M^3) for nonrigid
  solves; the optimized E-steps and the low-rank solve below cut both.
- Both are local: neither recovers large rotations of symmetric shapes on its own.

## Parameters in practice

- `OutlierWeight` 0.05-0.3 for scans with clutter or partial overlap; 0 only when every
  target point has a counterpart.
- `EstimateScale` false for scans in the same units.
- Nonrigid `Beta` (kernel width, normalized units) sets how far motion propagates;
  `Lambda` sets how strongly it is smoothed. Defaults 2 and 3 follow the paper's code.

## Choosing a backend (METHOD-049)

| `Params::EStep` | Result | When |
| --- | --- | --- |
| `Reference` (default) | exact, single thread | parity oracle, small inputs |
| `Dense` | exact up to rounding, parallel, thread-count independent | wide kernels, any size |
| `Truncated` | per-row relative error <= `EStepTolerance`, reported | narrow kernels (late iterations) |
| `FastGauss` | same bound, exact fixups | measured slower than dense in 3-D; kept for completeness |
| `Auto` (editor default) | dense or truncated per iteration from a 64-row probe | general use |
| `Nystrom` | approximate while the kernel is wide (sampled error estimate, not a bound), then exact as `Auto` | large inputs (>= 10^4 points); results report `EStepSampledError` |

`LowRank = k` replaces the nonrigid/Bayesian Gram matrix by k Nystroem eigenpairs (O(M k^2)
per iteration, up to 1,000,000 source points) and reports its sampled kernel error.
Measured (claim C113, 16 threads, Release, medians): rigid auto 19.9x the reference at
10^4 points (6.7x of the dense speedup from threads) and 2.2x dense at 10^5 with deltas
<= 1.4e-12; low rank 50 is 26.8x faster than the full kernel at 10^3 points. Reference
parity is measured up to 10^4 points (rigid) and 10^3 (low rank); beyond that it is chained.
A requested rank is an upper bound (`KernelRank` reports the effective one). Wide-kernel
iterations remain dense-bound.

## Bayesian CPD (METHOD-050)

`Variant::Bayesian` fits a similarity of a Gaussian-process deformation with per-point
mixing weights (`Kappa`, infinity = equal) and outlier weight omega (`OutlierWeight`).
Keep `PosteriorVarianceTerms` off unless you need the paper's full update: with smooth
kernels it collapses the scale (claim C114). `SubsampleSource`/`SubsampleTarget` register
farthest-point samples and interpolate the deformation to every point; use omega > 0 and
samples dense enough to keep correspondences. The similarity/deformation split is not
identifiable under weak priors; read the registered positions, not s R t alone.

## Complexity and memory

| Variant | Per iteration | Memory |
| --- | --- | --- |
| Rigid, affine | O(N*M) E-step + O(N+M) | O(N+M) |
| Nonrigid | O(N*M) + O(M^3) LU | O(M^2) kernel, M <= 8192 |
| Nonrigid/Bayesian low rank | E-step + O(M k^2) | O(M k), M <= 1,000,000 |
| Bayesian full kernel | O(N*M) + O(M^3) Cholesky (+ O(M^3) with variance terms) | O(M^2), M <= 8192 |

## In the editor

`Runtime.RegistrationOperations` (RUNTIME-273) runs CPD on any two point-domain entities
from the `sandbox.coherent_point_drift` section: a run captures both sets in world space,
iterates on a worker (all at once or step by step), streams a per-iteration trace and the
moving source positions, and publishes on Apply as one undoable step. Rigid results
drive the source Transform; affine and nonrigid results (which a TRS transform cannot
hold) overwrite the source positions or write a named vec3 displacement property in the
source's local space. Agents use `preview_registration` and `run_registration`
(`{"method": "cpd"}`).

## Evidence

- Tests: `tests/unit/geometry/Test.CoherentPointDrift.cpp` (rigid/affine/nonrigid
  recovery, outlier robustness, reflection handling, GEOM-058 E-step agreement,
  determinism and step mode, termination and failure states).
- Tests: `Test.CoherentPointDriftAccelerated.cpp` (parity per policy, thread-count
  determinism, truncation and fast-Gauss bound validity, two-pass fallback, low-rank
  convergence) and `Test.CoherentPointDriftBayesian.cpp` (similarity and deformation
  recovery, clutter, density, subsampling, accelerated parity, fail-closed states).
- Benchmarks: `geometry.coherent_point_drift.reference.smoke`
  ([manifest](../../../benchmarks/geometry/manifests/coherent_point_drift_reference_smoke.yaml)),
  ground-truth RMS per variant including Bayesian;
  `geometry.coherent_point_drift.accelerated.smoke` (parity);
  `geometry.coherent_point_drift.accelerated`
  ([manifest](../../../benchmarks/geometry/manifests/coherent_point_drift_accelerated.yaml)),
  the opt-in scaling profile behind C113
  (`ara/evidence/diagnostics/method049_cpd_accelerated_20260928/`).
